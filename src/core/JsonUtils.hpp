// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <string_view>

#include "Error.hpp"

namespace mychat::json
{

/// @brief Parses a JSON string, returning a Result.
/// @param input The JSON string to parse.
/// @return The parsed JSON object or an Error.
[[nodiscard]] inline auto parse(std::string_view input) -> Result<nlohmann::json>
{
    try
    {
        return nlohmann::json::parse(input);
    }
    catch (const nlohmann::json::parse_error& e)
    {
        return makeError(ErrorCode::ProtocolError, std::format("JSON parse error: {}", e.what()));
    }
}

/// @brief Extracts a required string field from a JSON object.
/// @param obj The JSON object.
/// @param key The field name.
/// @return The string value or an Error.
[[nodiscard]] inline auto getString(const nlohmann::json& obj, std::string_view key) -> Result<std::string>
{
    auto keyStr = std::string(key);
    if (!obj.contains(keyStr) || !obj[keyStr].is_string())
        return makeError(ErrorCode::ProtocolError, std::format("Missing or invalid string field: {}", key));
    return obj[keyStr].get<std::string>();
}

/// @brief Extracts an optional string field from a JSON object.
/// @param obj The JSON object.
/// @param key The field name.
/// @param defaultValue The value to return if the field is missing.
/// @return The string value or the default.
[[nodiscard]] inline auto getStringOr(const nlohmann::json& obj,
                                      std::string_view key,
                                      std::string_view defaultValue) -> std::string
{
    auto keyStr = std::string(key);
    if (obj.contains(keyStr) && obj[keyStr].is_string())
        return obj[keyStr].get<std::string>();
    return std::string(defaultValue);
}

/// @brief Extracts an optional integer field from a JSON object.
/// @param obj The JSON object.
/// @param key The field name.
/// @param defaultValue The value to return if the field is missing.
/// @return The integer value or the default.
[[nodiscard]] inline auto getIntOr(const nlohmann::json& obj, std::string_view key, int defaultValue) -> int
{
    auto keyStr = std::string(key);
    if (obj.contains(keyStr) && obj[keyStr].is_number_integer())
        return obj[keyStr].get<int>();
    return defaultValue;
}

/// @brief Extracts an optional float field from a JSON object.
/// @param obj The JSON object.
/// @param key The field name.
/// @param defaultValue The value to return if the field is missing.
/// @return The float value or the default.
[[nodiscard]] inline auto getFloatOr(const nlohmann::json& obj, std::string_view key, float defaultValue)
    -> float
{
    auto keyStr = std::string(key);
    if (obj.contains(keyStr) && obj[keyStr].is_number())
        return obj[keyStr].get<float>();
    return defaultValue;
}

/// @brief Extracts an optional boolean field from a JSON object.
/// @param obj The JSON object.
/// @param key The field name.
/// @param defaultValue The value to return if the field is missing.
/// @return The boolean value or the default.
[[nodiscard]] inline auto getBoolOr(const nlohmann::json& obj, std::string_view key, bool defaultValue)
    -> bool
{
    auto keyStr = std::string(key);
    if (obj.contains(keyStr) && obj[keyStr].is_boolean())
        return obj[keyStr].get<bool>();
    return defaultValue;
}

} // namespace mychat::json
