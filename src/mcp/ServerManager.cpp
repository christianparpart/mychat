// SPDX-License-Identifier: Apache-2.0
#include "ServerManager.hpp"

#include <core/Log.hpp>

#include <format>

namespace mychat
{

ServerManager::ServerManager() = default;

ServerManager::~ServerManager()
{
    shutdown();
}

auto ServerManager::addServer(const McpServerConfig& config) -> VoidResult
{
    auto transport = std::make_unique<StdioTransport>();

    auto transportConfig = StdioTransportConfig {
        .command = config.command,
        .args = config.args,
        .env = config.env,
    };

    auto startResult = transport->start(transportConfig);
    if (!startResult)
        return std::unexpected(startResult.error());

    auto client = std::make_unique<McpClient>(std::move(transport));

    auto initResult = client->initialize();
    if (!initResult)
        return std::unexpected(initResult.error());

    auto toolsResult = client->listTools();
    if (!toolsResult)
    {
        log::warning("Failed to list tools for server '{}': {}", config.name, toolsResult.error().message);
    }

    auto entry = ServerEntry {
        .name = config.name,
        .client = std::move(client),
        .tools = toolsResult ? std::move(*toolsResult) : std::vector<ToolDefinition> {},
    };

    auto const serverIdx = _servers.size();
    for (const auto& tool: entry.tools)
    {
        _toolToServer[tool.name] = serverIdx;
        log::info("  Tool registered: {} (from server '{}')", tool.name, config.name);
    }

    _servers.push_back(std::move(entry));
    log::info("MCP server '{}' connected with {} tools", config.name, _servers.back().tools.size());

    return {};
}

auto ServerManager::allTools() -> std::vector<ToolDefinition>
{
    auto result = std::vector<ToolDefinition> {};
    for (const auto& server: _servers)
    {
        for (const auto& tool: server.tools)
            result.push_back(tool);
    }
    return result;
}

auto ServerManager::callTool(std::string_view name, const nlohmann::json& arguments) -> Result<ToolResult>
{
    auto const it = _toolToServer.find(std::string(name));
    if (it == _toolToServer.end())
        return makeError(ErrorCode::ToolCallError, std::format("Unknown tool: {}", name));

    auto const serverIdx = it->second;
    if (serverIdx >= _servers.size())
        return makeError(ErrorCode::ToolCallError, "Server index out of range");

    auto result = _servers[serverIdx].client->callTool(name, arguments);
    if (result)
        result->callId = std::string(name); // Use tool name as fallback call ID

    return result;
}

auto ServerManager::serverCount() const -> size_t
{
    return _servers.size();
}

void ServerManager::shutdown()
{
    _servers.clear();
    _toolToServer.clear();
}

} // namespace mychat
