// SPDX-License-Identifier: Apache-2.0
#include "ChatSession.hpp"

#include <utility>

namespace mychat
{

ChatSession::ChatSession(std::string systemPrompt): _systemPrompt(std::move(systemPrompt))
{
    ensureSystemPrompt();
}

void ChatSession::addUserMessage(std::string content)
{
    _messages.push_back(ChatMessage {
        .role = Role::User,
        .content = std::move(content),
        .toolCalls = {},
        .toolCallId = {},
    });
}

void ChatSession::addAssistantMessage(std::string content, std::vector<ToolCall> toolCalls)
{
    _messages.push_back(ChatMessage {
        .role = Role::Assistant,
        .content = std::move(content),
        .toolCalls = std::move(toolCalls),
        .toolCallId = {},
    });
}

void ChatSession::addToolResult(std::string callId, std::string content, bool isError)
{
    _messages.push_back(ChatMessage {
        .role = Role::Tool,
        .content = std::move(content),
        .toolCalls = {},
        .toolCallId = std::move(callId),
    });
    (void) isError; // Stored in content context
}

auto ChatSession::messages() const -> const std::vector<ChatMessage>&
{
    return _messages;
}

void ChatSession::clear()
{
    _messages.clear();
    ensureSystemPrompt();
}

auto ChatSession::messageCount() const -> size_t
{
    if (_systemPrompt.empty())
        return _messages.size();
    return _messages.empty() ? 0 : _messages.size() - 1;
}

auto ChatSession::systemPrompt() const -> const std::string&
{
    return _systemPrompt;
}

void ChatSession::setSystemPrompt(std::string prompt)
{
    _systemPrompt = std::move(prompt);
    _messages.clear();
    ensureSystemPrompt();
}

void ChatSession::ensureSystemPrompt()
{
    if (!_systemPrompt.empty())
    {
        _messages.insert(_messages.begin(),
                         ChatMessage {
                             .role = Role::System,
                             .content = _systemPrompt,
                             .toolCalls = {},
                             .toolCallId = {},
                         });
    }
}

} // namespace mychat
