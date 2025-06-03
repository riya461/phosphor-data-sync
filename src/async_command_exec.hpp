// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "utility.hpp"

#include <fcntl.h>
#include <spawn.h>

#include <sdbusplus/async.hpp>

namespace data_sync::async
{

using FD = data_sync::utility::FD;
namespace utility
{
/**
 * @class SpawnFActions
 *
 * @brief Class to handle the file operations object of the posix std
 */
class SpawnFActions
{
  public:
    SpawnFActions(const SpawnFActions&) = delete;
    SpawnFActions& operator=(const SpawnFActions&) = delete;
    SpawnFActions(SpawnFActions&&) = delete;
    SpawnFActions& operator=(SpawnFActions&&) = delete;

    /**
     * @brief Constructor
     *
     * To initialize the file action object for handling the spawned
     * process
     */
    SpawnFActions();

    /**
     * @brief Destructor
     *
     * To destroy the file action object.
     */
    ~SpawnFActions();

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
     * @brief API to setup the pipe for the parent ot child communication by
     * wrapping the pipe() system call.
     *
     * @param[out] pipefd - Array of two integers to hold the read and write end
     *                      of the created pipe.
     *
     * @return - true, if the pipe was successfully created.
     *         - false, if the pipe creation failed.
     */
    static bool setupPipe(int pipefd[2]);

    /**
     * @brief Configure file actions to redirect child process stdout and stderr
     * to the write end of the pipe so that parent can read the same.
     *
     * @param[in]  readFd   File descriptor for the read end of the pipe.
     * @param[in]  writeFd  File descriptor for the write end of the pipe.
     * @param[in]  actions  reference to the posix_spawn file actions object.
     *
     * @return true,  if the redirections were successfully set up.
     *         false, if any of the redirections failed.
     */
    bool setupPipeRedirection(const FD& readFd, const FD& writeFd,
                              const auto& actions);

    /**
     * @brief API to spawn a child process by wrapping the posix_spawn(),
     *        executing the provided command string in the spawned child
     *        process.
     *
     * @param[in]  cmd     Command string to execute.
     * @param[in]  actions  reference to the posix_spawn file actions object
     *
     * @return std::pair<pid_t, int>
     *         - first  : PID of the spawned child process (-1 for failure).
     *         - second : Result of posix_spawn().
     */
    std::pair<pid_t, int> spawnCommand(const std::string& cmd,
                                       const auto& actions);

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
