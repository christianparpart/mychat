// SPDX-License-Identifier: Apache-2.0
#include "Log.hpp"

#include <print>

namespace mychat::log
{

namespace
{
    auto globalLevel = Level::Info;
    auto globalCallback = LogCallback {};
} // namespace

void setCallback(LogCallback callback)
{
    globalCallback = std::move(callback);
}

void setLevel(Level level)
{
    globalLevel = level;
}

auto getLevel() -> Level
{
    return globalLevel;
}

void write(Level level, std::string_view message)
{
    if (level > globalLevel)
        return;

    if (globalCallback)
    {
        globalCallback(level, message);
        return;
    }

    constexpr auto levelPrefix = [](Level l) -> std::string_view {
        switch (l)
        {
            case Level::Error: return "ERROR";
            case Level::Warning: return "WARN ";
            case Level::Info: return "INFO ";
            case Level::Debug: return "DEBUG";
            case Level::Trace: return "TRACE";
        }
        return "?????";
    };

    std::println(stderr, "[{}] {}", levelPrefix(level), message);
}

} // namespace mychat::log
