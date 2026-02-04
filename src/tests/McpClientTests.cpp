// SPDX-License-Identifier: Apache-2.0
#include <mcp/McpClient.hpp>

#include <catch2/catch_test_macros.hpp>

#include <queue>

using namespace mychat;

/// @brief Mock transport for testing McpClient without real processes.
class MockTransport: public Transport
{
  public:
    std::queue<nlohmann::json> responses;
    std::vector<nlohmann::json> sentMessages;

    auto send(const nlohmann::json& message) -> VoidResult override
    {
        sentMessages.push_back(message);
        return {};
    }

    auto receive() -> Result<nlohmann::json> override
    {
        if (responses.empty())
            return makeError(ErrorCode::TransportError, "No more mock responses");
        auto msg = responses.front();
        responses.pop();
        return msg;
    }

    void close() override {}

    auto isConnected() const -> bool override { return true; }

    void queueResponse(nlohmann::json response) { responses.push(std::move(response)); }
};

TEST_CASE("McpClient initialize handshake", "[mcp]")
{
    auto transport = std::make_unique<MockTransport>();
    auto* mock = transport.get();

    mock->queueResponse(nlohmann::json {
        { "jsonrpc", "2.0" },
        { "id", 1 },
        { "result",
          {
              { "protocolVersion", "2024-11-05" },
              { "serverInfo", { { "name", "test-server" }, { "version", "1.0" } } },
              { "capabilities", { { "tools", nlohmann::json::object() } } },
          } },
    });

    auto client = McpClient(std::move(transport));
    auto result = client.initialize();

    REQUIRE(result.has_value());
    CHECK(result->serverName == "test-server");
    CHECK(result->serverVersion == "1.0");
    CHECK(result->hasTools);
    CHECK(!result->hasResources);
    CHECK(client.isInitialized());

    // Verify the initialize request was sent
    REQUIRE(!mock->sentMessages.empty());
    CHECK(mock->sentMessages[0]["method"] == "initialize");
}

TEST_CASE("McpClient listTools", "[mcp]")
{
    auto transport = std::make_unique<MockTransport>();
    auto* mock = transport.get();

    // Queue initialize response
    mock->queueResponse(nlohmann::json {
        { "jsonrpc", "2.0" },
        { "id", 1 },
        { "result",
          {
              { "protocolVersion", "2024-11-05" },
              { "serverInfo", { { "name", "test" }, { "version", "1.0" } } },
              { "capabilities", { { "tools", nlohmann::json::object() } } },
          } },
    });

    // Queue tools/list response
    mock->queueResponse(nlohmann::json {
        { "jsonrpc", "2.0" },
        { "id", 2 },
        { "result",
          { { "tools",
              nlohmann::json::array({
                  { { "name", "read_file" },
                    { "description", "Read a file" },
                    { "inputSchema", nlohmann::json::object() } },
                  { { "name", "write_file" },
                    { "description", "Write a file" },
                    { "inputSchema", nlohmann::json::object() } },
              }) } } },
    });

    auto client = McpClient(std::move(transport));
    auto initResult = client.initialize();
    REQUIRE(initResult.has_value());

    auto toolsResult = client.listTools();
    REQUIRE(toolsResult.has_value());
    REQUIRE(toolsResult->size() == 2);
    CHECK((*toolsResult)[0].name == "read_file");
    CHECK((*toolsResult)[1].name == "write_file");
}

TEST_CASE("McpClient callTool", "[mcp]")
{
    auto transport = std::make_unique<MockTransport>();
    auto* mock = transport.get();

    // Queue initialize response
    mock->queueResponse(nlohmann::json {
        { "jsonrpc", "2.0" },
        { "id", 1 },
        { "result",
          {
              { "protocolVersion", "2024-11-05" },
              { "serverInfo", { { "name", "test" }, { "version", "1.0" } } },
              { "capabilities", { { "tools", nlohmann::json::object() } } },
          } },
    });

    // Queue tool call response
    mock->queueResponse(nlohmann::json {
        { "jsonrpc", "2.0" },
        { "id", 2 },
        { "result",
          {
              { "content", nlohmann::json::array({ { { "type", "text" }, { "text", "Hello World" } } }) },
              { "isError", false },
          } },
    });

    auto client = McpClient(std::move(transport));
    REQUIRE(client.initialize().has_value());

    auto result = client.callTool("read_file", { { "path", "/tmp/test.txt" } });
    REQUIRE(result.has_value());
    CHECK(result->content == "Hello World");
    CHECK(!result->isError);
}

TEST_CASE("McpClient rejects operations before initialization", "[mcp]")
{
    auto transport = std::make_unique<MockTransport>();
    auto client = McpClient(std::move(transport));

    auto toolsResult = client.listTools();
    REQUIRE(!toolsResult.has_value());
    CHECK(toolsResult.error().code == ErrorCode::ProtocolError);

    auto callResult = client.callTool("test", nlohmann::json::object());
    REQUIRE(!callResult.has_value());
    CHECK(callResult.error().code == ErrorCode::ProtocolError);
}
