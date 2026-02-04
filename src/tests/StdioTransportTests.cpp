// SPDX-License-Identifier: Apache-2.0
#include <mcp/StdioTransport.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace mychat;

TEST_CASE("StdioTransport starts disconnected", "[transport]")
{
    auto transport = StdioTransport();
    CHECK(!transport.isConnected());
}

TEST_CASE("StdioTransport send fails when not connected", "[transport]")
{
    auto transport = StdioTransport();
    auto result = transport.send(nlohmann::json { { "test", true } });
    REQUIRE(!result.has_value());
    CHECK(result.error().code == ErrorCode::TransportError);
}

TEST_CASE("StdioTransport receive fails when not connected", "[transport]")
{
    auto transport = StdioTransport();
    auto result = transport.receive();
    REQUIRE(!result.has_value());
    CHECK(result.error().code == ErrorCode::TransportError);
}

#ifndef _WIN32
TEST_CASE("StdioTransport can spawn and communicate with a simple process", "[transport]")
{
    auto transport = StdioTransport();

    auto config = StdioTransportConfig {
        .command = "cat",
        .args = {},
        .env = {},
    };

    auto startResult = transport.start(config);
    REQUIRE(startResult.has_value());
    CHECK(transport.isConnected());

    // Send a JSON message
    auto msg = nlohmann::json { { "test", "hello" } };
    auto sendResult = transport.send(msg);
    REQUIRE(sendResult.has_value());

    // Read it back (cat echoes stdin to stdout)
    auto recvResult = transport.receive();
    REQUIRE(recvResult.has_value());
    CHECK((*recvResult)["test"] == "hello");

    transport.close();
    CHECK(!transport.isConnected());
}
#endif

TEST_CASE("StdioTransport fails to start invalid command", "[transport]")
{
    auto transport = StdioTransport();

    auto config = StdioTransportConfig {
        .command = "/nonexistent/command/that/does/not/exist",
        .args = {},
        .env = {},
    };

    auto result = transport.start(config);
    // posix_spawnp may succeed even for non-existent commands on some systems,
    // but the process will fail immediately. We check that either start fails
    // or subsequent operations fail.
    if (result.has_value())
    {
        // If spawn succeeded, the process should have died
        auto recvResult = transport.receive();
        CHECK(!recvResult.has_value());
    }
    else
    {
        CHECK(result.error().code == ErrorCode::TransportError);
    }
}
