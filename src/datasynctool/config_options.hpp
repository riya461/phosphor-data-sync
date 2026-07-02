// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <sdbusplus/async.hpp>

#include <string>

namespace datasynctool::config_options
{

/**
 * @brief List config paths filtered by BMC role and sync direction
 *
 * @param[in] ctx - Async context
 * @param[in] jsonOutput - Output in JSON format if true
 *
 * @return async task
 */
sdbusplus::async::task<> listConfigPaths(sdbusplus::async::context& ctx,
                                         bool jsonOutput);

/**
 * @brief Get configuration for a specific path
 *
 * @param[in] ctx - Async context
 * @param[in] targetPath - Path to get configuration for
 * @param[in] jsonOutput - Output in JSON format if true
 *
 * @return async task
 */
sdbusplus::async::task<> getPathConfig(sdbusplus::async::context& ctx,
                                       const std::string& targetPath,
                                       bool jsonOutput);

/**
 * @brief List actively watched paths or check if a specific path is watched
 *
 * Sends SIGUSR1 signal to the phosphor-data-sync daemon, which triggers
 * it to dump currently watched paths to a file. Then reads and displays
 * the file contents.
 *
 * If targetPath is provided, checks if that specific path is being watched.
 * If targetPath is empty, lists all watched paths.
 *
 * @param[in] targetPath - Specific path to check (empty = list all)
 * @param[in] jsonOutput - Output in JSON format if true
 *
 * @return async task
 */
sdbusplus::async::task<> listWatchingPaths(sdbusplus::async::context& ctx,
                                           const std::string& targetPath,
                                           bool jsonOutput);

} // namespace datasynctool::config_options
