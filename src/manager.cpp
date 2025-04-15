// SPDX-License-Identifier: Apache-2.0

#include "config.h"

#include "manager.hpp"

#include "async_command_exec.hpp"
#include "data_watcher.hpp"
#include "notify_sibling.hpp"

#include <nlohmann/json.hpp>
#include <phosphor-logging/lg2.hpp>

#include <cstdlib>
#include <exception>
#include <experimental/scope>
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
            "Sync is disabled, data sync cannot be performed to the sibling BMC.");
        co_return;
    }
    if (fs::exists(NOTIFY_SERVICES_DIR))
    {
        _ctx.spawn(monitorServiceNotifications());
    }

    // TODO: Explore the possibility of running FullSync and Background Sync
    // concurrently
    if (_extDataIfaces->bmcRedundancy())
    {
        // NOLINTNEXTLINE
        co_await startFullSync();
    }

    // NOLINTNEXTLINE
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

// NOLINTNEXTLINE
sdbusplus::async::task<> Manager::monitorServiceNotifications()
{
    lg2::debug("Starting monitoring for sibling notifications...");

    try
    {
        // TODO : Process the unprocessed notify requests during startup

        // Start watching the NOTIFY_SERVICE_DIR
        // Monitoring for IN_MOVED_TO only as rsync creates a temporary file in
        // the destination and then rename to original file.
        watch::inotify::DataWatcher notifyWatcher(
            _ctx, IN_NONBLOCK, IN_MOVED_TO, NOTIFY_SERVICES_DIR);

        while (!_ctx.stop_requested())
        {
            if (auto dataOperations = co_await notifyWatcher.onDataChange();
                !dataOperations.empty())
            {
                for (const auto& [path, Op] : dataOperations)
                {
                    _notifyReqs.emplace_back(
                        std::make_unique<notify::NotifyService>(
                            _ctx, *_extDataIfaces, path,
                            [this](notify::NotifyService* ptr) {
                        std::erase_if(_notifyReqs, [ptr](const auto& p) {
                            return p.get() == ptr;
                        });
                    }));
                }
            }
        }
    }
    catch (std::exception& e)
    {
        // TODO : Create error log if fails to create watcher.
        constexpr auto notifyDir{NOTIFY_SERVICES_DIR};
        lg2::error("Failed to create watcher for {NOTIFY_DIR}. Exception : "
                   "{EXCEP}",
                   "NOTIFY_DIR", notifyDir, "EXCEP", e);
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
            try
            {
                this->_ctx.spawn(this->monitorDataToSync(dataSyncCfg));
            }
            catch (const std::exception& e)
            {
                setSyncEventsHealth(SyncEventsHealth::Critical);
                lg2::error(
                    "Failed to configure immediate sync for {PATH}: {EXCEPTION}",
                    "EXCEPTION", e, "PATH", dataSyncCfg._path);
            }
        }
        else if (dataSyncCfg._syncType == Periodic)
        {
            try
            {
                this->_ctx.spawn(this->monitorTimerToSync(dataSyncCfg));
            }
            catch (const std::exception& e)
            {
                setSyncEventsHealth(SyncEventsHealth::Critical);
                lg2::error("Failed to configure periodic sync for {PATH}: "
                           "{EXCEPTION}",
                           "EXCEPTION", e, "PATH", dataSyncCfg._path);
            }
        }
    });
    co_return;
}

// Disabled because this function conditionally accesses class members when
// unit tests are not enabled.
// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
void Manager::getRsyncCmd(RsyncMode mode,
                          const config::DataSyncConfig& dataSyncCfg,
                          const std::string& srcPath, std::string& cmd)
{
    using namespace std::string_literals;

    cmd.append("rsync --compress --recursive --perms --group --owner --times "
               "--atimes --update"s);
    if (mode == RsyncMode::Sync)
    {
        // Appending required flags to sync data between BMCs
        // For more details about CLI options, refer rsync man page.
        // https://download.samba.org/pub/rsync/rsync.1#OPTION_SUMMARY

        cmd.append(" --relative --delete --delete-missing-args --stats"s);

        if (dataSyncCfg._excludeList.has_value())
        {
            cmd.append(dataSyncCfg._excludeList->second);
        }
    }
    else if (mode == RsyncMode::Notify)
    {
        // Appending the required flags to notify the siblng
        cmd.append(" --remove-source-files"s);
    }

    if (!srcPath.empty())
    {
        // Append the modified path name as its available
        cmd.append(" "s + srcPath);
    }
    else if ((dataSyncCfg._includeList.has_value()) && (srcPath.empty()))
    {
        // Build rsync command only for paths that currently exist in the
        // filesystem this avoids running rsync with invalid or missing source
        // paths
        auto anyExist = std::ranges::fold_left(
            dataSyncCfg._includeList.value() |
                std::views::filter([](const fs::path& p) {
            std::error_code ec;
            return fs::exists(p, ec);
        }),
            false, [&cmd]([[maybe_unused]] auto, const fs::path& p) {
            cmd.append(" ");
            cmd.append(p.string());
            return true; // mark that at least one path exists
        });

        // Skip sync if none of the configured include paths exist
        // Future inotify events will trigger sync once files appear
        if (!anyExist)
        {
            lg2::debug(
                "IncludeList: none of the configured source paths exist, skipping rsync");
            cmd.clear();
            return;
        }
    }
    else
    {
        cmd.append(" "s + dataSyncCfg._path.string());
    }

#ifdef UNIT_TEST
    cmd.append(" "s);
#else
    static const std::string rsyncdURL(
        std::format(" rsync://localhost:{}/{}",
                    (_extDataIfaces->bmcPosition() == 0 ? BMC1_RSYNC_PORT
                                                        : BMC0_RSYNC_PORT),
                    RSYNCD_MODULE_NAME));
    cmd.append(rsyncdURL);
#endif

    if (mode == RsyncMode::Sync)
    {
        // Add destination data path if configured
        cmd.append(dataSyncCfg._destPath.value_or(fs::path("")).string());
    }
    else if (mode == RsyncMode::Notify)
    {
        cmd.append(NOTIFY_SERVICES_DIR);
    }
}

