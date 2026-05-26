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
#include <optional>
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
        // NOLINTNEXTLINE(clang-analyzer-core.uninitialized.Branch)
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

// Helper function to search for a path (exact or parent) in a JSON array
static std::optional<json> findPathInArray(const json& array,
                                           const std::string& normalizedTarget)
{
    if (!array.is_array())
    {
        return std::nullopt;
    }

    for (const auto& jsonEntry : array)
    {
        if (jsonEntry.contains("Path"))
        {
            std::string configPath = jsonEntry["Path"].get<std::string>();
            std::string normalizedConfig = utils::normalizePath(configPath);

            // Check exact match OR if target is child of config directory
            // (target starts with config path means it's a child)
            if (normalizedTarget == normalizedConfig ||
                normalizedTarget.find(normalizedConfig) == 0)
            {
                return jsonEntry;
            }
        }
    }
    return std::nullopt;
}

sdbusplus::async::task<> getPathConfig(sdbusplus::async::context& ctx,
                                       const std::string& targetPath,
                                       bool jsonOutput)
{
    namespace fs = std::filesystem;

    try
    {
        // False positive from clang static analyzer with coroutine
        // NOLINTNEXTLINE(clang-analyzer-core.uninitialized.Branch)
        auto role = co_await dbus_interactions::getBMCRole(ctx);

        const fs::path configDir = DATA_SYNC_CONFIG_DIR;

        if (!fs::exists(configDir) || !fs::is_directory(configDir))
        {
            std::cerr << "Config directory not found: " << configDir << "\n";
            co_return;
        }

        // Normalize target path once
        std::string normalizedTarget = utils::normalizePath(targetPath);

        // Search through all JSON config files
        for (const auto& entry : fs::directory_iterator(configDir))
        {
            if (!entry.is_regular_file() || entry.path().extension() != ".json")
            {
                continue;
            }

            std::ifstream configFile(entry.path());
            if (!configFile.is_open())
            {
                std::cerr << "Failed to open: " << entry.path() << "\n";
                continue;
            }

            try
            {
                json config = json::parse(configFile);

                auto matchedConfig = findPathInArray(config["Files"],
                                                     normalizedTarget);
                if (!matchedConfig)
                {
                    matchedConfig = findPathInArray(config["Directories"],
                                                    normalizedTarget);
                }

                if (matchedConfig)
                {
                    // Add default retry values if not present
                    if (!matchedConfig->contains("RetryAttempts"))
                    {
                        (*matchedConfig)["RetryAttempts"] =
                            DEFAULT_RETRY_ATTEMPTS;
                    }
                    if (!matchedConfig->contains("RetryInterval"))
                    {
                        (*matchedConfig)["RetryInterval"] =
                            std::to_string(DEFAULT_RETRY_INTERVAL) + " seconds";
                    }

                    // Add BMC Role to output
                    (*matchedConfig)["BMC Role"] = role;

                    // Append config file name
                    (*matchedConfig)["Config File"] = entry.path().string();

                    // Output the configuration
                    if (jsonOutput)
                    {
                        std::println("{}", matchedConfig->dump(4));
                    }
                    else
                    {
                        utils::displayJsonAsText(*matchedConfig);
                    }
                    co_return;
                }
            }
            catch (const json::exception& e)
            {
                std::cerr << "JSON parse error in " << entry.path() << ": "
                          << e.what() << "\n";
                continue;
            }
        }

        std::cerr << "Error: Path '" << targetPath
                  << "' not found in configuration\n";
        co_return;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error getting path config: " << e.what() << "\n";
        throw;
    }
}

} // namespace datasynctool::config_options
