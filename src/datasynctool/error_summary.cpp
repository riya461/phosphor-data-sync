// SPDX-License-Identifier: Apache-2.0

#include "error_summary.hpp"

#include "err_reason_rules.hpp"
#include "utils.hpp"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <format>
#include <map>
#include <optional>
#include <print>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

namespace datasynctool::error_summary
{

using json = nlohmann::ordered_json;

// ── Constants
// ─────────────────────────────────────────────────────────────────

constexpr std::string_view datasyncSrcPrefix = "BD8D70";

constexpr std::string_view separator =
    "─────────────────────────────────────────────────";

// Registry message map
static const std::map<std::string_view, std::string_view> srcRegistryMap = {
    {"BD8D7001", "SyncFailure"},
    {"BD8D7002", "SyncEventsFailure"},
    {"BD8D7003", "ParserFailure"},
    {"BD8D7004", "NotifyFailure"},
};

// Field descriptors
static constexpr PelField fieldRegistryMsg = {"Primary SRC", "Reference Code",
                                              "Registry Message"};

static constexpr PelField fieldFailureTime = {"Private Header", "Committed at",
                                              "Failure Time"};

static constexpr PelField fieldPelId = {"Private Header", "Platform Log Id",
                                        "PEL ID"};

// SyncFailure - only fields
static constexpr PelField fieldPath = {"User Data 1", "DS_Sync_Path", "Path"};
static constexpr PelField fieldRsyncErrMsg = {"User Data 1", "DS_Sync_ErrMsg",
                                              "RsyncErrMsg"};
static constexpr PelField fieldRsyncErrCode = {"User Data 1", "DS_Sync_ErrCode",
                                               "RsyncErrCode"};

// Extract a string value from a PEL JSON object using a PelField descriptor.
// Returns an empty string if the parent section or child key is absent.
static std::string_view extractField(const json& pelData, const PelField& field)
{
    const auto parentIt = pelData.find(field.parent);
    if (parentIt == pelData.end() || !parentIt->is_object())
    {
        return {};
    }
    const auto childIt = parentIt->find(field.child);
    return (childIt != parentIt->end() && childIt->is_string())
               ? std::string_view(childIt->get_ref<const std::string&>())
               : std::string_view{};
}

// Extract trace lines from PEL User Data sections.
// If section is provided, only that section is read.
// If section is absent, all known trace sections are read (User Data 2 + 3).
static std::vector<std::string>
    extractTraceLines(const json& pelData,
                      std::optional<std::string_view> section = std::nullopt)
{
    std::vector<std::string> lines;

    const auto sectionsToRead =
        section.has_value()
            ? std::vector<std::string_view>{section.value()}
            : std::vector<std::string_view>{"User Data 2", "User Data 3"};

    for (const std::string_view sec : sectionsToRead)
    {
        const auto sectionIt = pelData.find(std::string(sec));
        if (sectionIt == pelData.end() || !sectionIt->is_object())
        {
            continue;
        }

        const auto dataIt = sectionIt->find("Data");
        if (dataIt == sectionIt->end() || !dataIt->is_array())
        {
            continue;
        }

        for (const auto& item : *dataIt)
        {
            if (item.is_string())
            {
                lines.emplace_back(item.get<std::string>());
            }
        }
    }

    return lines;
}

// Build a SummaryEntry from a single PEL JSON object.
static SummaryEntry makeSummaryEntry(const json& pelData, bool includeTrace)
{
    const std::string_view rawSrc = extractField(pelData, fieldRegistryMsg);
    const auto it = srcRegistryMap.find(rawSrc);
    // Resolve the raw SRC reference code to a registry message string.
    // Falls back to the raw code if not found in the map.
    const std::string_view regMsg = it != srcRegistryMap.end() ? it->second
                                                               : rawSrc;

    SummaryEntry entry{
        .registryMsg = std::string(regMsg),
        .failureTime = std::string(extractField(pelData, fieldFailureTime)),
        .pelId = std::string(extractField(pelData, fieldPelId)),
        .path = {},
        .rsyncErrMsg = {},
        .rsyncErrCode = {},
        .errReason = {},
        .errCauses = {},
        .errVerify = {},
        .traceLines = {},
    };

    if (regMsg == "SyncFailure")
    {
        entry.path = std::string(extractField(pelData, fieldPath));
        entry.rsyncErrMsg =
            std::string(extractField(pelData, fieldRsyncErrMsg));
        entry.rsyncErrCode =
            std::string(extractField(pelData, fieldRsyncErrCode));

        // Scan only stunnel traces (User Data 3) for error reason derivation.
        const auto derived =
            deriveErrorReason(extractTraceLines(pelData, "User Data 3"));
        if (derived.has_value())
        {
            entry.errReason = std::string(derived->reason);
            for (const auto& c : derived->causes)
            {
                entry.errCauses.emplace_back(c);
            }
            for (const auto& v : derived->verify)
            {
                entry.errVerify.emplace_back(v);
            }
        }
    }

    if (includeTrace)
    {
        // All trace sections for -T output.
        entry.traceLines = extractTraceLines(pelData);
    }
    return entry;
}

sdbusplus::async::task<> displayErrorLogSummary(bool jsonOutput,
                                                std::size_t limit,
                                                bool includeTrace)
{
    const auto [exitCode, output] = utils::runCommand(std::format(
        "peltool.py --src {} --reverse {} --all-pels --skip-parser-plugins ",
        datasyncSrcPrefix, limit));

    if (exitCode != 0)
    {
        std::println(stderr, "peltool.py exited with code {}", exitCode);
        co_return;
    }

    if (output.empty())
    {
        std::println("peltool.py returned no output for SRC prefix {}",
                     datasyncSrcPrefix);
        co_return;
    }

    json pelMap;
    try
    {
        pelMap = json::parse(output);
    }
    catch (const json::exception& e)
    {
        std::println(stderr, "Failed to parse peltool output: {}", e.what());
        co_return;
    }

    if (!pelMap.is_object())
    {
        std::println(stderr, "Unexpected peltool output format");
        co_return;
    }

    // Collect one SummaryEntry per error log.
    auto entries = pelMap.items() | std::views::filter([](const auto& kv) {
        return kv.value().is_object();
    }) | std::views::transform([includeTrace](const auto& kv) {
        return makeSummaryEntry(kv.value(), includeTrace);
    }) | std::views::filter([](const SummaryEntry& e) {
        return !e.registryMsg.empty() || !e.failureTime.empty() ||
               !e.pelId.empty();
    }) | std::ranges::to<std::vector>();

    if (entries.empty())
    {
        std::println("No DataSync error logs found");
        co_return;
    }

    if (jsonOutput)
    {
        auto out = entries | std::views::transform([](const SummaryEntry& e) {
            json obj;
            obj[fieldRegistryMsg.displayName] = e.registryMsg;
            obj[fieldFailureTime.displayName] = e.failureTime;
            obj[fieldPelId.displayName] = e.pelId;
            obj.update(syncFailureToJson(e));
            if (!e.traceLines.empty())
            {
                obj["Trace"] = e.traceLines;
            }
            return obj;
        }) | std::ranges::to<json>();
        std::println("{}", out.dump(2));
        co_return;
    }

    // Text output
    for (const auto& e : entries)
    {
        std::println("{}", separator);
        std::println("  {:<18}: {}", fieldRegistryMsg.displayName,
                     e.registryMsg);
        std::println("  {:<18}: {}", fieldFailureTime.displayName,
                     e.failureTime);
        std::println("  {:<18}: {}", fieldPelId.displayName, e.pelId);
        printSyncFailureEntry(e);
        if (!e.traceLines.empty())
        {
            std::println("  {:<18}:", "Trace");
            for (const auto& line : e.traceLines)
            {
                std::println("    {}", line);
            }
        }
    }
    std::println("{}", separator);
}

void printSyncFailureEntry(const SummaryEntry& entry)
{
    if (!entry.path.empty())
    {
        std::println("  {:<18}: {}", fieldPath.displayName, entry.path);
    }

    if (!entry.errReason.empty())
    {
        std::println("  {:<18}: {}", "ErrorReason", entry.errReason);
        std::ranges::for_each(entry.errCauses | std::views::enumerate,
                              [](const auto& item) {
            std::println("    {:<16}{} - {}",
                         std::get<0>(item) == 0 ? "PossibleCauses" : "",
                         std::get<0>(item) == 0 ? ":" : " ", std::get<1>(item));
        });
        std::ranges::for_each(entry.errVerify | std::views::enumerate,
                              [](const auto& item) {
            std::println("    {:<16}{} - {}",
                         std::get<0>(item) == 0 ? "Verify" : "",
                         std::get<0>(item) == 0 ? ":" : " ", std::get<1>(item));
        });
    }

    if (!entry.rsyncErrMsg.empty())
    {
        std::println("  {:<18}: {}", fieldRsyncErrMsg.displayName,
                     entry.rsyncErrMsg);
    }
    if (!entry.rsyncErrCode.empty())
    {
        std::println("  {:<18}: {}", fieldRsyncErrCode.displayName,
                     entry.rsyncErrCode);
    }
}

json syncFailureToJson(const SummaryEntry& entry)
{
    json result = json::object();
    if (!entry.path.empty())
    {
        result[fieldPath.displayName] = entry.path;
    }

    if (!entry.errReason.empty())
    {
        result["ErrorReason"] = entry.errReason;
        if (!entry.errCauses.empty())
        {
            result["PossibleCauses"] = entry.errCauses;
        }
        if (!entry.errVerify.empty())
        {
            result["Verify"] = entry.errVerify;
        }
    }

    if (!entry.rsyncErrMsg.empty())
    {
        result[fieldRsyncErrMsg.displayName] = entry.rsyncErrMsg;
    }
    if (!entry.rsyncErrCode.empty())
    {
        result[fieldRsyncErrCode.displayName] = entry.rsyncErrCode;
    }
    return result;
}

} // namespace datasynctool::error_summary
