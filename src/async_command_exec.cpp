// SPDX-License-Identifier: Apache-2.0

#include "async_command_exec.hpp"

#include <phosphor-logging/lg2.hpp>

namespace data_sync::async
{

namespace utility
{

SpawnFActions::SpawnFActions()
{
    if (posix_spawn_file_actions_init(&_actions) != 0)
    {
        lg2::error("Failed to init posix_spawn_file_actions, errno : {ERRNO}, "
                   "ERROR : {ERROR}",
                   "ERRNO", errno, "ERROR", strerror(errno));
        throw std::runtime_error("Failed to init posix_spawn_file_actions");
    }
}

SpawnFActions::~SpawnFActions()
{
    if (posix_spawn_file_actions_destroy(&_actions) != 0)
    {
        lg2::error(
            "Failed to destroy the file action instance, errno : {ERRNO}, "
            "ERROR : {ERROR}",
            "ERRNO", errno, "ERROR", strerror(errno));
    }
}

posix_spawn_file_actions_t* SpawnFActions::get()
{
    return &_actions;
}

} // namespace utility

AsyncCommandExecutor::AsyncCommandExecutor(sdbusplus::async::context& ctx) :
    _ctx(ctx)
{}

bool AsyncCommandExecutor::setupPipe(int pipefd[2])
{
    if (pipe(pipefd) == -1)
    {
        lg2::error("Failed to create pipe. Errno: {ERRNO}, Error: {MSG}",
                   "ERRNO", errno, "MSG", strerror(errno));
        return false;
    }
    return true;
}

bool AsyncCommandExecutor::setupPipeRedirection(const FD& readFd,
                                                const FD& writeFd,
                                                const auto& actions)
{
    // Duplicate the write end of the pipe with the child's stdout and stderr
    // so that the parent can read child stdout and stderr after the command
    // execution is completed if any.
    if (posix_spawn_file_actions_adddup2(actions, writeFd(), STDOUT_FILENO) !=
            0 ||
        posix_spawn_file_actions_adddup2(actions, writeFd(), STDERR_FILENO) !=
            0)
    {
        lg2::error(
            "Failed to duplicate the STDOUT/STDERR to pipe. Errno : {ERRNO}, Error : {MSG}",
            "ERRNO", errno, "MSG", strerror(errno));
        return false;
    }

    // Close the read end of the pipe in child before executing command because
    // the parent only needs to read.
    // Otherwise, kernel will think that the child is also reading and will be
    // open even after parent closes it read end, which could cause blocking
    // or close unused file descriptors in this child context.
    if (posix_spawn_file_actions_addclose(actions, readFd()) != 0)
    {
        lg2::error("Failed to close the pipe's read end in child. Errno : "
                   "{ERRNO}, Error : {MSG}",
                   "ERRNO", errno, "MSG", strerror(errno));
        return false;
    }
    return true;
}

std::pair<pid_t, int> AsyncCommandExecutor::spawnCommand(const std::string& cmd,
                                                         const auto& actions)
{
    const char* argv[] = {"/bin/sh", "-c", cmd.c_str(), nullptr};
    pid_t pid = -1;
    int spawnResult = posix_spawn(
        &pid, "/bin/sh", actions, nullptr,
        // [cppcoreguidelines-pro-type-const-cast,-warnings-as-errors]
        // NOLINTNEXTLINE
        const_cast<char* const*>(argv), nullptr);

    if (spawnResult != 0)
    {
        lg2::error("Spawn for executing command failed : {ERROR}", "ERROR",
                   strerror(spawnResult));
    }
    return {pid, spawnResult};
}

sdbusplus::async::task<std::pair<int, std::string>>
    // NOLINTNEXTLINE
    AsyncCommandExecutor::execCmd(const std::string& cmd)
{
    int pipefd[2];
    // Create pipe for the IPC
    if (!setupPipe(pipefd))
    {
        co_return {-1, ""};
    }

    FD readFd(pipefd[0]);
    FD writeFd(pipefd[1]);

    utility::SpawnFActions fileActions;
    auto* actions = fileActions.get();

    if (!setupPipeRedirection(readFd, writeFd, actions))
    {
        co_return {-1, ""};
    }

    auto [pid, spawnResult] = spawnCommand(cmd, actions);

    // Manually close the write end of the pipe in parent because only the child
    // need to write.
    // Otherwise, the kernel will think that the parent also writes and will be
    // open even after child closes it's write end, which could blocks the
    // parent's read() forever without returning EOF and will hang waiting to
    // read.
    writeFd.reset();

    // Wait until the child writes into the fd.
    // NOLINTNEXTLINE
    auto output = co_await waitForCmdCompletion(readFd());

    // Manually close the read fd of the parent immediately instead of keeping
    // it open until RAII scope cleanup.
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

    co_return output;
}

} // namespace data_sync::async
