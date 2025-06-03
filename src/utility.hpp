// SPDX-License-Identifier: Apache-2.0

#pragma once

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

} // namespace data_sync::utility
