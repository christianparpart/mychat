// SPDX-License-Identifier: Apache-2.0
#include <mcp/JsonRpc.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace mychat;

TEST_CASE("makeRequest creates valid JSON-RPC 2.0 request", "[jsonrpc]")
{
    auto request = jsonrpc::makeRequest(1, "test/method");

    CHECK(request["jsonrpc"] == "2.0");
    CHECK(request["id"] == 1);
    CHECK(request["method"] == "test/method");
    CHECK(!request.contains("params"));
}

TEST_CASE("makeRequest includes params when provided", "[jsonrpc]")
{
    auto params = nlohmann::json { { "key", "value" } };
    auto request = jsonrpc::makeRequest(42, "test/method", params);

    CHECK(request["jsonrpc"] == "2.0");
    CHECK(request["id"] == 42);
    CHECK(request["method"] == "test/method");
    REQUIRE(request.contains("params"));
    CHECK(request["params"]["key"] == "value");
}

TEST_CASE("makeNotification creates valid notification (no id)", "[jsonrpc]")
{
    auto notif = jsonrpc::makeNotification("test/notify");

    CHECK(notif["jsonrpc"] == "2.0");
    CHECK(!notif.contains("id"));
    CHECK(notif["method"] == "test/notify");
}

TEST_CASE("parseResponse handles success response", "[jsonrpc]")
{
    auto msg = nlohmann::json {
        { "jsonrpc", "2.0" },
        { "id", 1 },
        { "result", { { "status", "ok" } } },
    };

    auto result = jsonrpc::parseResponse(msg);
    REQUIRE(result.has_value());
    CHECK(result->isSuccess());
    CHECK(result->result->at("status") == "ok");
    CHECK(!result->error.has_value());
}

TEST_CASE("parseResponse handles error response", "[jsonrpc]")
{
    auto msg = nlohmann::json {
        { "jsonrpc", "2.0" },
        { "id", 1 },
        { "error",
          {
              { "code", -32600 },
              { "message", "Invalid Request" },
          } },
    };

    auto result = jsonrpc::parseResponse(msg);
    REQUIRE(result.has_value());
    CHECK(!result->isSuccess());
    REQUIRE(result->error.has_value());
    CHECK(result->error->code == -32600);
    CHECK(result->error->message == "Invalid Request");
}

TEST_CASE("parseResponse rejects non-JSON-RPC messages", "[jsonrpc]")
{
    auto msg = nlohmann::json { { "version", "1.0" } };
    auto result = jsonrpc::parseResponse(msg);
    REQUIRE(!result.has_value());
    CHECK(result.error().code == ErrorCode::ProtocolError);
}

TEST_CASE("parseResponse handles response without result or error", "[jsonrpc]")
{
    auto msg = nlohmann::json {
        { "jsonrpc", "2.0" },
        { "id", 1 },
    };

    auto result = jsonrpc::parseResponse(msg);
    REQUIRE(!result.has_value());
    CHECK(result.error().code == ErrorCode::ProtocolError);
}
