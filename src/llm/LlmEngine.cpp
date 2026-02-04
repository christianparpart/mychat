// SPDX-License-Identifier: Apache-2.0
#include "LlmEngine.hpp"

#include <core/Log.hpp>

#include <llama.h>

#include <algorithm>
#include <array>
#include <format>
#include <optional>
#include <ranges>
#include <string>
#include <thread>

namespace mychat
{

struct LlmEngine::Impl
{
    llama_model* model = nullptr;
    llama_context* ctx = nullptr;
    int ctxSize = 0;

    ~Impl()
    {
        if (ctx)
            llama_free(ctx);
        if (model)
            llama_model_free(model);
    }
};

namespace
{

    /// @brief Line buffer for llama.cpp log continuation messages.
    auto llamaLineBuffer = std::string {};

    /// @brief Maps ggml_log_level to mychat::log::Level.
    /// @param level The ggml log level.
    /// @return The corresponding mychat log level, or std::nullopt for GGML_LOG_LEVEL_NONE.
    auto mapGgmlLevel(ggml_log_level level) -> std::optional<log::Level>
    {
        switch (level)
        {
            case GGML_LOG_LEVEL_ERROR: return log::Level::Error;
            case GGML_LOG_LEVEL_WARN: return log::Level::Warning;
            case GGML_LOG_LEVEL_INFO: return log::Level::Info;
            case GGML_LOG_LEVEL_DEBUG: return log::Level::Debug;
            default: return std::nullopt;
        }
    }

    /// @brief Log callback for llama.cpp that forwards messages to mychat::log.
    ///
    /// Handles continuation lines (GGML_LOG_LEVEL_CONT) by buffering partial lines
    /// and emitting complete lines on newline characters.
    /// @param level The ggml log level.
    /// @param text The log message text (may contain trailing newline or be a partial line).
    /// @param userData Unused user data pointer.
    void llamaLogCallback(ggml_log_level level, char const* text, void* /*userData*/)
    {
        if (level == GGML_LOG_LEVEL_NONE || text == nullptr)
            return;

        auto fragment = std::string_view { text };

        // Buffer partial lines (GGML_LOG_LEVEL_CONT or lines without trailing newline)
        llamaLineBuffer += fragment;

        // Emit complete lines
        while (true)
        {
            auto const nlPos = llamaLineBuffer.find('\n');
            if (nlPos == std::string::npos)
                break;

            auto line = llamaLineBuffer.substr(0, nlPos);

            // Strip trailing whitespace
            auto const end = line.find_last_not_of(" \t\r");
            if (end != std::string::npos)
                line = line.substr(0, end + 1);

            // Skip empty lines
            if (!line.empty())
            {
                auto const logLevel = mapGgmlLevel(level).value_or(log::Level::Info);
                log::write(logLevel, line);
            }

            llamaLineBuffer.erase(0, nlPos + 1);
        }
    }

