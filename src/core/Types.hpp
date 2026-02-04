// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace mychat
{

/// @brief The role of a message participant in a chat conversation.
enum class Role
{
    System,
    User,
    Assistant,
    Tool,
};

/// @brief Converts a Role enum to its string representation.
/// @param role The role to convert.
/// @return The string representation.
[[nodiscard]] constexpr auto roleToString(Role role) -> std::string_view
{
    switch (role)
    {
        case Role::System: return "system";
        case Role::User: return "user";
        case Role::Assistant: return "assistant";
        case Role::Tool: return "tool";
    }
    return "unknown";
}

/// @brief Parses a string to a Role enum value.
/// @param str The string to parse.
/// @return The corresponding Role, or Role::User if unknown.
[[nodiscard]] constexpr auto roleFromString(std::string_view str) -> Role
{
    if (str == "system")
        return Role::System;
    if (str == "assistant")
        return Role::Assistant;
    if (str == "tool")
        return Role::Tool;
    return Role::User;
}

/// @brief Represents a tool call request from the LLM.
struct ToolCall
{
    std::string id;
    std::string name;
    nlohmann::json arguments;
};

/// @brief Represents the result of executing a tool call.
struct ToolResult
{
    std::string callId;
    std::string content;
    bool isError = false;
};

/// @brief Schema for an input parameter in a tool definition.
struct ToolParameter
{
    std::string name;
    std::string type;
    std::string description;
    bool required = false;
};

/// @brief Defines a tool that the LLM can invoke.
struct ToolDefinition
{
    std::string name;
    std::string description;
    nlohmann::json inputSchema;
};

/// @brief A single message in a chat conversation.
struct ChatMessage
{
    Role role = Role::User;
    std::string content;
    std::vector<ToolCall> toolCalls;
    std::string toolCallId; // For Role::Tool messages
};

/// @brief The result of an LLM generation — either text or tool calls.
struct GenerateResult
{
    std::string text;
    std::vector<ToolCall> toolCalls;

    /// @brief Returns true if this result contains tool calls.
    [[nodiscard]] auto hasToolCalls() const -> bool { return !toolCalls.empty(); }
};

} // namespace mychat
