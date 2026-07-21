// SPDX-License-Identifier: Apache-2.0

#include "error_summary.hpp"

#include <sys/wait.h>

#include <nlohmann/json.hpp>

#include <array>
#include <cerrno>
#include <cstdio>
#include <format>
#include <map>
#include <optional>
#include <print>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace datasynctool::error_summary
{

using json = nlohmann::ordered_json;

// ── Constants
// ─────────────────────────────────────────────────────────────────

constexpr std::string_view datasyncSrcPrefix = "BD8D70";

constexpr std::string_view separator =
    "─────────────────────────────────────────────────";

// Read buffer for popen output
constexpr std::size_t readBufSize = 4096;

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
static constexpr PelField fieldErrMsg = {"User Data 1", "DS_Sync_ErrMsg",
                                         "ErrMsg"};
static constexpr PelField fieldErrCode = {"User Data 1", "DS_Sync_ErrCode",
                                          "ErrCode"};

static std::optional<std::string> runCommand(std::string_view cmd)
{
    // NOLINTNEXTLINE
    FILE* pipe = popen(std::string(cmd).c_str(), "r");
    if (pipe == nullptr)
    {
        std::println(stderr, "popen failed for command: {}", cmd);
        return std::nullopt;
    }

    std::string output;
    output.reserve(readBufSize);
    std::array<char, readBufSize> buf{};
    std::size_t n = 0;
    while ((n = std::fread(buf.data(), 1, buf.size(), pipe)) > 0)
    {
        const auto chunk = std::span(buf).first(n);
        output.append(chunk.begin(), chunk.end());
    }

    const int status = pclose(pipe);
    if (status == -1)
    {
        std::println(stderr, "pclose failed: {}",
                     std::system_category().message(errno));
    }
    else if (WIFEXITED(status) && WEXITSTATUS(status) != 0)
    {
        std::println(stderr, "peltool.py exited with code {}",
                     WEXITSTATUS(status));
    }

    return output;
}

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

// Extract trace lines from "User Data 2" and "User Data 3" "Data" arrays.
static std::vector<std::string> extractTraceLines(const json& pelData)
{
    std::vector<std::string> lines;

    for (const std::string_view section : {"User Data 2", "User Data 3"})
    {
        const auto sectionIt = pelData.find(section);
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
        .errMsg = {},
        .errCode = {},
        .traceLines = {},
    };

    if (regMsg == "SyncFailure")
    {
        entry.path = std::string(extractField(pelData, fieldPath));
        entry.errMsg = std::string(extractField(pelData, fieldErrMsg));
        entry.errCode = std::string(extractField(pelData, fieldErrCode));
    }

    if (includeTrace)
    {
        entry.traceLines = extractTraceLines(pelData);
    }

    return entry;
}

sdbusplus::async::task<> displayErrorLogSummary(bool jsonOutput,
                                                std::size_t limit,
                                                bool includeTrace)
{
    const auto output = runCommand(
        std::format("peltool.py --src {} -r {} -a", datasyncSrcPrefix, limit));

    if (!output.has_value() || output->empty())
    {
        std::println("peltool.py returned no output for SRC prefix {}",
                     datasyncSrcPrefix);
        co_return;
    }

    json pelMap;
    try
    {
        pelMap = json::parse(*output);
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
            if (!e.path.empty() || !e.errMsg.empty() || !e.errCode.empty())
            {
                obj[fieldPath.displayName] = e.path;
                obj[fieldErrMsg.displayName] = e.errMsg;
                obj[fieldErrCode.displayName] = e.errCode;
            }
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
        if (!e.path.empty() || !e.errMsg.empty() || !e.errCode.empty())
        {
            std::println("  {:<18}: {}", fieldPath.displayName, e.path);
            std::println("  {:<18}: {}", fieldErrMsg.displayName, e.errMsg);
            std::println("  {:<18}: {}", fieldErrCode.displayName, e.errCode);
        }
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

} // namespace datasynctool::error_summary
