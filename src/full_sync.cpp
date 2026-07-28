// SPDX-License-Identifier: Apache-2.0

#include "async_command_exec.hpp"
#include "manager.hpp"
#include "utility.hpp"

#include <phosphor-logging/lg2.hpp>

#include <chrono>
#include <sstream>
#include <string>

namespace data_sync
{

void Manager::filterPaths(const config::DataSyncConfig& dataSyncCfg,
                          PathTimestampMap& pathMap)
{
    // Remove excluded paths from the map
    if (dataSyncCfg._excludeList.has_value())
    {
        std::erase_if(pathMap, [&](const auto& entry) {
            return dataSyncCfg._excludeList->first.contains(entry.first);
        });
    }

    // If an include list is configured, remove everything not in it from the
    // map
    if (dataSyncCfg._includeList.has_value())
    {
        std::erase_if(pathMap, [&](const auto& entry) {
            return !dataSyncCfg._includeList->contains(entry.first);
        });
    }
}

sdbusplus::async::task<std::optional<Manager::PathTimestampMap>>
    Manager::pullPeerInfo(const config::DataSyncConfig& dataSyncCfg)
{
    std::string cmd;
    getRsyncCmd(RsyncMode::PullPeerInfo, dataSyncCfg._path, cmd);
    if (cmd.empty())
    {
        co_return std::nullopt;
    }
    lg2::debug("Pull peer info cmd: [{CMD}]", "CMD", cmd);

    data_sync::async::AsyncCommandExecutor executor(_ctx);
    auto result = co_await executor.execCmd(cmd);
    if (result.first != 0)
    {
        lg2::error(
            "Failed to pull peer info for [{PATH}], ErrCode: {ERRCODE}, ErrMsg: {ERRMSG}, Cmd : [{CMD}]",
            "PATH", dataSyncCfg._path, "ERRCODE", result.first, "ERRMSG",
            result.second, "CMD", cmd);
        co_return std::nullopt;
    }

    PathTimestampMap pathTimestamps;
    std::istringstream output(result.second);
    std::string line;

    while (std::getline(output, line))
    {
        std::istringstream lineStream(line);
        std::string perms;
        std::string size;
        std::string date;
        std::string time;
        std::string relativePath;

        if (lineStream >> perms >> size >> date >> time >> relativePath)
        {
            if (relativePath == ".")
            {
                continue;
            }

            std::string dateTime{};
            dateTime.append(date);
            dateTime.append(" ");
            dateTime.append(time);
            if (const auto timestamp = utility::parseDateTimeToEpoch(dateTime))
            {
                pathTimestamps.emplace(dataSyncCfg._path / relativePath,
                                       *timestamp);
            }
        }
    }

    filterPaths(dataSyncCfg, pathTimestamps);

    co_return std::make_optional(std::move(pathTimestamps));
}

Manager::PathTimestampMap Manager::collectLocalPathTimestamps(
    const config::DataSyncConfig& dataSyncCfg)
{
    PathTimestampMap pathTimestamps;

    std::error_code ec;
    if (!fs::exists(dataSyncCfg._path, ec))
    {
        return pathTimestamps;
    }

    if (dataSyncCfg._isPathDir)
    {
        for (const auto& entry :
             fs::recursive_directory_iterator(dataSyncCfg._path))
        {
            if (!entry.is_regular_file())
            {
                continue;
            }
            auto timestamp = utility::fileTimeToEpoch(entry.last_write_time());
            if (!timestamp)
            {
                continue;
            }
            pathTimestamps.emplace(entry.path(), *timestamp);
        }
    }
    else if (fs::is_regular_file(dataSyncCfg._path, ec))
    {
        auto timestamp =
            utility::fileTimeToEpoch(fs::last_write_time(dataSyncCfg._path));
        if (timestamp)
        {
            pathTimestamps.emplace(dataSyncCfg._path, *timestamp);
        }
    }

    filterPaths(dataSyncCfg, pathTimestamps);

    return pathTimestamps;
}

Manager::PathList
    Manager::getPeerDeletedPath(const PathTimestampMap& localInfo,
                                const PathTimestampMap& peerInfo,
                                std::chrono::seconds syncDisableTime)
{
    PathList remotelyDeletedPaths;

    for (const auto& [path, timestamp] : localInfo)
    {
        // Paths not present on peer BMC with a timestamp older than
        // syncDisableTime are deleted paths on the peer intentionally. Paths
        // newer than syncDisableTime were created locally during the disabled
        // window and must be kept.
        if (!peerInfo.contains(path) && timestamp < syncDisableTime)
        {
            remotelyDeletedPaths.emplace_back(path);
        }
    }

    return remotelyDeletedPaths;
}

void Manager::deletePeerDeletedPaths(const PathList& remotelyDeletedPaths)
{
    for (const auto& path : remotelyDeletedPaths)
    {
        std::error_code ec;
        fs::remove_all(path, ec);
        if (ec)
        {
            lg2::error(
                "Failed to delete remotely deleted path [{PATH}], Error: {ERROR}",
                "PATH", path, "ERROR", ec.message());
            continue;
        }

        lg2::debug("Deleted [{PATH}] on local BMC as it deleted at peer",
                   "PATH", path);
    }
}

sdbusplus::async::task<>
    Manager::cleanupPeerDeletedFiles(const config::DataSyncConfig& cfg)
{
    // step 1 : read the sync disabled timestamp
    auto syncDisableTime = data_sync::persist::readRawFile(
        data_sync::persist::SyncDisableTimeFile);
    if (!syncDisableTime)
    {
        lg2::warning(
            "Sync disable time missing, skipping pre-sync cleanup algorithm for [{PATH}]",
            "PATH", cfg._path);
        co_return;
    }

    lg2::debug("Running pre-fullsync for [{PATH}]", "PATH", cfg._path);

    // Step 2 : Collect the list of available files and their mtime from local
    // BMC
    auto localInfo = collectLocalPathTimestamps(cfg);

    // Step 3 : Collect the list of available files and their mtime from peer
    // BMC
    auto peerInfo = co_await pullPeerInfo(cfg);
    if (!peerInfo)
    {
        lg2::warning(
            "Failed to pull peer info for [{PATH}], skipping pre-sync cleanup",
            "PATH", cfg._path);
        co_return;
    }

    // Step 4 : Get paths that deleted in peer by using local and peer paths
    // meta data
    auto remotelyDeletedPaths = getPeerDeletedPath(
        localInfo, *peerInfo, std::chrono::seconds{*syncDisableTime});

    // Step 5 : Remove the deleted paths at the peer on local BMC
    deletePeerDeletedPaths(remotelyDeletedPaths);

    co_return;
}

sdbusplus::async::task<bool>
    Manager::bidirectionFullSync(const config::DataSyncConfig& cfg)
{
    // NOLINTNEXTLINE(clang-analyzer-core.uninitialized.Branch)
    co_await cleanupPeerDeletedFiles(cfg);
    co_return co_await syncData(cfg);
}

sdbusplus::async::task<void> Manager::startFullSync()
{
    lg2::info("Full Sync started");
    setFullSyncStatus(FullSyncStatus::FullSyncInProgress);

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
                auto task =
                    (cfg._syncDirection == config::SyncDirection::Bidirectional)
                        // NOLINTNEXTLINE(clang-analyzer-core.uninitialized.Branch)
                        ? bidirectionFullSync(cfg)
                        : syncData(cfg);

                _ctx.spawn(
                    std::move(task) |
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
        lg2::info(
            "Full Sync completed successfully. Elapsed time : [{DURATION_SECONDS}] seconds",
            "DURATION_SECONDS", FullsyncElapsedTime.count());
        setFullSyncStatus(FullSyncStatus::FullSyncCompleted);
        setSyncEventsHealth(SyncEventsHealth::Ok);
    }
    else
    {
        lg2::error(
            "Full Sync failed. Elapsed time : [{DURATION_SECONDS}] seconds",
            "DURATION_SECONDS", FullsyncElapsedTime.count());
        setFullSyncStatus(FullSyncStatus::FullSyncFailed);
    }

    co_return;
}

} // namespace data_sync
