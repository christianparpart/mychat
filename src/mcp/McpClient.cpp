// SPDX-License-Identifier: Apache-2.0
#include "McpClient.hpp"

#include <core/JsonUtils.hpp>
#include <core/Log.hpp>
#include <mcp/JsonRpc.hpp>

#include <format>

namespace mychat
{

McpClient::McpClient(std::unique_ptr<Transport> transport): _transport(std::move(transport))
{
}

McpClient::~McpClient() = default;

auto McpClient::initialize() -> Result<McpServerCapabilities>
{
    auto params = nlohmann::json {
        { "protocolVersion", "2024-11-05" },
        { "capabilities", nlohmann::json::object() },
        { "clientInfo",
          nlohmann::json {
              { "name", "mychat" },
              { "version", "0.1.0" },
          } },
    };

    return sendRequest("initialize", std::move(params))
        .and_then([this](const nlohmann::json& result) -> Result<McpServerCapabilities> {
            _capabilities.serverName =
                json::getStringOr(result.value("serverInfo", nlohmann::json {}), "name", "unknown");
            _capabilities.serverVersion =
                json::getStringOr(result.value("serverInfo", nlohmann::json {}), "version", "unknown");

            if (result.contains("capabilities"))
            {
                auto const& caps = result["capabilities"];
                _capabilities.hasTools = caps.contains("tools");
                _capabilities.hasResources = caps.contains("resources");
                _capabilities.hasPrompts = caps.contains("prompts");
            }

            // Send initialized notification
            auto notif = jsonrpc::makeNotification("notifications/initialized");
            (void) _transport->send(notif);

            _initialized = true;
            log::info(
                "MCP server initialized: {} v{}", _capabilities.serverName, _capabilities.serverVersion);

            return _capabilities;
        });
}

auto McpClient::listTools() -> Result<std::vector<ToolDefinition>>
{
    if (!_initialized)
        return makeError(ErrorCode::ProtocolError, "Client not initialized");

    return sendRequest("tools/list")
        .and_then([](const nlohmann::json& result) -> Result<std::vector<ToolDefinition>> {
            auto tools = std::vector<ToolDefinition> {};

            if (!result.contains("tools") || !result["tools"].is_array())
                return tools;

            for (const auto& toolJson: result["tools"])
            {
                auto tool = ToolDefinition {
                    .name = toolJson.value("name", ""),
                    .description = toolJson.value("description", ""),
                    .inputSchema = toolJson.value("inputSchema", nlohmann::json::object()),
                };
                tools.push_back(std::move(tool));
            }

            return tools;
        });
}

auto McpClient::callTool(std::string_view name, const nlohmann::json& arguments) -> Result<ToolResult>
{
    if (!_initialized)
        return makeError(ErrorCode::ProtocolError, "Client not initialized");

    auto params = nlohmann::json {
        { "name", name },
        { "arguments", arguments },
    };

    return sendRequest("tools/call", std::move(params))
        .and_then([&name](const nlohmann::json& result) -> Result<ToolResult> {
            auto toolResult = ToolResult {};
            toolResult.isError = result.value("isError", false);

            if (result.contains("content") && result["content"].is_array())
            {
                for (const auto& item: result["content"])
                {
                    if (item.value("type", "") == "text")
                    {
                        if (!toolResult.content.empty())
                            toolResult.content += "\n";
                        toolResult.content += item.value("text", "");
                    }
                }
            }

            log::debug("Tool '{}' returned: {} (isError: {})", name, toolResult.content, toolResult.isError);
            return toolResult;
        });
}

auto McpClient::capabilities() const -> const McpServerCapabilities&
{
    return _capabilities;
}

auto McpClient::isInitialized() const -> bool
{
    return _initialized;
}

auto McpClient::sendRequest(std::string_view method, nlohmann::json params) -> Result<nlohmann::json>
{
    auto const id = _nextId++;
    auto request = jsonrpc::makeRequest(id, method, std::move(params));

    return _transport->send(request)
        .and_then([this]() -> Result<nlohmann::json> { return _transport->receive(); })
        .and_then([](const nlohmann::json& msg) -> Result<nlohmann::json> {
            return jsonrpc::parseResponse(msg).and_then(
                [](const jsonrpc::Response& resp) -> Result<nlohmann::json> {
                    if (resp.error)
                    {
                        return makeError(
                            ErrorCode::ProtocolError,
                            std::format("RPC error {}: {}", resp.error->code, resp.error->message));
                    }
                    return resp.result.value_or(nlohmann::json::object());
                });
        });
}

} // namespace mychat
