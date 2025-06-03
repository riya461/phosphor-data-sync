// SPDX-License-Identifier: Apache-2.0

#include "utility.hpp"

#include <unistd.h>
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

} // namespace data_sync::utility
