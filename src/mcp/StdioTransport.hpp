// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <mcp/Transport.hpp>

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace mychat
{

/// @brief Configuration for spawning an MCP server process.
struct StdioTransportConfig
{
    std::string command;
    std::vector<std::string> args;
    std::map<std::string, std::string> env;
};

/// @brief Transport that communicates with an MCP server via stdio pipes.
///
/// Spawns a child process and communicates via its stdin/stdout.
class StdioTransport: public Transport
{
  public:
    StdioTransport();
    ~StdioTransport() override;

    StdioTransport(const StdioTransport&) = delete;
    StdioTransport& operator=(const StdioTransport&) = delete;

    /// @brief Starts the MCP server process.
    /// @param config The process configuration.
    /// @return Success or an error.
    [[nodiscard]] auto start(const StdioTransportConfig& config) -> VoidResult;

    [[nodiscard]] auto send(const nlohmann::json& message) -> VoidResult override;
    [[nodiscard]] auto receive() -> Result<nlohmann::json> override;
    void close() override;
    [[nodiscard]] auto isConnected() const -> bool override;

  private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
};

} // namespace mychat