    /// @brief Attempts to parse tool calls from generated text.
    ///
    /// Supports common formats used by chat models (e.g., Llama 3, Qwen, Mistral).
    /// Tool calls are typically enclosed in special tags or JSON blocks.
    void parseToolCalls(GenerateResult& result, std::span<const ToolDefinition> tools)
    {
        auto const& text = result.text;

        auto const toolCallStart = std::string_view("<tool_call>");
        auto const toolCallEnd = std::string_view("</tool_call>");

        auto pos = text.find(toolCallStart);
        while (pos != std::string::npos)
        {
            auto const start = pos + toolCallStart.size();
            auto const end = text.find(toolCallEnd, start);
            if (end == std::string::npos)
                break;

            auto const jsonStr = text.substr(start, end - start);
            try
            {
                auto const json = nlohmann::json::parse(jsonStr);
                auto call = ToolCall {};
                call.name = json.value("name", "");
                call.arguments = json.value("arguments", nlohmann::json::object());
                call.id = std::format("call_{}", result.toolCalls.size());

                auto const found =
                    std::ranges::any_of(tools, [&](const auto& t) { return t.name == call.name; });
                if (found)
                    result.toolCalls.push_back(std::move(call));
            }
            catch (...)
            {
                // Skip malformed tool calls
            }

            pos = text.find(toolCallStart, end + toolCallEnd.size());
        }

        if (!result.toolCalls.empty())
        {
            auto cleaned = std::string {};
            auto searchPos = size_t { 0 };
            while (true)
            {
                auto const tcStart = text.find(toolCallStart, searchPos);
                if (tcStart == std::string::npos)
                {
                    cleaned += text.substr(searchPos);
                    break;
                }
                cleaned += text.substr(searchPos, tcStart - searchPos);
                auto const tcEnd = text.find(toolCallEnd, tcStart);
                if (tcEnd == std::string::npos)
                {
                    cleaned += text.substr(tcStart);
                    break;
                }
                searchPos = tcEnd + toolCallEnd.size();
            }
            result.text = std::move(cleaned);
        }
    }

} // namespace

LlmEngine::LlmEngine(): _impl(std::make_unique<Impl>())
{
}

LlmEngine::~LlmEngine() = default;

LlmEngine::LlmEngine(LlmEngine&&) noexcept = default;

LlmEngine& LlmEngine::operator=(LlmEngine&&) noexcept = default;

auto LlmEngine::load(const LlmEngineConfig& config) -> VoidResult
{
    log::info("Loading model: {}", config.modelPath);

    llama_log_set(llamaLogCallback, nullptr);

    auto modelParams = llama_model_default_params();
    if (config.gpuLayers >= 0)
        modelParams.n_gpu_layers = config.gpuLayers;
    else
        modelParams.n_gpu_layers = 999; // Auto: offload as many as possible

    auto* model = llama_model_load_from_file(config.modelPath.c_str(), modelParams);
    if (!model)
        return makeError(ErrorCode::ModelLoadError,
                         std::format("Failed to load model: {}", config.modelPath));

    auto ctxParams = llama_context_default_params();
    ctxParams.n_ctx = static_cast<uint32_t>(config.contextSize);
    ctxParams.n_threads = config.threads > 0 ? static_cast<uint32_t>(config.threads)
                                             : static_cast<uint32_t>(std::thread::hardware_concurrency());
    ctxParams.n_threads_batch = ctxParams.n_threads;

    auto* ctx = llama_init_from_model(model, ctxParams);
    if (!ctx)
    {
        llama_model_free(model);
        return makeError(ErrorCode::ModelLoadError, "Failed to create llama context");
    }

    _impl->model = model;
    _impl->ctx = ctx;
    _impl->ctxSize = config.contextSize;

    log::info("Model loaded successfully (context size: {})", config.contextSize);
    return {};
}

auto LlmEngine::generate(std::span<const ChatMessage> messages,
                         std::span<const ToolDefinition> tools,
                         const SamplerConfig& sampler,
                         StreamCallback streamCb) -> Result<GenerateResult>
{
    if (!isLoaded())
        return makeError(ErrorCode::InferenceError, "No model loaded");

    // Build the prompt using llama_chat_apply_template
    auto const* tmpl = llama_model_chat_template(_impl->model, nullptr);
    auto chatTemplate = tmpl ? std::string(tmpl) : std::string("chatml");

    // Convert messages to llama_chat_message format
    auto llamaMsgs = std::vector<llama_chat_message> {};
    llamaMsgs.reserve(messages.size());

    // We need to keep strings alive
    auto roleStrings = std::vector<std::string> {};
    roleStrings.reserve(messages.size());

    for (const auto& msg: messages)
    {
        roleStrings.emplace_back(roleToString(msg.role));
        llamaMsgs.push_back(llama_chat_message {
            .role = roleStrings.back().c_str(),
            .content = msg.content.c_str(),
        });
    }

    // Apply the chat template
    auto buf = std::vector<char>(static_cast<size_t>(_impl->ctxSize) * 4);
    auto const len = llama_chat_apply_template(chatTemplate.c_str(),
                                               llamaMsgs.data(),
                                               llamaMsgs.size(),
                                               true,
                                               buf.data(),
                                               static_cast<int32_t>(buf.size()));

    if (len < 0)
        return makeError(ErrorCode::InferenceError, "Failed to apply chat template");

    auto prompt = std::string(buf.data(), static_cast<size_t>(len));

    // Tokenize
    auto const vocabModel = llama_model_get_vocab(_impl->model);
    auto tokens = std::vector<llama_token>(static_cast<size_t>(_impl->ctxSize));
    auto const nTokens = llama_tokenize(vocabModel,
                                        prompt.c_str(),
                                        static_cast<int32_t>(prompt.size()),
                                        tokens.data(),
                                        static_cast<int32_t>(tokens.size()),
                                        true,
                                        true);

    if (nTokens < 0)
        return makeError(ErrorCode::InferenceError, "Tokenization failed");
    tokens.resize(static_cast<size_t>(nTokens));

    // Clear KV cache and decode the prompt
    auto* mem = llama_get_memory(_impl->ctx);
    if (mem)
        llama_memory_clear(mem, true);

    auto batch = llama_batch_get_one(tokens.data(), nTokens);
    if (llama_decode(_impl->ctx, batch) != 0)
        return makeError(ErrorCode::InferenceError, "Failed to decode prompt");

    // Set up sampler chain
    auto* smpl = llama_sampler_chain_init(llama_sampler_chain_default_params());
    llama_sampler_chain_add(smpl, llama_sampler_init_temp(sampler.temperature));
    llama_sampler_chain_add(smpl, llama_sampler_init_top_k(sampler.topK));
    llama_sampler_chain_add(smpl, llama_sampler_init_top_p(sampler.topP, 1));
    llama_sampler_chain_add(
        smpl,
        llama_sampler_init_dist(sampler.seed < 0 ? LLAMA_DEFAULT_SEED : static_cast<uint32_t>(sampler.seed)));

    // Generate tokens
    auto result = GenerateResult {};
    auto const maxTokens = _impl->ctxSize - nTokens;

    for (auto i = 0; i < maxTokens; ++i)
    {
        auto const newTokenId = llama_sampler_sample(smpl, _impl->ctx, -1);

        if (llama_vocab_is_eog(vocabModel, newTokenId))
            break;

        auto tokenBuf = std::array<char, 256> {};
        auto const tokenLen = llama_token_to_piece(
            vocabModel, newTokenId, tokenBuf.data(), static_cast<int32_t>(tokenBuf.size()), 0, true);

        if (tokenLen > 0)
        {
            auto piece = std::string_view(tokenBuf.data(), static_cast<size_t>(tokenLen));
            result.text += piece;
            if (streamCb)
                streamCb(piece);
        }

        // llama_batch_get_one requires non-const pointer
        auto mutableTokenId = newTokenId;
        auto singleTokenBatch = llama_batch_get_one(&mutableTokenId, 1);
        if (llama_decode(_impl->ctx, singleTokenBatch) != 0)
        {
            llama_sampler_free(smpl);
            return makeError(ErrorCode::InferenceError, "Failed to decode generated token");
        }
    }

    llama_sampler_free(smpl);

    // Parse tool calls from the generated text if tools were provided
    if (!tools.empty())
        parseToolCalls(result, tools);

    return result;
}

auto LlmEngine::isLoaded() const -> bool
{
    return _impl->model != nullptr && _impl->ctx != nullptr;
}

auto LlmEngine::contextSize() const -> int
{
    return _impl->ctxSize;
}

} // namespace mychat
