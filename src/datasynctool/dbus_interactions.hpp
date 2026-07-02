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
 * @brief Get BMC role from D-Bus
 *
 * @param[in] ctx - Async context
 *
 * @return BMC role string
 */
sdbusplus::async::task<std::string> getBMCRole(sdbusplus::async::context& ctx);

/**
 * @brief Build JSON object from D-Bus properties
 *
 * @param[in] properties - Output of the GetAll method
 *
 * @return json - JSON object containing the D-Bus properties
 */
json buildStatusJson(const PropertyMap& properties);

/**
 * @brief Display D-Bus status properties
 *
 * @param[in] ctx - Async context
 * @param[in] jsonOutput - Output in JSON format if true
 *
 * @return async task
 */
sdbusplus::async::task<> displayStatus(sdbusplus::async::context& ctx,
                                       bool jsonOutput);

/**
 * @brief Start full sync operation
 *
 * @param[in] ctx - Async context
 *
 * @return async task
 */
sdbusplus::async::task<> startFullSync(sdbusplus::async::context& ctx);

/**
 * @brief Set sync enabled/disabled state
 *
 * @param[in] ctx - Async context
 * @param[in] enable - true to enable sync, false to disable
 *
 * @return async task
 */
sdbusplus::async::task<> setSyncEnabled(sdbusplus::async::context& ctx,
                                        bool enable);

/**
 * @brief Get the MainPID of a systemd service via D-Bus
 *
 * Queries org.freedesktop.systemd1 to get the MainPID of the given
 * service unit. Returns 0 if the service is not running.
 *
 * @param[in] ctx - Async context
 * @param[in] serviceName - Systemd service unit name (e.g. "foo.service")
 *
 * @return pid_t - The MainPID of the service, or 0 if not running
 */
sdbusplus::async::task<pid_t> getServiceMainPid(sdbusplus::async::context& ctx,
                                                const std::string& serviceName);

} // namespace datasynctool::dbus_interactions
