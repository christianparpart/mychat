// SPDX-License-Identifier: Apache-2.0
#include <llm/ChatSession.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace mychat;

TEST_CASE("ChatSession starts with system prompt", "[chat]")
{
    auto session = ChatSession("You are a helpful assistant.");

    REQUIRE(session.messages().size() == 1);
    CHECK(session.messages()[0].role == Role::System);
    CHECK(session.messages()[0].content == "You are a helpful assistant.");
    CHECK(session.messageCount() == 0); // System prompt doesn't count
}

TEST_CASE("ChatSession without system prompt", "[chat]")
{
    auto session = ChatSession();

    REQUIRE(session.messages().empty());
    CHECK(session.messageCount() == 0);
}

TEST_CASE("ChatSession adds user messages", "[chat]")
{
    auto session = ChatSession("system");

    session.addUserMessage("Hello!");
    REQUIRE(session.messages().size() == 2);
    CHECK(session.messages()[1].role == Role::User);
    CHECK(session.messages()[1].content == "Hello!");
    CHECK(session.messageCount() == 1);
}

TEST_CASE("ChatSession adds assistant messages", "[chat]")
{
    auto session = ChatSession("system");

    session.addAssistantMessage("Hi there!");
    REQUIRE(session.messages().size() == 2);
    CHECK(session.messages()[1].role == Role::Assistant);
    CHECK(session.messages()[1].content == "Hi there!");
}

TEST_CASE("ChatSession adds assistant messages with tool calls", "[chat]")
{
    auto session = ChatSession("system");

    auto toolCalls = std::vector<ToolCall> {
        ToolCall { .id = "1", .name = "test_tool", .arguments = { { "key", "value" } } },
    };
    session.addAssistantMessage("Calling tool...", toolCalls);

    REQUIRE(session.messages().size() == 2);
    CHECK(session.messages()[1].toolCalls.size() == 1);
    CHECK(session.messages()[1].toolCalls[0].name == "test_tool");
}

TEST_CASE("ChatSession adds tool results", "[chat]")
{
    auto session = ChatSession("system");

    session.addToolResult("call_1", "Tool output");
    REQUIRE(session.messages().size() == 2);
    CHECK(session.messages()[1].role == Role::Tool);
    CHECK(session.messages()[1].content == "Tool output");
    CHECK(session.messages()[1].toolCallId == "call_1");
}

TEST_CASE("ChatSession clear resets to system prompt", "[chat]")
{
    auto session = ChatSession("system");

    session.addUserMessage("Hello");
    session.addAssistantMessage("Hi");
    REQUIRE(session.messageCount() == 2);

    session.clear();
    REQUIRE(session.messageCount() == 0);
    REQUIRE(session.messages().size() == 1); // System prompt remains
    CHECK(session.messages()[0].role == Role::System);
}

TEST_CASE("ChatSession setSystemPrompt replaces prompt", "[chat]")
{
    auto session = ChatSession("old prompt");

    session.addUserMessage("Hello");
    session.setSystemPrompt("new prompt");

    REQUIRE(session.messages().size() == 1);
    CHECK(session.messages()[0].content == "new prompt");
    CHECK(session.systemPrompt() == "new prompt");
}

TEST_CASE("Role conversion roundtrips", "[types]")
{
    CHECK(roleToString(Role::System) == "system");
    CHECK(roleToString(Role::User) == "user");
    CHECK(roleToString(Role::Assistant) == "assistant");
    CHECK(roleToString(Role::Tool) == "tool");

    CHECK(roleFromString("system") == Role::System);
    CHECK(roleFromString("user") == Role::User);
    CHECK(roleFromString("assistant") == Role::Assistant);
    CHECK(roleFromString("tool") == Role::Tool);
    CHECK(roleFromString("unknown") == Role::User); // Default
}