sdbusplus::async::task<void>
    // NOLINTNEXTLINE
    Manager::triggerSiblingNotification(
        const config::DataSyncConfig& dataSyncCfg, const std::string& srcPath)
{
    std::error_code ec;
    if (dataSyncCfg._notifySibling.value()._paths.has_value())
    {
        if (!(dataSyncCfg._notifySibling.value()._paths.value().contains(
                srcPath)) &&
            (!fs::equivalent(srcPath, dataSyncCfg._path, ec)))
        {
            // Modified path doesn't need to notify
            lg2::debug("Sibling notification not configured for the path : "
                       "[{SRCPATH}] under the configured Path : [{CFGPATH}]",
                       "SRCPATH", srcPath, "CFGPATH", dataSyncCfg._path);
            co_return;
        }
    }
    // initiate sibling notification
    notify::NotifySibling notifySibling(dataSyncCfg, srcPath);
    std::string notifyCmd{};
    getRsyncCmd(RsyncMode::Notify, dataSyncCfg,
                notifySibling.getNotifyFilePath().string(), notifyCmd);
    lg2::debug("Rsync sibling notify cmd : {CMD}", "CMD", notifyCmd);

    data_sync::async::AsyncCommandExecutor executor(_ctx);
    // NOLINTNEXTLINE
    auto result = co_await executor.execCmd(notifyCmd);
    if (result.first != 0)
    {
        // TODO : Need to retry if notify command fails.
        lg2::error(
            "Failed to send notify request for the path [{PATH}], under the "
            "configured path: [{CFGPATH}] ErrCode : {ERRCODE}, Error : {ERROR}",
            "PATH", srcPath, "CFGPATH", dataSyncCfg._path, "ERRCODE",
            result.first, "ERROR", result.second);
    }
    else
    {
        lg2::debug(
            "Successfully send notify request for the path [{SRCPATH}], under "
            "the configured path : [{CFGPATH}]",
            "SRCPATH", srcPath, "CFGPATH", dataSyncCfg._path);
    }
    co_return;
}

sdbusplus::async::task<bool>
    // NOLINTNEXTLINE
    Manager::retrySync(const config::DataSyncConfig& cfg, fs::path srcPath,
                       size_t retryCount)
{
    const fs::path currentSrcPath = srcPath.empty() ? cfg._path : srcPath;

    if (retryCount++ < cfg._retry->_retryAttempts)
    {
        lg2::debug(
            "Retry [{RETRY_ATTEMPT}/{MAX_ATTEMPTS}] for [{SRC_PATH}] after "
            "[{RETRY_INTERVAL}s]",
            "RETRY_ATTEMPT", retryCount, "MAX_ATTEMPTS",
            cfg._retry->_retryAttempts, "SRC_PATH", currentSrcPath,
            "RETRY_INTERVAL", cfg._retry->_retryIntervalInSec.count());

        co_await sleep_for(_ctx, std::chrono::seconds(
                                     cfg._retry->_retryIntervalInSec.count()));

        // NOLINTNEXTLINE
        co_return co_await syncData(cfg, std::move(srcPath), retryCount);
    }

    // NOTE: The following line is commented out as part of a temporary
    // workaround. We are forcing Full Sync to succeed even if data syncing
    // fails. This change should be reverted once proper error handling is
    // implemented.
    // setSyncEventsHealth(SyncEventsHealth::Critical);
    lg2::error("Sync failed after [{MAX_ATTEMPTS}] retries for [{SRC_PATH}]",
               "MAX_ATTEMPTS", cfg._retry->_retryAttempts, "SRC_PATH",
               currentSrcPath);
    co_return false;
}

