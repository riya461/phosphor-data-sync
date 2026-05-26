// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <nlohmann/json.hpp>

#include <string>

namespace datasynctool::utils
{

using json = nlohmann::ordered_json;

/**
 * @brief Extract the enum value from the full D-Bus string
 *
 * @param[in] dbusValue - Full D-Bus string value
 *
 * @return std::string - The last part of the D-Bus string
 *
 * Example:
 * "xyz.openbmc_project.Control.SyncBMCData.FullSyncStatus.FullSyncCompleted"
 *          returns "FullSyncCompleted"
 */
std::string extractEnumValue(const std::string& dbusValue);

/**
 * @brief Normalize a path by removing trailing slashes
 *        Treats /a/b/c and /a/b/c/ as equal
 *
 * @param[in] - path : File system path to normalise
 */
std::string normalizePath(const std::string& path);

/**
 * @brief Print a parameter in text format based on the given key and value
 *
 * @param[in] key - Parameter name
 * @param[in] value - Parameter value
 */
template <typename T>
void printParam(std::string key, const T& value);

/**
 * @brief Display JSON data in human-readable text format
 *
 * Handles top-level objects with 1 level of nesting:
 * - Top-level key-value pairs (simple types)
 * - Top-level arrays of strings
 * - Top-level arrays of objects (with nested arrays displayed on separate
 * lines)
 *
 * @param[in] data - JSON object to display
 */
void displayJsonAsText(const json& data);

} // namespace datasynctool::utils
