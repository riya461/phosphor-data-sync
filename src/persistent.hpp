// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <nlohmann/json.hpp>

#include <filesystem>
#include <optional>

namespace data_sync::persist
{

extern std::filesystem::path DBusPropDataFile;

namespace key
{
constexpr auto disable = "Disable";
constexpr auto fullSyncStatus = "FullSyncStatus";
constexpr auto syncEventsHealth = "SyncEventsHealth";
} // namespace key

namespace util
{

/**
 * @brief Helper function to update a JSON file
 *
 * @param[in] json - The JSON to update
 * @param[in] path - The path to the file
 */
void writeFile(const nlohmann::json& json, const std::filesystem::path& path);

} // namespace util

/**
 * @brief Function to read a JSON file
 *
 * @param[in] path - The path to the file
 *
 * @return optional<json> - The JSON data, or std::nullopt if it
 *                          didn't exist or was corrupt.
 */
std::optional<nlohmann::json> readFile(const std::filesystem::path& path);

/**
 * @brief Updates "name": <value>  JSON to the file specified
 *
 * @tparam - The data type
 * @param[in] name - The key to save the value under
 * @param[in] value - The value to save
 */
template <typename T>
void update(std::string_view name, const T& value,
            const std::filesystem::path& path = DBusPropDataFile)
{
    auto json = readFile(path).value_or(nlohmann::json::object());
    if constexpr (std::is_enum_v<T>)
    {
        json[name] = std::to_underlying(value);
    }
    else
    {
        json[name] = value;
    }
    util::writeFile(json, path);
}

/**
 * @brief Reads the value of the key specified in the file specified
 *        Specifically, for unit testing purposes.
 *
 * @tparam T - The data type
 * @param[in] name - The key the value is saved under
 * @param[in] path - The path to the file
 *
 * @return optional<T> - The value, or std::nullopt if the file or
 *                       key isn't present.
 */
template <typename T>
std::optional<T> read(std::string_view name,
                      const std::filesystem::path& path = DBusPropDataFile)
{
    auto json = readFile(path);
    if (!json)
    {
        return std::nullopt;
    }

    auto it = json->find(name);
    if (it != json->end())
    {
        if constexpr (std::is_enum_v<T>)
        {
            auto value = it->get<std::underlying_type_t<T>>();
            return static_cast<T>(value);
        }
        else
        {
            return it->get<T>();
        }
    }

    return std::nullopt;
}

} // namespace data_sync::persist
