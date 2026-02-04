// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <core/Error.hpp>

#include <functional>
#include <memory>
#include <span>
#include <string_view>

namespace mychat
{

/// @brief Callback invoked when audio data is captured.
/// @param samples Float32 PCM samples at 16kHz mono.
using AudioCallback = std::function<void(std::span<const float> samples)>;

/// @brief Captures audio from the microphone using miniaudio.
///
/// Captures float32 PCM audio at 16kHz mono, suitable for speech recognition.
class AudioCapture
{
  public:
    AudioCapture();
    ~AudioCapture();

    AudioCapture(const AudioCapture&) = delete;
    AudioCapture& operator=(const AudioCapture&) = delete;

    /// @brief Initializes the audio capture device.
    /// @param callback Called with audio chunks from the capture thread.
    /// @param deviceName Optional substring to match against capture device names (case-insensitive).
    ///                   If empty, the system default capture device is used.
    /// @return Success or an error.
    [[nodiscard]] auto initialize(AudioCallback callback, std::string_view deviceName = {}) -> VoidResult;

    /// @brief Starts capturing audio.
    /// @return Success or an error.
    [[nodiscard]] auto start() -> VoidResult;

    /// @brief Stops capturing audio.
    void stop();

    /// @brief Returns true if currently capturing.
    [[nodiscard]] auto isCapturing() const -> bool;

    /// @brief Returns the current peak audio level (0.0 to 1.0).
    ///
    /// Updated atomically from the audio callback thread. Safe to call from any thread.
    [[nodiscard]] auto peakLevel() const -> float;

    // Impl must be accessible from the C audio callback
    struct Impl;

  private:
    std::unique_ptr<Impl> _impl;
};

} // namespace mychat
