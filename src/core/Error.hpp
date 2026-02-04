// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <expected>
#include <format>
#include <string>

namespace mychat
{

/// @brief Error codes for categorizing failures across the application.
enum class ErrorCode
{
    Unknown,
    InvalidArgument,
    IoError,
    ConfigError,
    ModelLoadError,
    InferenceError,
    AudioError,
    TranscriptionError,
    TransportError,
    ProtocolError,
    ToolCallError,
    TimeoutError,
    DownloadError,
};

/// @brief Represents an error with a code and descriptive message.
struct Error
{
    ErrorCode code = ErrorCode::Unknown;
    std::string message;
};

/// @brief Result type for operations that return a value or an error.
/// @tparam T The success value type.
template <typename T>
using Result = std::expected<T, Error>;

/// @brief Result type for operations that return no value on success.
using VoidResult = std::expected<void, Error>;

/// @brief Creates an unexpected Error value for use with std::expected.
/// @param code The error code.
/// @param message A descriptive error message.
/// @return An unexpected Error.
[[nodiscard]] inline auto makeError(ErrorCode code, std::string message) -> std::unexpected<Error>
{
    return std::unexpected<Error>(Error { code, std::move(message) });
}

} // namespace mychat

template <>
struct std::formatter<mychat::Error>: std::formatter<std::string>
{
    auto format(const mychat::Error& error, auto& ctx) const
    {
        return std::formatter<std::string>::format(
            std::format("[{}] {}", static_cast<int>(error.code), error.message), ctx);
    }
};
