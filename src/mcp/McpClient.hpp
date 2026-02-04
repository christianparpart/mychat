// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <core/Error.hpp>
#include <core/Types.hpp>
#include <mcp/Transport.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace mychat
{

/// @brief MCP server capabilities reported during initialization.
struct McpServerCapabilities
{
    bool hasTools = false;
    bool hasResources = false;
    bool hasPrompts = false;
    std::string serverName;
    std::string serverVersion;
};

/// @brief Client for the Model Context Protocol (MCP).
///
/// Handles the MCP lifecycle: initialize, list tools, call tools.
class McpClient
{
  public:
    /// @brief Constructs an McpClient with the given transport.
    /// @param transport The transport to use for communication.
    explicit McpClient(std::unique_ptr<Transport> transport);
    ~McpClient();

    McpClient(const McpClient&) = delete;
    McpClient& operator=(const McpClient&) = delete;

    /// @brief Performs the MCP initialize handshake.
    /// @return The server's capabilities or an error.
    [[nodiscard]] auto initialize() -> Result<McpServerCapabilities>;

    /// @brief Lists available tools from the server.
    /// @return A vector of tool definitions or an error.
    [[nodiscard]] auto listTools() -> Result<std::vector<ToolDefinition>>;

    /// @brief Calls a tool on the server.
    /// @param name The tool name.
    /// @param arguments The tool arguments.
    /// @return The tool result or an error.
    [[nodiscard]] auto callTool(std::string_view name, const nlohmann::json& arguments) -> Result<ToolResult>;

    /// @brief Returns the server capabilities (valid after initialize).
    [[nodiscard]] auto capabilities() const -> const McpServerCapabilities&;

    /// @brief Returns true if the client has been initialized.
    [[nodiscard]] auto isInitialized() const -> bool;

  private:
    std::unique_ptr<Transport> _transport;
    McpServerCapabilities _capabilities;
    int64_t _nextId = 1;
    bool _initialized = false;

    [[nodiscard]] auto sendRequest(std::string_view method, nlohmann::json params = nullptr)
        -> Result<nlohmann::json>;
};

} // namespace mychat
