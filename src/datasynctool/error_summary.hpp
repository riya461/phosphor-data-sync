// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <sdbusplus/async.hpp>

#include <string>
#include <string_view>

namespace datasynctool::error_summary
{

/**
 * @brief Describes a single field to extract from a PEL JSON object.
 *
 * The PEL JSON produced by peltool is a two-level map:
 *   { "<parent>": { "<child>": "<value>" } }
 *
 * @var parent       Top-level section key.
 * @var child        Key inside that section.
 * @var displayName  Label used in output.
 */
struct PelField
{
    std::string_view parent;
    std::string_view child;
    std::string_view displayName;
};

/**
 * @brief The fields collected for every DataSync failure PEL.
 *
 * Core fields (all error types):
 *   - registryMsg  : Primary SRC / Reference Code
 *   - failureTime  : Private Header / Committed at
 *   - pelId        : Private Header / Platform Log Id
 *
 * In SyncFailure :
 *   - path         : User Data 1 / DS_Sync_Path
 *   - errMsg       : User Data 1 / DS_Sync_ErrMsg
 *   - errCode      : User Data 1 / DS_Sync_ErrCode
 */
struct SummaryEntry
{
    std::string registryMsg;
    std::string failureTime;
    std::string pelId;

    // SyncFailure
    std::string path;
    std::string errMsg;
    std::string errCode;
};

/**
 * @brief Print a summary of DataSync error logs.
 *
 * Queries peltool for all PELs whose SRC begins with the DataSync prefix
 * (BD8D70).  For each matching PEL the three fields above are extracted and
 * displayed either as formatted text or JSON.
 *
 * @param[in] jsonOutput Output in JSON format if true
 * @param[in] limit      Maximum number of recent logs to display (default: 1)
 *
 * @return async task
 */
sdbusplus::async::task<> displayErrorLogSummary(bool jsonOutput,
                                                std::size_t limit = 1);

} // namespace datasynctool::error_summary
