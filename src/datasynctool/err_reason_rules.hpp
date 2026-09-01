// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <map>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

namespace datasynctool::error_summary
{

namespace trace_patterns
{

constexpr std::string_view connectionRefused = "Connection refused (111)";
constexpr std::string_view mtlsHandshake = "::reason(134)";

} // namespace trace_patterns

namespace reason
{

constexpr std::string_view connectionRefused =
    "Connection to sibling BMC is refused";
constexpr std::string_view mtlsHandshakeFailed = "mTLS handshake failed";

} // namespace reason

namespace cause
{

// Connection refused causes
constexpr std::string_view cause1 = "PeerConnected might be false";
constexpr std::string_view cause2 =
    "stunnel/rsync service might not be running on sibling";

// mTLS handshake failure causes
constexpr std::string_view cause3 = "System time is out of sync";
constexpr std::string_view cause4 =
    "Certificate, key or CA mismatch between BMCs";

} // namespace cause

namespace verify
{

// Connection refused verify steps
constexpr std::string_view verify1 = "run datasynctool --status on both BMCs";

// mTLS handshake failure verify steps
constexpr std::string_view verify2 =
    "Verify system time matches certificate validity period";
constexpr std::string_view verify3 =
    "Verify certificates are not expired or corrupted";

} // namespace verify

/**
 * @brief Holds the reason, possible causes, and verify steps for a matched
 *        trace pattern.
 */
struct ErrReason
{
    std::string_view reason;
    std::vector<std::string_view> causes;
    std::vector<std::string_view> verify;
};

// ── Error reason rules
// ─────────────────────────────────────────────────────────────────
//
// Flat map : trace pattern -> ErrReason { reason, causes[], verify[] }.
//
// To add a new pattern : declare constexpr pattern/reason/causes/verify
//                        above, then add one entry here.

static const std::map<std::string_view, ErrReason> errReasonRules = {
    {trace_patterns::connectionRefused,
     {reason::connectionRefused,
      {cause::cause1, cause::cause2},
      {verify::verify1}}},
    {trace_patterns::mtlsHandshake,
     {reason::mtlsHandshakeFailed,
      {cause::cause3, cause::cause4},
      {verify::verify2, verify::verify3}}}};

/**
 * @brief Derive error reason and possible causes by scanning trace lines.
 *
 * Scans traceLines for the first pattern matching an entry in errReasonRules.
 * Returns nullopt when traceLines is empty. Returns an ErrReason with empty
 * causes and verify steps, and "Unknown reason, check traces" when no
 * pattern matched.
 *
 * @param[in] traceLines - trace lines from PEL User Data sections
 *
 * @return std::optional<ErrReason> - matched reason+causes, or nullopt if
 *                                    no traces present
 */
inline std::optional<ErrReason>
    deriveErrorReason(const std::vector<std::string>& traceLines)
{
    if (traceLines.empty())
    {
        return std::nullopt;
    }

    for (const auto& line : traceLines | std::views::reverse)
    {
        const auto it = std::ranges::find_if(errReasonRules,
                                             [&line](const auto& rule) {
            return line.find(rule.first) != std::string::npos;
        });

        if (it != errReasonRules.end())
        {
            return it->second;
        }
    }

    return ErrReason{"Unknown reason, check traces", {}, {}};
}

} // namespace datasynctool::error_summary
