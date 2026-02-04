// SPDX-License-Identifier: Apache-2.0
#include "StdioTransport.hpp"

#include <core/JsonUtils.hpp>
#include <core/Log.hpp>

#include <format>

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
#else
    #include <sys/wait.h>

    #include <signal.h>
    #include <spawn.h>
    #include <unistd.h>

extern char** environ;
#endif

namespace mychat
{

struct StdioTransport::Impl
{
#ifdef _WIN32
    HANDLE childProcess = INVALID_HANDLE_VALUE;
    HANDLE stdinWrite = INVALID_HANDLE_VALUE;
    HANDLE stdoutRead = INVALID_HANDLE_VALUE;
#else
    pid_t childPid = -1;
    int stdinWrite = -1;
    int stdoutRead = -1;
#endif
    bool connected = false;
    std::string readBuffer;
};

StdioTransport::StdioTransport(): _impl(std::make_unique<Impl>())
{
}

StdioTransport::~StdioTransport()
{
    close();
}

auto StdioTransport::start(const StdioTransportConfig& config) -> VoidResult
{
    if (_impl->connected)
        return makeError(ErrorCode::TransportError, "Transport already connected");

#ifdef _WIN32
    // Windows: CreateProcess with pipes
    SECURITY_ATTRIBUTES sa {};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE stdinRead, stdinWrite, stdoutRead, stdoutWrite;
    if (!CreatePipe(&stdinRead, &stdinWrite, &sa, 0))
        return makeError(ErrorCode::TransportError, "Failed to create stdin pipe");
    if (!CreatePipe(&stdoutRead, &stdoutWrite, &sa, 0))
    {
        CloseHandle(stdinRead);
        CloseHandle(stdinWrite);
        return makeError(ErrorCode::TransportError, "Failed to create stdout pipe");
    }

    SetHandleInformation(stdinWrite, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(stdoutRead, HANDLE_FLAG_INHERIT, 0);

    auto cmdLine = config.command;
    for (const auto& arg: config.args)
        cmdLine += " " + arg;

    STARTUPINFOA si {};
    si.cb = sizeof(si);
    si.hStdInput = stdinRead;
    si.hStdOutput = stdoutWrite;
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    si.dwFlags |= STARTF_USESTDHANDLES;

    PROCESS_INFORMATION pi {};
    if (!CreateProcessA(nullptr, cmdLine.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si, &pi))
    {
        CloseHandle(stdinRead);
        CloseHandle(stdinWrite);
        CloseHandle(stdoutRead);
        CloseHandle(stdoutWrite);
        return makeError(ErrorCode::TransportError,
                         std::format("Failed to start process: {}", config.command));
    }

    CloseHandle(stdinRead);
    CloseHandle(stdoutWrite);
    CloseHandle(pi.hThread);

    _impl->childProcess = pi.hProcess;
    _impl->stdinWrite = stdinWrite;
    _impl->stdoutRead = stdoutRead;
#else
    // POSIX: posix_spawn with pipes
    int stdinPipe[2];
    int stdoutPipe[2];

    if (pipe(stdinPipe) != 0)
        return makeError(ErrorCode::TransportError, "Failed to create stdin pipe");
    if (pipe(stdoutPipe) != 0)
    {
        ::close(stdinPipe[0]);
        ::close(stdinPipe[1]);
        return makeError(ErrorCode::TransportError, "Failed to create stdout pipe");
    }

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, stdinPipe[0], STDIN_FILENO);
    posix_spawn_file_actions_adddup2(&actions, stdoutPipe[1], STDOUT_FILENO);
    posix_spawn_file_actions_addclose(&actions, stdinPipe[1]);
    posix_spawn_file_actions_addclose(&actions, stdoutPipe[0]);

    // Build argv
    auto argv = std::vector<char*> {};
    auto cmdCopy = config.command;
    argv.push_back(cmdCopy.data());
    auto argCopies = std::vector<std::string>(config.args);
    for (auto& arg: argCopies)
        argv.push_back(arg.data());
    argv.push_back(nullptr);

    // Build environment (inherit + config overrides)
    auto envStrings = std::vector<std::string> {};
    if (environ)
    {
        for (auto** e = environ; *e; ++e)
            envStrings.emplace_back(*e);
    }
    for (const auto& [key, value]: config.env)
        envStrings.push_back(std::format("{}={}", key, value));

    auto envp = std::vector<char*> {};
    for (auto& s: envStrings)
        envp.push_back(s.data());
    envp.push_back(nullptr);

    pid_t pid;
    auto const status =
        posix_spawnp(&pid, config.command.c_str(), &actions, nullptr, argv.data(), envp.data());
    posix_spawn_file_actions_destroy(&actions);

    ::close(stdinPipe[0]);
    ::close(stdoutPipe[1]);

    if (status != 0)
    {
        ::close(stdinPipe[1]);
        ::close(stdoutPipe[0]);
        return makeError(ErrorCode::TransportError,
                         std::format("Failed to spawn process '{}': {}", config.command, strerror(status)));
    }

    _impl->childPid = pid;
    _impl->stdinWrite = stdinPipe[1];
    _impl->stdoutRead = stdoutPipe[0];
#endif

    _impl->connected = true;
    log::info("MCP server started: {}", config.command);
    return {};
}

auto StdioTransport::send(const nlohmann::json& message) -> VoidResult
{
    if (!_impl->connected)
        return makeError(ErrorCode::TransportError, "Transport not connected");

    auto const data = message.dump() + "\n";

#ifdef _WIN32
    DWORD written;
    if (!WriteFile(_impl->stdinWrite, data.c_str(), static_cast<DWORD>(data.size()), &written, nullptr))
        return makeError(ErrorCode::TransportError, "Failed to write to process stdin");
#else
    auto const result = ::write(_impl->stdinWrite, data.c_str(), data.size());
    if (result < 0)
        return makeError(ErrorCode::TransportError, "Failed to write to process stdin");
#endif

    return {};
}

auto StdioTransport::receive() -> Result<nlohmann::json>
{
    if (!_impl->connected)
        return makeError(ErrorCode::TransportError, "Transport not connected");

    // Read until we get a complete line
    while (true)
    {
        auto const newlinePos = _impl->readBuffer.find('\n');
        if (newlinePos != std::string::npos)
        {
            auto line = _impl->readBuffer.substr(0, newlinePos);
            _impl->readBuffer.erase(0, newlinePos + 1);

            if (line.empty())
                continue;

            return json::parse(line);
        }

        auto buf = std::array<char, 4096> {};
#ifdef _WIN32
        DWORD bytesRead;
        if (!ReadFile(_impl->stdoutRead, buf.data(), static_cast<DWORD>(buf.size()), &bytesRead, nullptr)
            || bytesRead == 0)
        {
            _impl->connected = false;
            return makeError(ErrorCode::TransportError, "Process stdout closed");
        }
        _impl->readBuffer.append(buf.data(), bytesRead);
#else
        auto const bytesRead = ::read(_impl->stdoutRead, buf.data(), buf.size());
        if (bytesRead <= 0)
        {
            _impl->connected = false;
            return makeError(ErrorCode::TransportError, "Process stdout closed");
        }
        _impl->readBuffer.append(buf.data(), static_cast<size_t>(bytesRead));
#endif
    }
}

void StdioTransport::close()
{
    if (!_impl->connected)
        return;

    _impl->connected = false;

#ifdef _WIN32
    if (_impl->stdinWrite != INVALID_HANDLE_VALUE)
    {
        CloseHandle(_impl->stdinWrite);
        _impl->stdinWrite = INVALID_HANDLE_VALUE;
    }
    if (_impl->stdoutRead != INVALID_HANDLE_VALUE)
    {
        CloseHandle(_impl->stdoutRead);
        _impl->stdoutRead = INVALID_HANDLE_VALUE;
    }
    if (_impl->childProcess != INVALID_HANDLE_VALUE)
    {
        TerminateProcess(_impl->childProcess, 0);
        WaitForSingleObject(_impl->childProcess, 5000);
        CloseHandle(_impl->childProcess);
        _impl->childProcess = INVALID_HANDLE_VALUE;
    }
#else
    if (_impl->stdinWrite >= 0)
    {
        ::close(_impl->stdinWrite);
        _impl->stdinWrite = -1;
    }
    if (_impl->stdoutRead >= 0)
    {
        ::close(_impl->stdoutRead);
        _impl->stdoutRead = -1;
    }
    if (_impl->childPid > 0)
    {
        kill(_impl->childPid, SIGTERM);
        int status;
        waitpid(_impl->childPid, &status, 0);
        _impl->childPid = -1;
    }
#endif

    log::debug("MCP transport closed");
}

auto StdioTransport::isConnected() const -> bool
{
    return _impl->connected;
}

} // namespace mychat
