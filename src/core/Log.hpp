// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <format>
#include <functional>
#include <string_view>

namespace mychat::log
{

/// @brief Verbosity level for log messages.
enum class Level
{
    Error,
    Warning,
    Info,
    Debug,
    Trace,
};

/// @brief Callback type that receives all log messages.
/// @param level The log level of the message.
/// @param message The formatted log message text (without level prefix).
using LogCallback = std::function<void(Level level, std::string_view message)>;

/// @brief Sets a callback that receives all log messages.
///
/// When set, log messages are routed to the callback instead of stderr.
/// Pass an empty/nullptr callback to revert to stderr output.
/// @param callback The callback to install, or empty to revert to stderr.
void setCallback(LogCallback callback);

/// @brief Sets the global log verbosity level.
/// @param level The maximum level to output.
void setLevel(Level level);

/// @brief Returns the current global log verbosity level.
[[nodiscard]] auto getLevel() -> Level;

/// @brief Writes a log message at the given level.
///
/// If a callback is installed via setCallback(), the message is routed there.
/// Otherwise, it is written to stderr with a level prefix.
/// @param level The log level.
/// @param message The message to output.
void write(Level level, std::string_view message);

/// @brief Logs an error message.
template <typename... Args>
void error(std::format_string<Args...> fmt, Args&&... args)
{
    write(Level::Error, std::format(fmt, std::forward<Args>(args)...));
}

/// @brief Logs a warning message.
template <typename... Args>
void warning(std::format_string<Args...> fmt, Args&&... args)
{
    write(Level::Warning, std::format(fmt, std::forward<Args>(args)...));
}

/// @brief Logs an info message.
template <typename... Args>
void info(std::format_string<Args...> fmt, Args&&... args)
{
    write(Level::Info, std::format(fmt, std::forward<Args>(args)...));
}

/// @brief Logs a debug message.
template <typename... Args>
void debug(std::format_string<Args...> fmt, Args&&... args)
{
    if (getLevel() >= Level::Debug)
        write(Level::Debug, std::format(fmt, std::forward<Args>(args)...));
}

/// @brief Logs a trace message.
template <typename... Args>
void trace(std::format_string<Args...> fmt, Args&&... args)
{
    if (getLevel() >= Level::Trace)
        write(Level::Trace, std::format(fmt, std::forward<Args>(args)...));
}

} // namespace mychat::log
