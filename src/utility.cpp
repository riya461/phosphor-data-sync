// SPDX-License-Identifier: Apache-2.0

#include "config.h"

#include "utility.hpp"

#include <unistd.h>

#include <phosphor-logging/lg2.hpp>

#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace data_sync::utility
{

namespace fs = std::filesystem;

FD::FD(int fd) : fd(fd) {}

FD::~FD()
{
    reset();
}

FD::FD(FD&& other) noexcept : fd(std::exchange(other.fd, -1)) {}

FD& FD::operator=(FD&& other) noexcept
{
    if (this != &other)
    {
        reset();
        fd = std::exchange(other.fd, -1);
    }

    return *this;
}

void FD::reset()
{
    if (fd >= 0)
    {
        close(fd);
        fd = -1;
    }
}

int FD::operator()() const
{
    return fd;
}

std::optional<std::chrono::seconds>
    parseDateTimeToEpoch(std::string_view dateTime)
{
    std::tm tm{};
    std::istringstream timestampStream(std::string{dateTime});
    timestampStream >> std::get_time(&tm, "%Y/%m/%d %H:%M:%S");
    if (timestampStream.fail())
    {
        return std::nullopt;
    }

    return std::chrono::seconds{std::mktime(&tm)};
}

std::optional<std::chrono::seconds>
    fileTimeToEpoch(const std::filesystem::file_time_type& fileTime)
{
    auto systemNow = std::chrono::system_clock::now();
    auto fileNow = fs::file_time_type::clock::now();
    auto systemTime =
        std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            fileTime - fileNow + systemNow);
    return std::chrono::seconds{
        std::chrono::system_clock::to_time_t(systemTime)};
}

void setupPaths()
{
    const fs::path persistPath{"/var/lib/phosphor-data-sync/"};
    std::error_code ec;
    // Directory to keep the sibling BMC's data as backup on local BMC
    const fs::path bkpPath{persistPath / "bmc_data_bkp/"};
    if (!fs::exists(bkpPath))
    {
        if (!fs::create_directories(bkpPath, ec))
        {
            lg2::error("Failed to create the path[{PATH}] : Error : {ERROR}",
                       "PATH", bkpPath, "ERROR", ec.message());
            throw std::runtime_error("Failed to create the path: " +
                                     bkpPath.string());
        }
    }

    // Directory where sibling notify requests get created
    const fs::path notifySiblingDir{NOTIFY_SIBLING_DIR};
    if (!fs::exists(notifySiblingDir))
    {
        if (!fs::create_directories(notifySiblingDir, ec))
        {
            lg2::error("Failed to create the path[{PATH}] : Error : {ERROR}",
                       "PATH", notifySiblingDir, "ERROR", ec.message());
            throw std::runtime_error("Failed to create the path: " +
                                     notifySiblingDir.string());
        }
    }

    // Directory which receives the notify requests from sibling BMC
    const fs::path notifyServiceDir{NOTIFY_SERVICES_DIR};
    if (!fs::exists(notifyServiceDir))
    {
        if (!fs::create_directories(notifyServiceDir, ec))
        {
            lg2::error("Failed to create the path[{PATH}] : Error : {ERROR}",
                       "PATH", notifyServiceDir, "ERROR", ec.message());
            throw std::runtime_error("Failed to create the path: " +
                                     notifyServiceDir.string());
        }
    }
}

std::size_t readBMCPosition()
{
    constexpr auto bmcPositionFile = "/run/openbmc/bmc_position";
    std::ifstream posFile(bmcPositionFile);
    if (!posFile.is_open())
    {
        throw std::runtime_error(std::string("Cannot open ") + bmcPositionFile);
    }

    std::size_t position{};
    // max<size_t>() indicates that the BMC position could not be determined.
    if (!(posFile >> position) ||
        position == std::numeric_limits<std::size_t>::max())
    {
        throw std::runtime_error(std::string("Invalid BMC position in ") +
                                 bmcPositionFile);
    }

    return position;
}

namespace rsync
{

bool isSynced(const std::string& rsyncOutput)
{
    // Match either:
    //   - "Literal data:" with a non-zero value (data written to remote)
    //   - "*deleting " line (file deleted on remote, via --itemize-changes)
    std::regex re(R"(Literal data:\s*[1-9]|\*deleting\s+)");
    return std::regex_search(rsyncOutput, re);
}

} // namespace rsync
} // namespace data_sync::utility
