// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <core/Error.hpp>

#include <memory>
#include <span>
#include <string>

namespace mychat
{

/// @brief Configuration for the whisper.cpp transcriber.
struct TranscriberConfig
{
    std::string modelPath;
    std::string language = "en";
    int threads = 4;
    bool translate = false;
};

/// @brief Speech-to-text transcription using whisper.cpp.
class Transcriber
{
  public:
    Transcriber();
    ~Transcriber();

    Transcriber(const Transcriber&) = delete;
    Transcriber& operator=(const Transcriber&) = delete;

    /// @brief Loads the whisper model.
    /// @param config Transcriber configuration.
    /// @return Success or an error.
    [[nodiscard]] auto initialize(const TranscriberConfig& config) -> VoidResult;

    /// @brief Transcribes audio samples to text.
    /// @param samples Float32 PCM audio at 16kHz mono.
    /// @return The transcribed text or an error.
    [[nodiscard]] auto transcribe(std::span<const float> samples) -> Result<std::string>;

    /// @brief Returns true if the model is loaded.
    [[nodiscard]] auto isLoaded() const -> bool;

  private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
};

} // namespace mychat
