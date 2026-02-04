// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <core/Error.hpp>

#include <memory>
#include <span>

namespace mychat
{

/// @brief Plays raw PCM audio through the default playback device using miniaudio.
///
/// Uses PIMPL to isolate miniaudio headers from consumers. Playback is blocking:
/// play() returns once all samples have been consumed by the audio device.
class AudioPlayback
{
  public:
    AudioPlayback();
    ~AudioPlayback();

    AudioPlayback(const AudioPlayback&) = delete;
    AudioPlayback& operator=(const AudioPlayback&) = delete;

    /// @brief Initializes the playback device.
    /// @param sampleRate Audio sample rate in Hz (e.g. 22050).
    /// @param channels Number of audio channels (e.g. 1 for mono).
    /// @return Success or an error.
    [[nodiscard]] auto initialize(unsigned sampleRate, unsigned channels) -> VoidResult;

    /// @brief Plays raw PCM audio, blocking until all samples are consumed.
    /// @param samples The float32 PCM samples to play.
    /// @return Success or an error.
    [[nodiscard]] auto play(std::span<const float> samples) -> VoidResult;

    /// @brief Cancels the current playback immediately.
    void stop();

    struct Impl;

  private:
    std::unique_ptr<Impl> _impl;
};

} // namespace mychat
