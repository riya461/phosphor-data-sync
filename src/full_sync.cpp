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
