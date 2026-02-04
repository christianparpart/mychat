// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <core/Types.hpp>

#include <string>
#include <vector>

namespace mychat
{

/// @brief Manages conversation history for a chat session.
class ChatSession
{
  public:
    /// @brief Constructs a ChatSession with an optional system prompt.
    /// @param systemPrompt The system prompt to prepend to conversations.
    explicit ChatSession(std::string systemPrompt = "");

    /// @brief Adds a user message to the conversation.
    /// @param content The user's message text.
    void addUserMessage(std::string content);

    /// @brief Adds an assistant message to the conversation.
    /// @param content The assistant's response text.
    /// @param toolCalls Any tool calls made by the assistant.
    void addAssistantMessage(std::string content, std::vector<ToolCall> toolCalls = {});

    /// @brief Adds a tool result message to the conversation.
    /// @param callId The ID of the tool call this result corresponds to.
    /// @param content The tool's output.
    /// @param isError Whether the tool execution resulted in an error.
    void addToolResult(std::string callId, std::string content, bool isError = false);

    /// @brief Returns all messages in the conversation, including the system prompt.
    [[nodiscard]] auto messages() const -> const std::vector<ChatMessage>&;

    /// @brief Clears all messages except the system prompt.
    void clear();

    /// @brief Returns the number of messages (excluding system prompt).
    [[nodiscard]] auto messageCount() const -> size_t;

    /// @brief Returns the system prompt.
    [[nodiscard]] auto systemPrompt() const -> const std::string&;

    /// @brief Sets a new system prompt, replacing the existing one.
    /// @param prompt The new system prompt.
    void setSystemPrompt(std::string prompt);

  private:
    std::string _systemPrompt;
    std::vector<ChatMessage> _messages;

    void ensureSystemPrompt();
};

} // namespace mychat
