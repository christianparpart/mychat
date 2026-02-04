// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <core/Error.hpp>

#include <nlohmann/json.hpp>

namespace mychat
{

/// @brief Abstract interface for MCP transport communication.
class Transport
{
  public:
    virtual ~Transport() = default;

    /// @brief Sends a JSON message to the server.
    /// @param message The JSON message to send.
    /// @return Success or an error.
    [[nodiscard]] virtual auto send(const nlohmann::json& message) -> VoidResult = 0;

    /// @brief Receives a JSON message from the server (blocking).
    /// @return The received JSON message or an error.
    [[nodiscard]] virtual auto receive() -> Result<nlohmann::json> = 0;

    /// @brief Closes the transport connection.
    virtual void close() = 0;

    /// @brief Returns true if the transport is connected.
    [[nodiscard]] virtual auto isConnected() const -> bool = 0;
};

} // namespace mychat
