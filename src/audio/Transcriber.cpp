// SPDX-License-Identifier: Apache-2.0
#include "Transcriber.hpp"

#include <core/Log.hpp>

#include <whisper.h>

#include <array>
#include <format>
#include <optional>
#include <string>

namespace mychat
{

namespace
{

    /// @brief Line buffer for whisper.cpp log continuation messages.
    auto whisperLineBuffer = std::string {};

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

    /// @brief Log callback for whisper.cpp that forwards messages to mychat::log.
    ///
    /// Handles continuation lines by buffering partial lines and emitting complete lines
    /// on newline characters.
    /// @param level The ggml log level.
    /// @param text The log message text (may contain trailing newline or be a partial line).
    /// @param userData Unused user data pointer.
    void whisperLogCallback(ggml_log_level level, char const* text, void* /*userData*/)
    {
        if (level == GGML_LOG_LEVEL_NONE || text == nullptr)
            return;

        auto fragment = std::string_view { text };

        // Buffer partial lines
        whisperLineBuffer += fragment;

        // Emit complete lines
        while (true)
        {
            auto const nlPos = whisperLineBuffer.find('\n');
            if (nlPos == std::string::npos)
                break;

            auto line = whisperLineBuffer.substr(0, nlPos);

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

            whisperLineBuffer.erase(0, nlPos + 1);
        }
    }

} // namespace

struct Transcriber::Impl
{
    whisper_context* ctx = nullptr;
    TranscriberConfig config;

    ~Impl()
    {
        if (ctx)
            whisper_free(ctx);
    }
};

Transcriber::Transcriber(): _impl(std::make_unique<Impl>())
{
}

Transcriber::~Transcriber() = default;

auto Transcriber::initialize(const TranscriberConfig& config) -> VoidResult
{
    _impl->config = config;

    whisper_log_set(whisperLogCallback, nullptr);

    auto params = whisper_context_default_params();
    _impl->ctx = whisper_init_from_file_with_params(config.modelPath.c_str(), params);

    if (!_impl->ctx)
        return makeError(ErrorCode::TranscriptionError,
                         std::format("Failed to load whisper model: {}", config.modelPath));

    log::info("Whisper model loaded: {}", config.modelPath);
    return {};
}

auto Transcriber::transcribe(std::span<const float> samples) -> Result<std::string>
{
    if (!_impl->ctx)
        return makeError(ErrorCode::TranscriptionError, "Whisper model not loaded");

    auto params = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    params.language = _impl->config.language.c_str();
    params.translate = _impl->config.translate;
    params.n_threads = _impl->config.threads;
    params.print_progress = false;
    params.print_special = false;
    params.print_realtime = false;
    params.print_timestamps = false;
    params.no_context = true;
    params.single_segment = true;

    auto const result = whisper_full(_impl->ctx, params, samples.data(), static_cast<int>(samples.size()));

    if (result != 0)
        return makeError(ErrorCode::TranscriptionError,
                         std::format("Whisper transcription failed with code: {}", result));

    auto const nSegments = whisper_full_n_segments(_impl->ctx);
    auto text = std::string {};

    for (auto i = 0; i < nSegments; ++i)
    {
        auto const* segmentText = whisper_full_get_segment_text(_impl->ctx, i);
        if (segmentText)
            text += segmentText;
    }

    // Trim whitespace
    auto const start = text.find_first_not_of(" \t\n\r");
    auto const end = text.find_last_not_of(" \t\n\r");
    if (start != std::string::npos)
        text = text.substr(start, end - start + 1);

    // Filter whisper hallucination tokens (e.g. "[BLANK_AUDIO]", "(blank audio)")
    if (text.starts_with('[') || text.starts_with('('))
    {
        static constexpr auto HallucinationPatterns = std::array {
            std::string_view { "[BLANK_AUDIO]" }, std::string_view { "(blank audio)" },
            std::string_view { "[SOUND]" },       std::string_view { "[MUSIC]" },
            std::string_view { "[NOISE]" },
        };
        for (auto const& pattern: HallucinationPatterns)
            if (text == pattern)
                return std::string {};
    }

    return text;
}

auto Transcriber::isLoaded() const -> bool
{
    return _impl->ctx != nullptr;
}

} // namespace mychat
