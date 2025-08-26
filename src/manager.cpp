// SPDX-License-Identifier: Apache-2.0

#include "manager.hpp"

#include "async_command_exec.hpp"
#include "data_watcher.hpp"

#include <fcntl.h>
#include <spawn.h>

#include <nlohmann/json.hpp>
#include <phosphor-logging/lg2.hpp>

#include <cstdlib>
#include <exception>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

namespace data_sync
{

Manager::Manager(sdbusplus::async::context& ctx,
                 std::unique_ptr<ext_data::ExternalDataIFaces>&& extDataIfaces,
                 const fs::path& dataSyncCfgDir) :
    _ctx(ctx), _extDataIfaces(std::move(extDataIfaces)),
    _dataSyncCfgDir(dataSyncCfgDir), _syncBMCDataIface(ctx, *this)
{
    _ctx.spawn(init());
}

// NOLINTNEXTLINE
sdbusplus::async::task<> Manager::init()
{
    co_await sdbusplus::async::execution::when_all(
        parseConfiguration(), _extDataIfaces->startExtDataFetches());

    if (_syncBMCDataIface.disable_sync())
    {
        lg2::info(
            "Sync is Disabled, data sync cannot be performed to the sibling BMC.");
        co_return;
    }

    // TODO: Explore the possibility of running FullSync and Background Sync
    // concurrently
    if (_extDataIfaces->bmcRedundancy())
    {
        co_await startFullSync();
    }

    co_return co_await startSyncEvents();
}

// NOLINTNEXTLINE
sdbusplus::async::task<> Manager::parseConfiguration()
{
    auto parse = [this](const auto& configFile) {
        try
        {
            std::ifstream file;
            file.open(configFile.path());

            nlohmann::json configJSON(nlohmann::json::parse(file));

            if (configJSON.contains("Files"))
            {
                std::ranges::transform(
                    configJSON["Files"],
                    std::back_inserter(this->_dataSyncConfiguration),
                    [](const auto& element) {
                    return config::DataSyncConfig(element, false);
                });
            }
            if (configJSON.contains("Directories"))
            {
                std::ranges::transform(
                    configJSON["Directories"],
                    std::back_inserter(this->_dataSyncConfiguration),
                    [](const auto& element) {
                    return config::DataSyncConfig(element, true);
                });
            }
        }
        catch (const std::exception& e)
        {
            // TODO Create error log
            lg2::error("Failed to parse the configuration file : {CONFIG_FILE},"
                       " exception : {EXCEPTION}",
                       "CONFIG_FILE", configFile.path(), "EXCEPTION", e);
        }
    };

    if (fs::exists(_dataSyncCfgDir) && fs::is_directory(_dataSyncCfgDir))
    {
        std::ranges::for_each(fs::directory_iterator(_dataSyncCfgDir), parse);
    }

    co_return;
}

bool Manager::isSyncEligible(const config::DataSyncConfig& dataSyncCfg)
{
    using enum config::SyncDirection;
    using enum ext_data::BMCRole;

    if ((dataSyncCfg._syncDirection == Bidirectional) ||
        ((dataSyncCfg._syncDirection == Active2Passive) &&
         this->_extDataIfaces->bmcRole() == Active) ||
        ((dataSyncCfg._syncDirection == Passive2Active) &&
         this->_extDataIfaces->bmcRole() == Passive))
    {
        return true;
    }
    else
    {
        // TODO Trace is required, will overflow?
        lg2::debug("Sync is not required for [{PATH}] due to "
                   "SyncDirection: {SYNC_DIRECTION} BMCRole: {BMC_ROLE}",
                   "PATH", dataSyncCfg._path, "SYNC_DIRECTION",
                   dataSyncCfg.getSyncDirectionInStr(), "BMC_ROLE",
                   _extDataIfaces->bmcRole());
    }
    return false;
}

// NOLINTNEXTLINE
sdbusplus::async::task<> Manager::startSyncEvents()
{
    std::ranges::for_each(
        _dataSyncConfiguration |
            std::views::filter([this](const auto& dataSyncCfg) {
        return this->isSyncEligible(dataSyncCfg);
    }),
        [this](const auto& dataSyncCfg) {
        using enum config::SyncType;
        if (dataSyncCfg._syncType == Immediate)
        {
            this->_ctx.spawn(this->monitorDataToSync(dataSyncCfg));
        }
        else if (dataSyncCfg._syncType == Periodic)
        {
            this->_ctx.spawn(this->monitorTimerToSync(dataSyncCfg));
        }
    });
    co_return;
}

sdbusplus::async::task<bool>
    // NOLINTNEXTLINE
    Manager::syncData(const config::DataSyncConfig& dataSyncCfg,
                      fs::path srcPath)
{
    using namespace std::string_literals;

    // For more details about CLI options, refer rsync man page.
    // https://download.samba.org/pub/rsync/rsync.1#OPTION_SUMMARY
    std::string syncCmd{
        "rsync --compress --recursive --perms --group --owner --times --atimes"
        " --update --relative --delete --delete-missing-args "};

    if (dataSyncCfg._excludeList.has_value())
    {
        syncCmd.append(dataSyncCfg._excludeList->second);
    }

    if (!srcPath.empty())
    {
        syncCmd.append(" "s + srcPath.string());
    }
    else
    {
        syncCmd.append(" "s + dataSyncCfg._path.string());
    }

#ifdef UNIT_TEST
    syncCmd.append(" "s);
#else
    // TODO Support for remote (i,e sibling BMC) copying needs to be added.
#endif

    // Add destination data path if configured
    syncCmd.append(dataSyncCfg._destPath.value_or(fs::path("")));
    lg2::debug("RSYNC CMD : {CMD}", "CMD", syncCmd);

    data_sync::async::AsyncCommandExecutor executor(_ctx);
    auto result = co_await executor.execCmd(syncCmd); // NOLINT
    if (result.first != 0)
    {
        // TODOs:
        // 1. Retry based on rsync error code
        // 2. Create error log and Disable redundancy if retry fails
        // 3. Perform a callout

        // NOTE: The following line is commented out as part of a temporary
        // workaround. We are forcing Full Sync to succeed even if data syncing
        // fails. This change should be reverted once proper error handling is
        // implemented.
        // setSyncEventsHealth(SyncEventsHealth::Critical);

        lg2::error("Error syncing: {PATH}", "PATH", dataSyncCfg._path);

        co_return false;
    }
    co_return true;
}

sdbusplus::async::task<>
    // NOLINTNEXTLINE
    Manager::monitorDataToSync(const config::DataSyncConfig& dataSyncCfg)
{
    try
    {
        uint32_t eventMasksToWatch = IN_CLOSE_WRITE | IN_MOVE | IN_DELETE_SELF;
        if (dataSyncCfg._isPathDir)
        {
            eventMasksToWatch |= IN_CREATE | IN_DELETE;
        }

        // Create watcher for the dataSyncCfg._path
        watch::inotify::DataWatcher dataWatcher(_ctx, IN_NONBLOCK,
                                                eventMasksToWatch, dataSyncCfg);

        while (!_ctx.stop_requested() && !_syncBMCDataIface.disable_sync())
        {
            if (auto dataOperations = co_await dataWatcher.onDataChange();
                !dataOperations.empty())
            {
                // Below is temporary check to avoid sync when disable sync is
                // set to true.
                // TODO: add receiver logic to stop sync events when disable
                // sync is set to true.
                if (_syncBMCDataIface.disable_sync())
                {
                    break;
                }
                for (const auto& [path, dataOp] : dataOperations)
                {
                    co_await syncData(dataSyncCfg, path);
                }
            }
        }
    }
    catch (std::exception& e)
    {
        // TODO : Create error log if fails to create watcher for a
        // file/directory.
        lg2::error("Failed to create watcher object for {PATH}. Exception : "
                   "{ERROR}",
                   "PATH", dataSyncCfg._path, "ERROR", e.what());
    }
    co_return;
}

sdbusplus::async::task<>
    // NOLINTNEXTLINE
    Manager::monitorTimerToSync(const config::DataSyncConfig& dataSyncCfg)
{
    while (!_ctx.stop_requested() && !_syncBMCDataIface.disable_sync())
    {
        co_await sdbusplus::async::sleep_for(
            _ctx, dataSyncCfg._periodicityInSec.value());
        // Below is temporary check to avoid sync when disable sync is set to
        // true.
        // TODO: add receiver logic to stop sync events when disable sync is set
        // to true.
        if (_syncBMCDataIface.disable_sync())
        {
            break;
        }
        co_await syncData(dataSyncCfg);
    }
    co_return;
}

void Manager::disableSyncPropChanged(bool disableSync)
{
    if (disableSync)
    {
        // TODO: Disable all sync events using Sender Receiver.
        lg2::info("Sync is Disabled, Stopping events");
    }
    else
    {
        lg2::info("Sync is Enabled, Starting events");
        _ctx.spawn(startSyncEvents());
    }
}

void Manager::setFullSyncStatus(const FullSyncStatus& fullSyncStatus)
{
    if (_syncBMCDataIface.full_sync_status() == fullSyncStatus)
    {
        return;
    }
    _syncBMCDataIface.full_sync_status(fullSyncStatus);

    try
    {
        data_sync::persist::update(data_sync::persist::key::fullSyncStatus,
                                   fullSyncStatus);
    }
    catch (const std::exception& e)
    {
        lg2::error(
            "Error writing fullSyncStatus property to JSON file: {ERROR}",
            "ERROR", e);
    }
}

void Manager::setSyncEventsHealth(const SyncEventsHealth& syncEventsHealth)
{
    if (_syncBMCDataIface.sync_events_health() == syncEventsHealth)
    {
        return;
    }
    _syncBMCDataIface.sync_events_health(syncEventsHealth);
    try
    {
        data_sync::persist::update(data_sync::persist::key::syncEventsHealth,
                                   syncEventsHealth);
    }
    catch (const std::exception& e)
    {
        lg2::error(
            "Error writing syncEventsHealth property to JSON file: {ERROR}",
            "ERROR", e);
    }
}

// NOLINTNEXTLINE
sdbusplus::async::task<void> Manager::startFullSync()
{
    setFullSyncStatus(FullSyncStatus::FullSyncInProgress);
    lg2::info("Full Sync started");

    auto fullSyncStartTime = std::chrono::steady_clock::now();

    auto syncResults = std::vector<bool>();
    size_t spawnedTasks = 0;

    for (const auto& cfg : _dataSyncConfiguration)
    {
        // TODO: add receiver logic to stop fullsync when disable sync is set to
        // true.
        if (isSyncEligible(cfg))
        {
            _ctx.spawn(
                syncData(cfg) |
                stdexec::then([&syncResults, &spawnedTasks](bool result) {
                syncResults.push_back(result);
                spawnedTasks--; // Decrement the number of spawned tasks
            }));
            spawnedTasks++;     // Increment the number of spawned tasks
        }
    }

    while (spawnedTasks > 0)
    {
        co_await sdbusplus::async::sleep_for(_ctx,
                                             std::chrono::milliseconds(50));
    }

    auto fullSyncEndTime = std::chrono::steady_clock::now();
    auto FullsyncElapsedTime = std::chrono::duration_cast<std::chrono::seconds>(
        fullSyncEndTime - fullSyncStartTime);

    // If any sync operation fails, the FullSync will be considered failed;
    // otherwise, it will be marked as completed.
    if (std::ranges::all_of(syncResults,
                            [](const auto& result) { return result; }))
    {
        setFullSyncStatus(FullSyncStatus::FullSyncCompleted);
        setSyncEventsHealth(SyncEventsHealth::Ok);
        lg2::info("Full Sync completed successfully");
    }
    else
    {
        // Forcefully marking full sync as successful, even if data syncing
        // fails.
        // TODO: Revert this workaround once the proper logic is implemented
        setFullSyncStatus(FullSyncStatus::FullSyncCompleted);
        setSyncEventsHealth(SyncEventsHealth::Ok);
        lg2::info("Full Sync passed temporarily despite sync failures");

        // setFullSyncStatus(FullSyncStatus::FullSyncFailed);
        // lg2::info("Full Sync failed");
    }

    // total duration/time diff of the Full Sync operation
    lg2::info("Elapsed time for full sync: [{DURATION_SECONDS}] seconds",
              "DURATION_SECONDS", FullsyncElapsedTime.count());

    co_return;
}

} // namespace data_sync