sdbusplus::async::task<bool>
    // NOLINTNEXTLINE
    Manager::syncData(const config::DataSyncConfig& dataSyncCfg,
                      fs::path srcPath, size_t retryCount)
{
    // Don't sync if the sync is disabled
    if (_syncBMCDataIface.disable_sync())
    {
        co_return false;
    }

    using std::experimental::scope_exit;
    const fs::path currentSrcPath = srcPath.empty() ? dataSyncCfg._path
                                                    : srcPath;

    auto cleanup = scope_exit([&dataSyncCfg, &currentSrcPath]() noexcept {
        // remove this path from the in-progress set once the first(main)
        // attempt completes
        dataSyncCfg._syncInProgressPaths.erase(currentSrcPath);
    });

    if (retryCount == 0)
    {
        if (dataSyncCfg._syncInProgressPaths.contains(currentSrcPath))
        {
            lg2::debug("Skipping sync for [{SRC}]: already in progress", "SRC",
                       currentSrcPath);
            cleanup.release(); // nothing inserted, skip cleanup
            co_return true;
        }
        dataSyncCfg._syncInProgressPaths.emplace(currentSrcPath);
    }
    else
    {
        // skip cleanup for retries, the main attempt will handle it
        cleanup.release();
    }

    std::string syncCmd{};
    getRsyncCmd(RsyncMode::Sync, dataSyncCfg, srcPath.string(), syncCmd);

    if (syncCmd.empty())
    {
        co_return true;
    }

    lg2::debug("Rsync command: {CMD}", "CMD", syncCmd);

    data_sync::async::AsyncCommandExecutor executor(_ctx);
    // NOLINTNEXTLINE
    auto result = co_await executor.execCmd(syncCmd);
    lg2::debug("Rsync cmd return code : {RET} : output : {OUTPUT}", "RET",
               result.first, "OUTPUT", result.second);

    switch (result.first)
    {
        case 0: // Success
        {
            // Notify only if configured, we know the concrete path,
            // and bytes > 0
            if (dataSyncCfg._notifySibling &&
                utility::rsync::getTransferredBytes(result.second) != 0)
            {
                // Rsync success alone doesn’t guarantee data got updated on the
                // remote.
                // Checking bytes transferred helps to confirm if any data
                // mismatch was actually synced.
                // initiate sibling notification
                // NOLINTNEXTLINE
                co_await triggerSiblingNotification(dataSyncCfg,
                                                    currentSrcPath.string());
            }
            co_return true;
        }

        case 24: // Vanished source: treat as success
        {
            // TODO: Revisit notification handling for vanished files if partial
            // data got synced
            lg2::debug(
                "Rsync exited with vanished file error for [{SRC}], treating as success",
                "SRC", currentSrcPath);
            co_return true;
        }

        // Permanent errors — do not retry
        case 1:  // syntax or usage
        case 2:  // protocol incompatibility
        case 3:  // input/output paths selection error
        case 4:  // requested action not supported
        case 6:  // daemon unable to append to log-file
        case 11: // error in file I/O
        case 13: // program diagnostics errors
        case 14: // Error in IPC code
        case 22: // Error allocating core memory buffers
        {
            lg2::error(
                "Error syncing [{PATH}], ErrCode: {ERRCODE}, Error: {ERROR}"
                "RsyncCLI: [RSYNC_CMD]",
                "PATH", currentSrcPath, "ERRCODE", result.first, "ERROR",
                result.second, "RSYNC_CMD", syncCmd);
            co_return false;
        }

        default: // Retryable
        {
            lg2::debug("Retrying rsync for [{SRC}] after error [{CODE}]", "SRC",
                       currentSrcPath, "CODE", result.first);

            // NOLINTNEXTLINE
            co_return co_await retrySync(
                dataSyncCfg, srcPath.empty() ? fs::path{} : currentSrcPath,
                retryCount);
        }
    }
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

        auto excludeList =
            dataSyncCfg._excludeList.has_value()
                ? std::make_optional<std::unordered_set<fs::path>>(
                      dataSyncCfg._excludeList.value().first)
                : std::nullopt;
        watch::inotify::DataWatcher dataWatcher(
            _ctx, IN_NONBLOCK, eventMasksToWatch, dataSyncCfg._path,
            excludeList, dataSyncCfg._includeList);

        while (!_ctx.stop_requested() && !_syncBMCDataIface.disable_sync())
        {
            // NOLINTNEXTLINE
            if (auto dataOperations = co_await dataWatcher.onDataChange();
                !dataOperations.empty())
            {
                for (const auto& [path, dataOp] : dataOperations)
                {
                    // NOLINTNEXTLINE
                    _ctx.spawn(
                        syncData(dataSyncCfg, path) |
                        stdexec::then([]([[maybe_unused]] bool result) {}));
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
        // NOLINTNEXTLINE
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
        try
        {
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
        catch (const std::exception& e)
        {
            lg2::error(
                "Full sync spawn failed for [{PATH}], Error : {EXCEPTION}",
                "PATH", cfg._path, "EXCEPTION", e);
            setFullSyncStatus(FullSyncStatus::FullSyncFailed);
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
