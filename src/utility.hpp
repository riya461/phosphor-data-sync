// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

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

    // Move
    FD(FD&& other) noexcept;
    FD& operator=(FD&& other) noexcept;

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

/**
 * @brief Create the necessary persistent paths during startup
 *
 * The API will create the following persistent paths
 *  - /var/lib/phosphor-data-sync/bmc_data_bkp/ :
 *      - To keep the sibling BMC's data as backup on local BMC
 *  - /var/lib/phosphor-data-sync/notify-sibling/ :
 *      - To keep the generated notify requests
 *  - /var/lib/phosphor-data-sync/notify-services/ :
 *      - To keep the received notify requests form sibling BMC.
 */
void setupPaths();

/**
 * @brief Read the BMC position from /run/openbmc/bmc_position.
 *
 * @return BMC position
 *
 * @throws std::runtime_error if the position cannot be read
 */
std::size_t readBMCPosition();

/*
 * @brief Convert a date/time string to epoch-seconds timestamp.
 *
 * @param[in] dateTime - Date/time string in "%Y/%m/%d %H:%M:%S" format.
 *
 * @return Parsed epoch-seconds timestamp, or std::nullopt on failure.
 */
std::optional<std::chrono::seconds>
    parseDateTimeToEpoch(std::string_view dateTime);

/**
 * @brief Convert a filesystem time point to epoch-seconds timestamp.
 *
 * @param[in] fileTime - Filesystem time point to convert.
 *
 * @return Converted epoch-seconds timestamp.
 */
std::optional<std::chrono::seconds>
    fileTimeToEpoch(const std::filesystem::file_time_type& fileTime);

namespace rsync
{
/**
 * @brief Determine whether rsync actually transferred or deleted data on
 *        the remote.
 *
 * Checks the rsync output for two conditions:
 *  - "Literal data:" value is non-zero  : data was written to the remote
 *  - a "*deleting" itemize line present : files were deleted on the remote
 *
 * @param[in] rsyncOutput - rsync output string containing transfer summary
 * @return true if data was transferred or deleted, false otherwise
 */
bool isSynced(const std::string& rsyncOutput);

} // namespace rsync
} // namespace data_sync::utility
