// SPDX-License-Identifier: Apache-2.0
#include "JsonRpc.hpp"

#include <format>

namespace mychat::jsonrpc
{

auto makeRequest(int64_t id, std::string_view method, nlohmann::json params) -> nlohmann::json
{
    auto msg = nlohmann::json {
        { "jsonrpc", "2.0" },
        { "id", id },
        { "method", method },
    };

    if (!params.is_null())
        msg["params"] = std::move(params);

    return msg;
}

auto makeNotification(std::string_view method, nlohmann::json params) -> nlohmann::json
{
    auto msg = nlohmann::json {
        { "jsonrpc", "2.0" },
        { "method", method },
    };

    if (!params.is_null())
        msg["params"] = std::move(params);

    return msg;
}

auto parseResponse(const nlohmann::json& message) -> Result<Response>
{
    if (!message.contains("jsonrpc") || message["jsonrpc"] != "2.0")
        return makeError(ErrorCode::ProtocolError, "Not a valid JSON-RPC 2.0 message");

    auto response = Response {};

    if (message.contains("id"))
        response.id = message["id"];

    if (message.contains("result"))
    {
        response.result = message["result"];
    }
    else if (message.contains("error"))
    {
        auto const& err = message["error"];
        response.error = RpcError {
            .code = err.value("code", 0),
            .message = err.value("message", "Unknown error"),
            .data = err.value("data", nlohmann::json {}),
        };
    }
    else if (!message.contains("method"))
    {
        // It's neither a valid response nor a notification
        return makeError(ErrorCode::ProtocolError, "JSON-RPC message has neither result, error, nor method");
    }

    return response;
}

} // namespace mychat::jsonrpc
