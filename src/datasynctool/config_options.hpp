// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <sdbusplus/async.hpp>

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

} // namespace datasynctool::config_options
