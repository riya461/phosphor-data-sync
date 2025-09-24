// SPDX-License-Identifier: Apache-2.0

#include "utility.hpp"

#include <unistd.h>

#include <regex>
namespace data_sync::utility
{

FD::FD(int fd) : fd(fd) {}

FD::~FD()
{
    reset();
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

namespace rsync
{

size_t getTransferredBytes(const std::string& rsyncOpStr)
{
    // Regex to capture the numeric value of "Total transferred file size:"
    std::regex re(
        R"(Total transferred file size:\s*([0-9]+(?:\.[0-9]+)?\s*\w+))");
    std::smatch match;

    if (std::regex_search(rsyncOpStr, match, re))
    {
        return static_cast<size_t>(std::stod(match[1].str()));
    }
    return 0;
}
} // namespace rsync
} // namespace data_sync::utility
