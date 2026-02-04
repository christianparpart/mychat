// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <core/Error.hpp>

#include <memory>
#include <string>

namespace mychat
{

/// @brief Configuration for the TTS speaker.
struct TtsSpeakerConfig
{
    /// @brief Path to the piper voice model (.onnx file).
    std::string modelPath;

    /// @brief Path to the espeak-ng-data directory (defaults to built-in).
    std::string espeakDataPath;
};

/// @brief Synthesizes speech using the piper library (linked at build time).
///
/// Maintains a background worker thread that dequeues sentences,
/// synthesizes audio via the piper C API, and plays it through AudioPlayback.
class TtsSpeaker
{
  public:
    TtsSpeaker();
    ~TtsSpeaker();

    TtsSpeaker(const TtsSpeaker&) = delete;
    TtsSpeaker& operator=(const TtsSpeaker&) = delete;

    /// @brief Initializes the speaker by creating the piper synthesizer and setting up audio playback.
    /// @param config The TTS configuration.
    /// @return Success or an error.
    [[nodiscard]] auto initialize(const TtsSpeakerConfig& config) -> VoidResult;

    /// @brief Enqueues a sentence for synthesis and playback (non-blocking).
    /// @param text The text to speak.
    void speak(std::string text);

    /// @brief Blocks until all queued sentences have been spoken.
    void flush();

    /// @brief Returns true when no sentences are queued and nothing is being synthesized or played.
    [[nodiscard]] auto idle() const -> bool;

    /// @brief Cancels any active synthesis and clears the queue.
    void cancel();

    /// @brief Cancels pending work and joins the worker thread.
    void shutdown();

    struct Impl;

  private:
    std::unique_ptr<Impl> _impl;
};

} // namespace mychat
