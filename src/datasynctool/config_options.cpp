// SPDX-License-Identifier: Apache-2.0

#include "config.h"

#include "config_options.hpp"

#include "dbus_interactions.hpp"
#include "utils.hpp"

#include <nlohmann/json.hpp>
#include <xyz/openbmc_project/State/BMC/Redundancy/client.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <print>
#include <string>
#include <vector>

namespace datasynctool::config_options
{

using json = nlohmann::ordered_json;

namespace fs = std::filesystem;

// Helper: Check if path should be included based on role and sync direction
static bool shouldIncludePath(const std::string& syncDir,
                              const std::string& role)
{
    if (role == "Active")
    {
        return (syncDir == "Active2Passive" || syncDir == "Bidirectional");
    }
    else if (role == "Passive")
    {
        return (syncDir == "Passive2Active" || syncDir == "Bidirectional");
    }
    return false;
}

// Helper: Process all entries in a JSON array
static json processEntries(const json& entries, const std::string& role)
{
    json result = json::array();
    for (const auto& entry : entries)
    {
        if (!entry.contains("SyncDirection") || !entry.contains("Path"))
        {
            continue;
        }

        if (!shouldIncludePath(entry["SyncDirection"], role))
        {
            continue;
        }

        json item;
        item["Path"] = entry["Path"];

        if (entry.contains("ExcludeList"))
        {
            item["ExcludeList"] = entry["ExcludeList"];
        }
        if (entry.contains("IncludeList"))
        {
            item["IncludeList"] = entry["IncludeList"];
        }

        if (!item.is_null())
        {
            result.push_back(item);
        }
    }
    return result;
}

// Helper: Parse a single config file
static void parseConfigFile(const fs::path& filePath, const std::string& role,
                            json& files, json& directories)
{
    try
    {
        std::ifstream configFile(filePath);
        if (!configFile.is_open())
        {
            std::cerr << "Failed to open: " << filePath << "\n";
            return;
        }

        json config = json::parse(configFile);

        if (config.contains("Files") && config["Files"].is_array())
        {
            auto processed = processEntries(config["Files"], role);
            files.insert(files.end(), processed.begin(), processed.end());
        }

        if (config.contains("Directories") && config["Directories"].is_array())
        {
            auto processed = processEntries(config["Directories"], role);
            directories.insert(directories.end(), processed.begin(),
                               processed.end());
        }
    }
    catch (const json::exception& e)
    {
        std::cerr << "JSON parse error in " << filePath << ": " << e.what()
                  << "\n";
    }
}

sdbusplus::async::task<> listConfigPaths(sdbusplus::async::context& ctx,
                                         bool jsonOutput)
{
    try
    {
        auto role = co_await dbus_interactions::getBMCRole(ctx);
        const fs::path configDir = DATA_SYNC_CONFIG_DIR;

        if (!fs::exists(configDir) || !fs::is_directory(configDir))
        {
            std::cerr << "Config directory not found: " << configDir << "\n";
            co_return;
        }

        json files = json::array();
        json directories = json::array();

        // Parse all JSON config files
        for (const auto& entry : fs::directory_iterator(configDir))
        {
            if (entry.is_regular_file() && entry.path().extension() == ".json")
            {
                parseConfigFile(entry.path(), role, files, directories);
            }
        }

        // Build and display output
        json output;
        output["Files"] = files;
        output["Directories"] = directories;

        if (jsonOutput)
        {
            std::println("{}", output.dump(4));
        }
        else
        {
            utils::displayJsonAsText(output);
        }

        co_return;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error listing config paths: " << e.what() << "\n";
        throw;
    }
}

} // namespace datasynctool::config_options