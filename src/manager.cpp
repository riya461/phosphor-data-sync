// SPDX-License-Identifier: Apache-2.0

#include "config.h"

#include "manager.hpp"

#include "data_watcher.hpp"

#include <nlohmann/json.hpp>
#include <phosphor-logging/lg2.hpp>

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <fcntl.h>
#include <fstream>
#include <iterator>
#include <spawn.h>
#include <string>
#include <string_view>
#include <regex>

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

#if 0
    co_await syncData(*(_dataSyncConfiguration.begin()),
            "--dry-run /var/lib/phosphor-data-sync/bmc_data_bkp/dry_run",
            "/var/lib/phosphor-data-sync/bmc_data_bkp/dry_run_dest");
#endif

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
#if 0
        // TODO Trace is required, will overflow?
        lg2::debug("Sync is not required for [{PATH}] due to "
                   "SyncDirection: {SYNC_DIRECTION} BMCRole: {BMC_ROLE}",
                   "PATH", dataSyncCfg._path, "SYNC_DIRECTION",
                   dataSyncCfg.getSyncDirectionInStr(), "BMC_ROLE",
                   ext_data::RBMC::convertRoleToString(_extDataIfaces->bmcRole()));
#endif
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
        [this](auto& dataSyncCfg) {
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

    using namespace std::chrono_literals;
    co_await sdbusplus::async::sleep_for(_ctx, 500ms);
    lg2::debug("Total No of watchers: {NO_WACTHERS}", "NO_WACTHERS", _noOfWatchers);
    co_return;
}

std::string Manager::frameIncludeString(const fs::path& cfgPath, const
        std::vector<fs::path>& includeList)
{
    using namespace std::string_literals;
    std::string includeListStr{};
    auto commaSeparatedFold = [&cfgPath](std::string listToStr,
                                    const fs::path& entry)
    {
        return std::move(listToStr) + " --include=" +
                    fs::relative(entry,cfgPath).string() + "/***";
    };
    includeListStr.append(std::ranges::fold_left(includeList, includeListStr,
                                commaSeparatedFold));

    includeListStr.append(" --exclude=*"s);
    lg2::debug("The converted list string : {LISTSTRING}", "LISTSTRING",
                    includeListStr);

    return includeListStr;
}

std::string Manager::frameExcludeString(const fs::path& cfgPath, const
        std::vector<fs::path> excludeList)
{
    using namespace std::string_literals;
    std::string excludeListStr{};
    auto commaSeparatedFold = [&cfgPath](std::string listToStr,
                                    const fs::path& entry)
    {
        return std::move(listToStr) + " --exclude=" +
                    fs::relative(entry,cfgPath).string();
    };
    excludeListStr.append(std::ranges::fold_left(excludeList, excludeListStr,
                                commaSeparatedFold));

    lg2::debug("Excluded Paths: {LISTSTRING} for [{PATH}]", "LISTSTRING",
                    excludeListStr, "PATH", cfgPath);

    return excludeListStr;
}

sdbusplus::async::task<std::string> Manager::waitForCmdCompletion(int fd)
{
    // Set non-blocking
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1)
    {
        lg2::error("fcntl failed");
        co_return "";
    }

    std::string output{};
    std::array<char, 512> buffer{};
    // The buffer size is extended so that it can fit the statistics data in one
    // read operation
    std::unique_ptr<sdbusplus::async::fdio> fdioInstance =
        std::make_unique<sdbusplus::async::fdio>(_ctx, fd);

    while (!_ctx.stop_requested())
    {
        co_await fdioInstance->next();

        int n = read(fd, buffer.data(), buffer.size());
        if (n > 0)
        {
            output += buffer.data();
            lg2::debug("*** BUFFER :\n {OUTPUT} ** " , "OUTPUT",
            buffer.data());
            buffer.fill(0);


        }
        else if (n == 0)
        {
            // EOF
            lg2::debug("EOF BUFFER");
            break;
        }
        else if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            lg2::debug("EAGAIN || EWOULDBLOCK");
            continue;
        }
        else
        {
            lg2::error("read is failed [{ERROR}]", "ERROR", strerror(errno));
            break;
        }
    }

    fdioInstance.reset();

    lg2::debug("*** OUTPUT from read :\n {OUTPUT} ** " , "OUTPUT", output);
    co_return output;
}

