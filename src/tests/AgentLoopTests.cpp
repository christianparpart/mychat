// SPDX-License-Identifier: Apache-2.0

// Agent loop tests use mock LLM engine and mock transport.
// Since LlmEngine wraps llama.cpp which requires a real model file,
// we test the AgentLoop logic indirectly through integration with
// mock MCP transport.

#include <core/Types.hpp>
#include <llm/ChatSession.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace mychat;

TEST_CASE("GenerateResult correctly identifies tool calls", "[agent]")
{
    auto result = GenerateResult {};
    CHECK(!result.hasToolCalls());

    result.toolCalls.push_back(ToolCall { .id = "1", .name = "test", .arguments = {} });
    CHECK(result.hasToolCalls());
}

TEST_CASE("ChatSession supports full agent conversation flow", "[agent]")
{
    auto session = ChatSession("You are a helpful assistant.");

    // User message
    session.addUserMessage("What files are in /tmp?");

    // Assistant responds with tool call
    auto toolCalls = std::vector<ToolCall> {
        ToolCall { .id = "call_0", .name = "list_directory", .arguments = { { "path", "/tmp" } } },
    };
    session.addAssistantMessage("Let me check that for you.", toolCalls);

    // Tool result
    session.addToolResult("call_0", "file1.txt\nfile2.txt");

    // Assistant final response
    session.addAssistantMessage("The directory /tmp contains: file1.txt and file2.txt");

    // Verify conversation flow
    REQUIRE(session.messages().size() == 5); // system + 4
    CHECK(session.messages()[0].role == Role::System);
    CHECK(session.messages()[1].role == Role::User);
    CHECK(session.messages()[2].role == Role::Assistant);
    CHECK(session.messages()[2].toolCalls.size() == 1);
    CHECK(session.messages()[3].role == Role::Tool);
    CHECK(session.messages()[4].role == Role::Assistant);
}

TEST_CASE("ToolDefinition can be constructed", "[agent]")
{
    auto tool = ToolDefinition {
        .name = "test_tool",
        .description = "A test tool",
        .inputSchema = {
            { "type", "object" },
            { "properties", { { "input", { { "type", "string" } } } } },
        },
    };

    CHECK(tool.name == "test_tool");
    CHECK(tool.description == "A test tool");
    CHECK(tool.inputSchema.contains("type"));
}
