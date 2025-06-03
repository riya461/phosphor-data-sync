// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <fcntl.h>
#include <spawn.h>

#include <sdbusplus/async.hpp>

namespace data_sync::async
{
namespace utility
{
/**
 * @class SpawnActions
 *
 * @brief Class to handle the file operations object of the posix std
 */
class SpawnActions
{
  public:
    SpawnActions(const SpawnActions&) = delete;
    SpawnActions& operator=(const SpawnActions&) = delete;
    SpawnActions(SpawnActions&&) = delete;
    SpawnActions& operator=(SpawnActions&&) = delete;

    /**
     * @brief Constructor
     *
     * To initialize the file action object for handling the spawned
     * process
     */
    SpawnActions();

    /**
     * @brief Destructor
     *
     * To destroy the file action object.
     */
    ~SpawnActions();

    /**
     * @brief API to return the reference of the initialised file actions
     * object
     *
     * @return posix_spawn_file_actions_t*
     */
    posix_spawn_file_actions_t* get();

  private:
    posix_spawn_file_actions_t _actions;
};
} // namespace utility

/**
 * @class AsyncCommandExecutor
 *
 * @brief To abstract the APIs to execute the CLI commands with posix spawn.
 */
class AsyncCommandExecutor
{
  public:
    AsyncCommandExecutor(const AsyncCommandExecutor&) = delete;
    AsyncCommandExecutor& operator=(const AsyncCommandExecutor&) = delete;
    AsyncCommandExecutor(AsyncCommandExecutor&&) = delete;
    AsyncCommandExecutor& operator=(AsyncCommandExecutor&&) = delete;

    /**
     * @brief Constructor
     *
     *  @param[in] ctx - The async context object
     *
     */
    AsyncCommandExecutor(sdbusplus::async::context& ctx);

    /**
     * @brief To execute bash commands asynchronously and redirect the
     *        comamnd output to a pipe to read by parent process using
     *        'posix_spawn'.
     *
     * @param[in] - cmd - The bash command to execute
     *
     * @return sdbusplus::async::task<std::pair<int, std::string>>
     *              - int : Exit code of the spawned process (-1 on failure)
     *              - std::string : Combined stdout and stderr output
     */
    sdbusplus::async::task<std::pair<int, std::string>>
        execCmd(const std::string& cmd);

  private:
    /**
     * @brief API to wait asynchronously until child completes the command
     *        execution and read and accumulate the output from the file
     *        descriptor once it is ready.
     *
     * @param[in] - fd - file descriptor to read the data from.
     *
     * @return - sdbusplus::async::task<std::string>
     *             On success - The accumulated output from the descriptor.
     *             On failure - An empty string.
     *
     */
    sdbusplus::async::task<std::string> waitForCmdCompletion(int fd);

    /**
     * @brief The async context object used to perform operations
     *        asynchronously as required.
     */
    sdbusplus::async::context& _ctx;
};
} // namespace data_sync::async
