// SPDX-License-Identifier: Apache-2.0

#include "async_command_exec.hpp"

#include "utility.hpp"

#include <phosphor-logging/lg2.hpp>

namespace data_sync::async
{

using FD = data_sync::utility::FD;
namespace utility
{

SpawnActions::SpawnActions()
{
    if (posix_spawn_file_actions_init(&_actions) != 0)
    {
        lg2::error("Failed to init posix_spawn_file_actions");
        throw std::runtime_error("Failed to init posix_spawn_file_actions");
    }
}

SpawnActions::~SpawnActions()
{
    posix_spawn_file_actions_destroy(&_actions);
}

posix_spawn_file_actions_t* SpawnActions::get()
{
    return &_actions;
}

} // namespace utility

AsyncCommandExecutor::AsyncCommandExecutor(sdbusplus::async::context& ctx) :
    _ctx(ctx)
{}

sdbusplus::async::task<std::pair<int, std::string>>
    // NOLINTNEXTLINE
    AsyncCommandExecutor::execCmd(const std::string& cmd)
{
    int pipefd[2];
    // Create pipe for the IPC
    if (pipe(pipefd) == -1)
    {
        lg2::error("Failed to create pipe. Errno : {ERRNO}, Error : {MSG}",
                   "ERRNO", errno, "MSG", strerror(errno));
        co_return {-1, ""};
    }

    FD readFd(pipefd[0]);
    FD writeFd(pipefd[1]);

    utility::SpawnActions fileActions;
    auto* actions = fileActions.get();

    // Duplicate the child's STDOUT and STDERR to the write end of the pipe
    if (posix_spawn_file_actions_adddup2(actions, writeFd(), STDOUT_FILENO) !=
            0 ||
        posix_spawn_file_actions_adddup2(actions, writeFd(), STDERR_FILENO) !=
            0)
    {
        lg2::error(
            "Failed to duplicate the STDOUT/STDERR to pipe. Errno : {ERRNO}, Error : {MSG}",
            "ERRNO", errno, "MSG", strerror(errno));
        co_return {-1, ""};
    }

    // Close the read end of the pipe in child
    if (posix_spawn_file_actions_addclose(actions, readFd()) != 0)
    {
        lg2::error("Failed to close the pipe's read end in child. Errno : "
                   "{ERRNO}, Error : {MSG}",
                   "ERRNO", errno, "MSG", strerror(errno));
        co_return {-1, ""};
    }

    const char* argv[] = {"/bin/sh", "-c", cmd.c_str(), nullptr};
    pid_t pid = -1;
    int spawnResult = posix_spawn(
        &pid, "/bin/sh", actions, nullptr,
        // [cppcoreguidelines-pro-type-const-cast,-warnings-as-errors]
        // NOLINTNEXTLINE
        const_cast<char* const*>(argv), nullptr);

    // Manually close the write end of the pipe in parent
    writeFd.reset();

    if (spawnResult != 0)
    {
        lg2::error("Spawn for executing command failed : {ERROR}", "ERROR",
                   strerror(spawnResult));
        co_return {-1, ""};
    }

    // Wait until the child writes into the fd.
    auto output = co_await waitForCmdCompletion(readFd());

    // Manually close the read end of the pipe in parent
    readFd.reset();

    // Wait for child process to exit
    int status = -1;
    waitpid(pid, &status, 0);

    int exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    if (!WIFEXITED(status))
    {
        lg2::error("Child exited abnormally. Status: {STATUS}", "STATUS",
                   status);
    }
    else if (exitCode != 0)
    {
        lg2::error("Command failed with  exitCode: {CODE}, Output: {OUT}",
                   "CODE", exitCode, "OUT", output);
    }

    co_return {exitCode, output};
}

sdbusplus::async::task<std::string>
    // NOLINTNEXTLINE
    AsyncCommandExecutor::waitForCmdCompletion(int fd)
{
    // Set non-blocking mode for the file descriptor
    int flags = fcntl(fd, F_GETFL, 0);
    // NOLINTNEXTLINE - [cppcoreguidelines-pro-type-vararg,-warnings-as-errors]
    if (flags == -1 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1)
    {
        lg2::error(
            "Failed to set non-blocking mode. Errno: {ERRNO}, Msg: {MSG}",
            "ERRNO", errno, "MSG", strerror(errno));
        co_return "";
    }

    std::string output;
    std::array<char, 512> buffer{};
    auto fdioInstance = std::make_unique<sdbusplus::async::fdio>(_ctx, fd);

    while (!_ctx.stop_requested())
    {
        co_await fdioInstance->next();

        auto bytes = read(fd, buffer.data(), buffer.size());
        if (bytes > 0)
        {
            output.append(buffer.data(), bytes);
            buffer.fill(0);
        }
        else if (bytes == 0)
        {
            // EOF
            break;
        }
        else if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            continue;
        }
        else
        {
            lg2::error("read failed on fd[{FD}] : [{ERROR}]", "FD", fd, "ERROR",
                       strerror(errno));
            break;
        }
    }

    fdioInstance.reset();

    co_return output;
}

} // namespace data_sync::async