sdbusplus::async::task<std::pair<int, std::string>> Manager::tryWithPosiSpawn(const std::string& cmd)
{
    int pipefd[2];
    if (pipe(pipefd) == -1)
    {
        lg2::error("pipe is failed");
        co_return std::make_pair(-1, "");
    }

    posix_spawn_file_actions_t fileActions;
    posix_spawn_file_actions_init(&fileActions);
    posix_spawn_file_actions_adddup2(&fileActions, pipefd[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&fileActions, pipefd[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&fileActions, pipefd[0]);

    pid_t pid;
    const char* argv[] = {"/bin/sh", "-c", cmd.c_str(), nullptr};

    int spawnResult = posix_spawn(&pid, "/bin/sh", &fileActions, nullptr,
                                  const_cast<char* const*>(argv), nullptr);
    posix_spawn_file_actions_destroy(&fileActions);

    close(pipefd[1]); // Close write end in parent
    if (spawnResult != 0)
    {
        lg2::error("posix_spawn failed: {ERROR}", "ERROR", strerror(spawnResult));
        close(pipefd[0]);
        co_return std::make_pair(-1, "");
    }

    auto output = co_await waitForCmdCompletion(pipefd[0]);
    lg2::debug("OUTPUT IN POSIX \n {OUT}", "OUT", output);
    close(pipefd[0]);

    // Wait for child process to exit
    int status = -1;
    waitpid(pid, &status, 0);

    int exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    if (exitCode != 0)
    {
        lg2::error("Sync is failed, command[{CMD}] errorCode[{ERRCODE}] "
                   "Output[{OUT}]", "CMD", cmd, "ERRCODE", exitCode, "OUT", output);
    }

    co_return std::make_pair(exitCode, output);
}

std::string getVanishedSrcPath(const std::string& rsyncCmdOut)
{
    const std::string vanishPattern = "file has vanished: \"";
    std::string vanishSrcs;
    size_t searchPos = 0;

    while ((searchPos = rsyncCmdOut.find(vanishPattern, searchPos)) != std::string::npos)
    {
        searchPos += vanishPattern.size();
        size_t endQuote = rsyncCmdOut.find('"', searchPos);
        if (endQuote == std::string::npos)
            break;

        vanishSrcs += rsyncCmdOut.substr(searchPos, endQuote - searchPos) + " ";
        searchPos = endQuote + 1;
    }

    return vanishSrcs;
}

sdbusplus::async::task<bool>
    Manager::writeStats(
                        std::string triggerBy, std::string triggerReason, 
                        size_t retryCount,std::string elapsedTime,
                        std::pair<int, std::string> ret)
{

    std::fstream f;
    f.open("/var/log/rsync_logs" , std::ios::app);
f << "Writing\n";
    
        struct FileData {
               std::string filename;
               std::string bytes_t;
               std::string bytes_l;
               std::string modified_time;
               std::string rsync_time;
        };

    std::regex list_match{
"Item:(<f.*|\\*deleting)\\s+File:([^\\s]+)\\s+Bytes_T:([0-9]+)\\s+Bytes_L:([0-9]+)\\s+Modified:([^\\[]+)\\[([^\\]]+)\\]"
    };
    std::vector<FileData> files;
    std::smatch file_list;
    std::string output = ret.second;
        while(
            std::regex_search(output, file_list, list_match) 
            )
        {
                
                FileData file;
                file.filename = file_list[2].str();
                file.bytes_t = file_list[3].str();
                file.bytes_l = file_list[4].str();
                file.modified_time = file_list[5].str();
                file.rsync_time  = file_list[6].str();

                files.push_back(file);
                output = file_list.suffix();
            
        }
    
    // Statistics: Extract necessary stats information 
    //       Total file size, Total transferred file size, 
    //       Literal data, Matched data,
    //       Total bytes sent, Total bytes received
    std::string rsyncStats;
    rsyncStats.append("\n---Rsync stats---\n");
    const std::string statsPatternStart = "Total file size: ";
    const std::string statsPatternEnd = "File list ";
    size_t searchPos  = output.find(statsPatternStart, 0);
    size_t endPos = output.find(statsPatternEnd, searchPos);
    rsyncStats += output.substr(searchPos, endPos - searchPos);
    const std::string statsPatternSent = "sent ";
    searchPos  = output.find(statsPatternSent);
    rsyncStats += output.substr(searchPos);
        size_t file_count = 0;
        while(file_count < files.size())
        {
            std::string rsync_logs("\n--------------------------------");
            rsync_logs.append("\nFile name: " + files[file_count].filename);
            size_t bmc_role =
                (ext_data::RBMC::convertRoleToString(_extDataIfaces->bmcRole())).find_last_of('.');
            rsync_logs.append("\nBMCRole : " + 
                (ext_data::RBMC::convertRoleToString(_extDataIfaces->bmcRole())).substr(bmc_role+1)
            );
           rsync_logs.append("\nTriggered By : " + triggerBy);
           rsync_logs.append("\nTrigger Reason : " + triggerReason);
           rsync_logs.append("\nRetry Count : " + std::to_string(retryCount));
           rsync_logs.append("\nReturn Code : \t" + std::to_string(ret.first));
            rsync_logs.append("\nRsync initiated: " + files[file_count].rsync_time);
            rsync_logs.append("\nModified Time: " + files[file_count].modified_time);
           rsync_logs.append("\nTime elapsed: \t" + elapsedTime);
            rsync_logs.append("\nBytes Transferred for the file: " +
            files[file_count].bytes_t );
            rsync_logs.append("\nBytes Length of the file: " + files[file_count].bytes_l );

           rsync_logs.append(rsyncStats);
           rsync_logs.append("\n--------------------------------\n");
           f << rsync_logs.c_str();
           file_count++;
        }
    
    
    lg2::debug("RSYNC: Logs {RSYNC_OUTPUT}", "RSYNC_OUTPUT", ret.second);
    f.close();

    co_return true;
}

// TODO: This isn't truly an async operation — Need to use popen/posix_spawn to
// run the rsync command asynchronously but it will be handled as part of
// concurrent sync changes.
sdbusplus::async::task<bool>
    // NOLINTNEXTLINE
    Manager::syncData(config::DataSyncConfig& dataSyncCfg,
                      std::string triggerBy, std::string triggerReason,
                      std::string srcPath, std::string destPath,
                      size_t retryCount)
{
    if (_syncBMCDataIface.disable_sync())
    {
        co_return false;
    }

    const auto currentSrcPath = srcPath.empty() ? dataSyncCfg._path.string()
                                                : srcPath;
    const auto currentDestPath =
        destPath.empty()
            ? dataSyncCfg._destPath.value_or(dataSyncCfg._path).string()
            : destPath;

    // On first try only, if this path is already in retry/syncing, skip that
    if (retryCount == 0)
    {
        if (dataSyncCfg._syncInProgressPaths.count(currentSrcPath))
        {
            lg2::debug(
                "Skipping sync for [{SRC}]: a sync is already in progress",
                "SRC", currentSrcPath);
            co_return true;
        }
        // Mark the path as in-progress so subsequent retries know to skip it
        dataSyncCfg._syncInProgressPaths.insert(currentSrcPath);
    }

    const size_t maxAttempts = dataSyncCfg._retry->_retryAttempts;
    const size_t retryIntervalSec =
        dataSyncCfg._retry->_retryIntervalInSec.count();

    using namespace std::string_literals;
    std::string syncCmd{
        "rsync --archive --compress --delete --delete-missing-args --relative "
        "--times --atimes --update --info=stats2 "
        "--out-format='Item:%i File:%f Bytes_T:%b Bytes_L:%l  Modified:%M [%t]'"
        };

    if (srcPath.empty() && dataSyncCfg._excludeList.has_value())
    {
        std::string excludeStr =  frameExcludeString(dataSyncCfg._path,
                                            dataSyncCfg._excludeList.value());
        syncCmd.append(excludeStr);
    }

    // Add source data path
    if (!srcPath.empty())
    {
        syncCmd.append(" "s + srcPath);
    }
    else
    {
        if (dataSyncCfg._includeList.has_value())
        {
            std::string srcPaths;
            for (auto& path : dataSyncCfg._includeList.value())
            {
                srcPaths += " " + path.string();
            }
            lg2::debug("Included Paths: {INC_PATHS} for [{PATH}]", "INC_PATHS", srcPaths, "PATH",
                    dataSyncCfg._path);
            syncCmd.append(srcPaths);
        }
        else
        {
            syncCmd.append(" "s + dataSyncCfg._path.string());
        }
    }

#ifdef UNIT_TEST
    syncCmd.append(" "s);
#else
    static const std::string rsyncdURL(std::format(" rsync://localhost:{}/{}",
            (_extDataIfaces->siblingBmcPos() == 0 ? BMC0_RSYNC_PORT
                                                  : BMC1_RSYNC_PORT),
            RSYNCD_MODULE_NAME));
    syncCmd.append(rsyncdURL);
#endif

    // Add destination data path
    if (!destPath.empty())
    {
        syncCmd.append(destPath);
    }
    else
    {
        //syncCmd.append(dataSyncCfg._destPath.value_or(dataSyncCfg._path).string());
        syncCmd.append(dataSyncCfg._destPath.value_or(fs::path("")).string());
    }

    lg2::debug("Rsync command: {CMD}", "CMD", syncCmd);

    auto syncStartTime = std::chrono::steady_clock::now();
    std::pair<int, std::string> ret;
    /*if (tryWith("system"))
    {
        ret = tryWithSystem(syncCmd);

    }
    else if (tryWith("popen"))
    {
        ret = tryWithPopen(syncCmd);
    }
    else if (tryWith("popen_nonblock"))
    {
        ret = co_await tryWithPopenNonBlock(syncCmd);
    }
    else if (tryWith("fork"))
    {
        ret = co_await tryWithFork(syncCmd);
    }
    else if (tryWith("vfork"))
    {
        //co_return co_await tryWithVFork(syncCmd);
        auto result = tryWithVFork(syncCmd);
        if (result.first == -1 && result.second == -1)
        {
            ret = std::make_pair(-1, "");
        }
        else
        {
            std::string output;
            bool waitSuccess = true;

            try
            {
                output = co_await waitForCmdCompletion(result.second);
            }
            catch (const std::exception& ex)
            {
                lg2::error("waitForCmdCompletion threw an exception: {ERR}", "ERR", ex.what());
                waitSuccess = false;
            }

            close(result.second);

            // Always wait for child
            int status = -1;
            if (waitpid(result.first, &status, 0) == -1)
            {
                lg2::error("waitpid failed: {ERR}", "ERR", strerror(errno));
                ret = std::make_pair(-1, output);
            }
            else
            {
                if (!waitSuccess)
                {
                    ret = std::make_pair(-1, output);
                }
                else
                {
                    int exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
                    if (exitCode != 0)
                    {
                        lg2::error("Sync is failed, [{CMD}] ExitCode[{EC}] Output[{OUT}]",
                                "CMD", syncCmd, "EC", exitCode, "OUT", output);
                    }
                    ret = std::make_pair(exitCode, output);
                }
            }
        }
    }
    else if (tryWith("posix_spawn"))
    {*/
        ret = co_await tryWithPosiSpawn(syncCmd);
    /*}
    else
    {
        lg2::error("No system call mentioned");
        ret = std::make_pair(-1, "");
    }*/

    auto syncEndTime = std::chrono::steady_clock::now();
    auto syncElapsedTime = std::chrono::duration_cast<std::chrono::nanoseconds>(
            syncEndTime - syncStartTime);
    lg2::debug("Elapsed time for sync: [{DURATION_SECONDS}] seconds for {CMD}",
              "DURATION_SECONDS", syncElapsedTime.count(), "CMD", syncCmd);

    _ctx.spawn( writeStats(
    triggerBy, triggerReason, retryCount,
    std::to_string(syncElapsedTime.count()),
    ret) | stdexec::then([] ([[maybe_unused]] bool stats_result) {}));
    // On success, clear in-progress and return
    if (ret.first == 0)
    {
        dataSyncCfg._syncInProgressPaths.erase(currentSrcPath);
        co_return true;
    }

    if (retryCount < maxAttempts)
    {
        // TODO Retry
        // For now, just handle below rsync error codes
        // "24 - Partial transfer due to vanished source files"
        auto retrySrcPath = currentSrcPath;
        if (ret.first == 24)
        {
            retrySrcPath = getVanishedSrcPath(ret.second);
            lg2::warning("Retry Sync with vanished paths: [{VANISHED_SRCS}]",
                         "VANISHED_SRCS", retrySrcPath);
        }

        lg2::warning(
            "Retrying sync attempt {RETRY_COUNT}/{MAX_ATTEMPTS} after waiting {INTERVAL}s (exit code {ERROR_CODE}): [{SRC}] → [{DEST}]",
            "RETRY_COUNT", retryCount + 1, 
            "MAX_ATTEMPTS", maxAttempts,
            "INTERVAL", retryIntervalSec, 
            "ERROR_CODE", ret.first,
            "SRC", retrySrcPath, 
            "DEST", currentDestPath);

        co_await sleep_for(_ctx, std::chrono::seconds(retryIntervalSec));

        // Recheck vanished path after retry interval
        if (ret.first == 24 && fs::exists(currentSrcPath))
        {
            lg2::debug(
                "Vanish recovery: path [{SRC}] is now present retrying sync on this path",
                "SRC", currentSrcPath);
            retrySrcPath = currentSrcPath;
        }

        co_return co_await syncData(dataSyncCfg,"RETRY", std::to_string(ret.first) ,  
        retrySrcPath, destPath, retryCount + 1);
    }

    // TODO: Create error log entry for sync failure after retries.
    lg2::error("Sync failed after {MAX_ATTEMPTS} attempts (exit code {ERROR_CODE}): [{SRC}] → [{DEST}]",
               "MAX_ATTEMPTS", maxAttempts, 
               "ERROR_CODE", ret.first,
               "SRC", currentSrcPath, 
               "DEST", currentDestPath);

    dataSyncCfg._syncInProgressPaths.erase(currentSrcPath);
    co_return false;

}
sdbusplus::async::task<>
    // NOLINTNEXTLINE
    Manager::monitorDataToSync(config::DataSyncConfig& dataSyncCfg)
{
    try
    {
        uint32_t eventMasksToWatch = IN_CLOSE_WRITE | IN_MOVED_FROM |
                                        IN_MOVED_TO | IN_DELETE_SELF;
        if (dataSyncCfg._isPathDir)
        {
            eventMasksToWatch |= IN_CREATE | IN_DELETE ;
        }

        // Create watcher for the dataSyncCfg._path
        watch::inotify::DataWatcher dataWatcher(
            _ctx, IN_NONBLOCK, eventMasksToWatch, dataSyncCfg._path,
            dataSyncCfg._includeList, dataSyncCfg._excludeList);

        _noOfWatchers += dataWatcher.noOfWatch();

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
                for ([[maybe_unused]] const auto& dataOp : dataOperations)
                {
                    
                    _ctx.spawn(syncData(dataSyncCfg,"IMMEDIATE" ,
                    dataOp.second == data_sync::watch::inotify::DataOps::COPY ? "COPY" : "DEL",
                    dataOp.first.string()) | stdexec::then([] ([[maybe_unused]] bool result) {}));
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
    Manager::monitorTimerToSync(config::DataSyncConfig& dataSyncCfg)
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
        lg2::debug("Periodic timer [{INTERVAL}s] is expired for {PATH}",
                   "INTERVAL", dataSyncCfg._periodicityInSec.value().count(),
                   "PATH", dataSyncCfg._path);
        co_await syncData(dataSyncCfg, "PERIODIC", "INTERVAL");
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

// NOLINTNEXTLINE
sdbusplus::async::task<void> Manager::startFullSync()
{
    _syncBMCDataIface.full_sync_status(FullSyncStatus::FullSyncInProgress);
    lg2::info("Full Sync started");

    auto fullSyncStartTime = std::chrono::steady_clock::now();

    /*if (tryWith("full_sync_single_rsync"))
    {
        using namespace std::string_literals;
        std::string srcPaths;
        std::string excludePaths;
        for (const auto& cfg : _dataSyncConfiguration)
        {
            if (!isSyncEligible(cfg))
            {
                continue;
            }

            if (cfg._includeList.has_value())
            {
                std::string includePaths;
                for (auto& path : cfg._includeList.value())
                {
                    includePaths += path.string() + " ";
                }
                lg2::debug("Included Paths: {INC_PATHS} for [{PATH}]", "INC_PATHS", includePaths, "PATH",
                           cfg._path);
                srcPaths += includePaths;
            }
            else
            {
                srcPaths += " " + cfg._path.string();
            }

            if (cfg._excludeList.has_value())
            {
                excludePaths += frameExcludeString(cfg._path, cfg._excludeList.value());
            }
        }

        std::string sPaths(excludePaths);
        if (tryWith("files-from_opt"))
        {
            sPaths = " --recursive --files-from=/etc/phosphor-data-sync/rsync_paths.lsv /";
        }
        else
        {
            sPaths = excludePaths + " " + srcPaths;
        }

        fullSyncStartTime = std::chrono::steady_clock::now();
        auto ret  = co_await syncData(*(_dataSyncConfiguration.begin()), sPaths,
                "/var/lib/phosphor-data-sync/bmc_data_bkp/");
        if (ret)
        {
            _syncBMCDataIface.full_sync_status(FullSyncStatus::FullSyncCompleted);
            setSyncEventsHealth(SyncEventsHealth::Ok);
            lg2::info("Full Sync completed successfully");
        }
        else
        {
            lg2::error("Full Sync is failed");
            _syncBMCDataIface.full_sync_status(FullSyncStatus::FullSyncFailed);
        }

        auto fullSyncEndTime = std::chrono::steady_clock::now();
        auto FullsyncElapsedTime = std::chrono::duration_cast<std::chrono::seconds>(
                fullSyncEndTime - fullSyncStartTime);

        // total duration/time diff of the Full Sync operation
        lg2::info("Elapsed time for full sync: [{DURATION_SECONDS}] seconds",
                "DURATION_SECONDS", FullsyncElapsedTime.count());
    }
    else
    {*/
        auto syncResults = std::vector<bool>();
        size_t spawnedTasks = 0;

        for (auto& cfg : _dataSyncConfiguration)
        {
            // TODO: add receiver logic to stop fullsync when disable sync is set to
            // true.
            if (isSyncEligible(cfg))
            {
                _ctx.spawn(
                        syncData(cfg, "FULL SYNC" , "REBOOT") |
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
            _syncBMCDataIface.full_sync_status(FullSyncStatus::FullSyncCompleted);
            setSyncEventsHealth(SyncEventsHealth::Ok);
            lg2::info("Full Sync completed successfully");
        }
        else
        {
            // Forcefully marking full sync as successful, even if data syncing
            // fails.
            // TODO: Revert this workaround once the proper logic is implemented
            //_syncBMCDataIface.full_sync_status(FullSyncStatus::FullSyncCompleted);
            //setSyncEventsHealth(SyncEventsHealth::Ok);
            //lg2::info("Full Sync passed temporarily despite sync failures");

            _syncBMCDataIface.full_sync_status(FullSyncStatus::FullSyncFailed);
            lg2::info("Full Sync failed");
        }

        // total duration/time diff of the Full Sync operation
        lg2::info("Elapsed time for full sync: [{DURATION_SECONDS}] seconds",
                "DURATION_SECONDS", FullsyncElapsedTime.count());
// }
    co_return;
}

} // namespace data_sync
