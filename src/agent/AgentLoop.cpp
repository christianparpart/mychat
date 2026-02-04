// SPDX-License-Identifier: Apache-2.0
#include "AgentLoop.hpp"

#include <core/Log.hpp>

#include <format>

namespace mychat
{

AgentLoop::AgentLoop(LlmEngine& engine, ChatSession& session, ServerManager& servers, AgentConfig config):
    _engine(engine), _session(session), _servers(servers), _config(std::move(config))
{
}

auto AgentLoop::processMessage(std::string_view userMessage, AgentStreamCallback streamCb)
    -> Result<std::string>
{
    _session.addUserMessage(std::string(userMessage));

    auto tools = _servers.allTools();

    for (auto step = 0; step < _config.maxToolSteps; ++step)
    {
        log::debug("Agent step {}/{}", step + 1, _config.maxToolSteps);

        auto result = _engine.generate(_session.messages(), tools, _config.sampler, streamCb);
        if (!result)
            return std::unexpected(result.error());

        if (!result->hasToolCalls())
        {
            _session.addAssistantMessage(result->text);
            return result->text;
        }

        // Execute tool calls
        log::info("LLM requested {} tool call(s)", result->toolCalls.size());
        _session.addAssistantMessage(result->text, result->toolCalls);

        auto toolResults = executeToolCalls(result->toolCalls);
        for (auto& toolResult: toolResults)
            _session.addToolResult(toolResult.callId, toolResult.content, toolResult.isError);
    }

    // Exceeded max steps — generate without tools to force a final answer
    log::warning("Agent reached max tool steps ({}), forcing final response", _config.maxToolSteps);

    auto const emptyTools = std::span<const ToolDefinition> {};
    auto finalResult = _engine.generate(_session.messages(), emptyTools, _config.sampler, streamCb);
    if (!finalResult)
        return std::unexpected(finalResult.error());

    _session.addAssistantMessage(finalResult->text);
    return finalResult->text;
}

auto AgentLoop::config() const -> const AgentConfig&
{
    return _config;
}

auto AgentLoop::executeToolCalls(const std::vector<ToolCall>& calls) -> std::vector<ToolResult>
{
    auto results = std::vector<ToolResult> {};
    results.reserve(calls.size());

    for (const auto& call: calls)
    {
        log::info("Executing tool: {} (id: {})", call.name, call.id);

        auto result = _servers.callTool(call.name, call.arguments);
        if (result)
        {
            result->callId = call.id;
            results.push_back(std::move(*result));
        }
        else
        {
            log::error("Tool call failed: {}", result.error().message);
            results.push_back(ToolResult {
                .callId = call.id,
                .content = std::format("Error: {}", result.error().message),
                .isError = true,
            });
        }
    }

    return results;
}

} // namespace mychat
