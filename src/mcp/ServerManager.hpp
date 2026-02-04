// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <core/Error.hpp>
#include <core/Types.hpp>
#include <mcp/McpClient.hpp>
#include <mcp/StdioTransport.hpp>

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace mychat
{

/// @brief Configuration for a single MCP server.
struct McpServerConfig
{
    std::string name;
    std::string command;
    std::vector<std::string> args;
    std::map<std::string, std::string> env;
};

/// @brief Manages multiple MCP server connections and routes tool calls.
class ServerManager
{
  public:
    ServerManager();
    ~ServerManager();

    ServerManager(const ServerManager&) = delete;
    ServerManager& operator=(const ServerManager&) = delete;

    /// @brief Starts and initializes an MCP server.
    /// @param config The server configuration.
    /// @return Success or an error.
    [[nodiscard]] auto addServer(const McpServerConfig& config) -> VoidResult;

    /// @brief Lists all available tools from all connected servers.
    /// @return A vector of tool definitions.
    [[nodiscard]] auto allTools() -> std::vector<ToolDefinition>;

    /// @brief Calls a tool by name, routing to the correct server.
    /// @param name The tool name.
    /// @param arguments The tool arguments.
    /// @return The tool result or an error.
    [[nodiscard]] auto callTool(std::string_view name, const nlohmann::json& arguments) -> Result<ToolResult>;

    /// @brief Returns the number of connected servers.
    [[nodiscard]] auto serverCount() const -> size_t;

    /// @brief Shuts down all servers.
    void shutdown();

  private:
    struct ServerEntry
    {
        std::string name;
        std::unique_ptr<McpClient> client;
        std::vector<ToolDefinition> tools;
    };

    std::vector<ServerEntry> _servers;
    std::map<std::string, size_t> _toolToServer; // tool name → server index
};

} // namespace mychat
