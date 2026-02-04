// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <core/Error.hpp>
#include <core/Types.hpp>
#include <llm/Sampler.hpp>

#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

struct llama_model;
struct llama_context;

namespace mychat
{

/// @brief Callback invoked for each generated token during streaming.
using StreamCallback = std::function<void(std::string_view token)>;

/// @brief Configuration for the LLM engine.
struct LlmEngineConfig
{
    std::string modelPath;
    int contextSize = 8192;
    int gpuLayers = -1; // -1 means auto
    int threads = 0;    // 0 means auto
};

/// @brief Wraps llama.cpp for LLM inference with streaming and tool call support.
class LlmEngine
{
  public:
    LlmEngine();
    ~LlmEngine();

    LlmEngine(const LlmEngine&) = delete;
    LlmEngine& operator=(const LlmEngine&) = delete;
    LlmEngine(LlmEngine&&) noexcept;
    LlmEngine& operator=(LlmEngine&&) noexcept;

    /// @brief Loads a GGUF model from disk.
    /// @param config The engine configuration including model path.
    /// @return Success or an error.
    [[nodiscard]] auto load(const LlmEngineConfig& config) -> VoidResult;

    /// @brief Generates a response given conversation messages and available tools.
    /// @param messages The conversation history.
    /// @param tools Available tool definitions (empty if none).
    /// @param sampler Sampling configuration.
    /// @param streamCb Optional callback for streaming tokens.
    /// @return The generation result containing text and/or tool calls.
    [[nodiscard]] auto generate(std::span<const ChatMessage> messages,
                                std::span<const ToolDefinition> tools,
                                const SamplerConfig& sampler,
                                StreamCallback streamCb = {}) -> Result<GenerateResult>;

    /// @brief Returns true if a model is currently loaded.
    [[nodiscard]] auto isLoaded() const -> bool;

    /// @brief Returns the model's context size.
    [[nodiscard]] auto contextSize() const -> int;

  private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
};

} // namespace mychat
