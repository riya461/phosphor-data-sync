// SPDX-License-Identifier: Apache-2.0

#include "config.h"

#include "notify_sibling.hpp"

#include "utility.hpp"

#include <nlohmann/json.hpp>
#include <phosphor-logging/lg2.hpp>

#include <cstdlib>
#include <fstream>
#include <iostream>

namespace data_sync::notify
{
namespace file_operations
{
fs::path writeToFile(const auto& jsonData)
{
    if (!fs::exists(NOTIFY_SIBLING_DIR))
    {
        fs::create_directories(NOTIFY_SIBLING_DIR);
    }

    // File name template : notifyReq_<TIMESTAMP>_<RANDOM-6-CHAR>.json
    std::string pathTemplate =
        fs::path(NOTIFY_SIBLING_DIR) /
        ("notifyReq_" + std::to_string(std::time(nullptr)) + "_XXXXXX.json");
    std::vector<char> filePathBuf(pathTemplate.begin(), pathTemplate.end());
    filePathBuf.push_back('\0');

    data_sync::utility::FD notifyFileFd(mkstemps(filePathBuf.data(), 5));
    if (notifyFileFd() == -1)
    {
        throw std::runtime_error("Failed to create the notify request file");
    }

    // Copy the generated temp file name back to notifyFilePath
    fs::path notifyFilePath{filePathBuf.data()};

    try
    {
        std::string jsonDataStr = jsonData.dump(4);
        ssize_t writtenBytes = write(notifyFileFd(), jsonDataStr.data(),
                                     jsonDataStr.size());
        if (writtenBytes != static_cast<ssize_t>(jsonDataStr.size()))
        {
            throw std::runtime_error("Failed to write the sibling notify json "
                                     "into " +
                                     notifyFilePath.string());
        }

        return notifyFilePath;
    }
    catch (const std::exception& e)
    {
        throw std::runtime_error("Failed to write the notify request to the "
                                 "file, error : " +
                                 std::string(e.what()));
    }
}
} // namespace file_operations

NotifySibling::NotifySibling(const config::DataSyncConfig& dataSyncConfig,
                             const fs::path& modifiedDataPath)
{
    try
    {
        const fs::path& modifiedPath =
            modifiedDataPath.empty() ? dataSyncConfig._path : modifiedDataPath;

        nlohmann::json notifyInfoJson = frameNotifyReq(dataSyncConfig,
                                                       modifiedPath);
        _notifyInfoFile = file_operations::writeToFile(notifyInfoJson);

        lg2::debug(
            "Notify request [{REQFILE}] created for configured path{PATH}",
            "REQFILE", _notifyInfoFile, "PATH", dataSyncConfig._path);
    }
    catch (std::exception& e)
    {
        throw std::runtime_error(
            "Creation of sibling notification request failed!!! for [" +
            dataSyncConfig._path.string() + "]");
    }
}

fs::path NotifySibling::getNotifyFilePath() const
{
    return _notifyInfoFile;
}

nlohmann::json
    NotifySibling::frameNotifyReq(const config::DataSyncConfig& dataSyncConfig,
                                  const fs::path& modifiedDataPath)
{
    try
    {
        return nlohmann::json::object(
            {{"ModifiedDataPath", modifiedDataPath},
             {"NotifyInfo",
              dataSyncConfig._notifySibling.value()._notifyReqInfo}});
    }
    catch (const std::exception& e)
    {
        throw std::runtime_error(
            "Failed to frame the notify request JSON for path: " +
            dataSyncConfig._path.string() + ", error: " + e.what());
    }
}

} // namespace data_sync::notify
