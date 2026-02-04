// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <core/Error.hpp>
#include <core/Types.hpp>
#include <llm/ChatSession.hpp>
#include <llm/LlmEngine.hpp>
#include <llm/Sampler.hpp>
#include <mcp/ServerManager.hpp>

#include <functional>
#include <string>

namespace mychat
{

/// @brief Configuration for the agent loop.
struct AgentConfig
{
    int maxToolSteps = 10;
    int maxRetries = 3;
    SamplerConfig sampler;
};

/// @brief Callback for streaming tokens to the user interface.
using AgentStreamCallback = std::function<void(std::string_view token)>;

/// @brief Implements a multi-step agent reasoning loop.
///
/// The loop sends user messages to the LLM, executes any tool calls,
/// feeds results back, and repeats until the LLM produces a final text response
/// or the maximum number of tool steps is reached.
class AgentLoop
{
  public:
    /// @brief Constructs an AgentLoop.
    /// @param engine Reference to the LLM engine.
    /// @param session Reference to the chat session.
    /// @param servers Reference to the MCP server manager.
    /// @param config Agent configuration.
    AgentLoop(LlmEngine& engine, ChatSession& session, ServerManager& servers, AgentConfig config);

    /// @brief Processes a user message through the agent loop.
    /// @param userMessage The user's input text.
    /// @param streamCb Optional callback for streaming tokens.
    /// @return The final assistant response text or an error.
    [[nodiscard]] auto processMessage(std::string_view userMessage, AgentStreamCallback streamCb = {})
        -> Result<std::string>;

    /// @brief Returns the agent configuration.
    [[nodiscard]] auto config() const -> const AgentConfig&;

  private:
    LlmEngine& _engine;
    ChatSession& _session;
    ServerManager& _servers;
    AgentConfig _config;

    [[nodiscard]] auto executeToolCalls(const std::vector<ToolCall>& calls) -> std::vector<ToolResult>;
};

} // namespace mychat
