// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <string>
namespace data_sync::utility
{

/**
 * @class FD
 * @brief RAII wrapper for file descriptor.
 */
class FD
{
  public:
    FD(const FD&) = delete;
    FD& operator=(const FD&) = delete;
    FD(FD&&) = delete;
    FD& operator=(FD&&) = delete;

    /**
     * @brief Constructor
     *
     * Saves the file descriptor and uses it to do file operation
     *
     *  @param[in] fd - File descriptor
     */
    explicit FD(int fd);

    /**
     * @brief Destructor
     *
     * To close the file descriptor once goes out of scope.
     */
    ~FD();

    /**
     * @brief To close the file descriptor manually.
     */
    void reset();

    /**
     * @brief To return the saved file descriptor
     */
    int operator()() const;

  private:
    /**
     * @brief File descriptor
     */
    int fd = -1;
};

namespace rsync
{
/**
 * @brief Extract the numeric value of the transferred file size
 *
 * The function searches the provided rsync log string for the line
 * starting with "Total transferred file size:" and captures its numeric
 * value.
 *
 * @param[in] rsyncOpStr - rsync output string containing the transfer
 *                         summary.
 * @return size_t - numeric value of the transferred size
 *                - Returns 0 if the value is not found
 */
size_t getTransferredBytes(const std::string& rsyncOpStr);

} // namespace rsync
} // namespace data_sync::utility
