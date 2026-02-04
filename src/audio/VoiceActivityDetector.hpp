// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <core/Error.hpp>

#include <memory>
#include <span>

namespace mychat
{

/// @brief Voice Activity Detection using Silero-VAD (via whisper.cpp's GGML implementation).
class VoiceActivityDetector
{
  public:
    VoiceActivityDetector();
    ~VoiceActivityDetector();

    VoiceActivityDetector(const VoiceActivityDetector&) = delete;
    VoiceActivityDetector& operator=(const VoiceActivityDetector&) = delete;

    /// @brief Initializes the VAD model.
    /// @param modelPath Path to the Silero-VAD ONNX/GGML model file.
    /// @return Success or an error.
    [[nodiscard]] auto initialize(std::string_view modelPath) -> VoidResult;

    /// @brief Processes audio samples and returns the speech probability.
    /// @param samples Float32 PCM samples at 16kHz mono.
    /// @return Speech probability (0.0 to 1.0) or an error.
    [[nodiscard]] auto process(std::span<const float> samples) -> Result<float>;

    /// @brief Returns true if speech is currently detected (above threshold).
    /// @param probability The speech probability from process().
    /// @param threshold The detection threshold (default: 0.5).
    [[nodiscard]] static auto isSpeech(float probability, float threshold = 0.5f) -> bool;

    /// @brief Resets the VAD state (call between utterances).
    void reset();

  private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
};

} // namespace mychat
