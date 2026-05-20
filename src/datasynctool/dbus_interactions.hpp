// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <nlohmann/json.hpp>
#include <sdbusplus/async.hpp>
#include <sdbusplus/bus.hpp>

#include <map>
#include <string>
#include <variant>

namespace datasynctool::dbus_interactions
{

using json = nlohmann::ordered_json;
using DbusVariant = std::variant<bool, std::string>;
using PropertyMap = std::map<std::string, DbusVariant>;

/**
 * @brief Get all D-Bus properties using GetAll method
 *
 * @param[in] bus - D-Bus connection
 * @param[in] service - D-Bus service name
 * @param[in] path - D-Bus object path
 * @param[in] interface - D-Bus interface name
 *
 * @return PropertyMap - Map of property names to values
 */
PropertyMap getAllProperties(sdbusplus::bus_t& bus, const std::string& service,
                             const std::string& path,
                             const std::string& interface);

/**
 * @brief Build JSON object from D-Bus properties
 *
 * @param[in] properties - Output of the GetAll method
 *
 * @return json - JSON object containing the D-Bus properties
 */
json buildStatusJson(const PropertyMap& properties);

/**
 * @brief Display D-Bus status properties (async version)
 *
 * @param[in] ctx - Async context
 * @param[in] jsonOutput - Output in JSON format if true
 *
 * @return async task
 */
sdbusplus::async::task<> displayStatus(sdbusplus::async::context& ctx,
                                       bool jsonOutput);

} // namespace datasynctool::dbus_interactions
