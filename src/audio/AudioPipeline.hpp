// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <audio/AudioCapture.hpp>
#include <audio/Transcriber.hpp>
#include <audio/VoiceActivityDetector.hpp>
#include <core/Error.hpp>

#include <functional>
#include <memory>
#include <string>

namespace mychat
{

/// @brief Callback invoked when speech has been transcribed.
using TranscriptionCallback = std::function<void(std::string text)>;

/// @brief Voice input mode.
enum class AudioMode : std::uint8_t
{
    PushToTalk,
    VoiceActivityDetection,
};

/// @brief Configuration for the audio pipeline.
struct AudioPipelineConfig
{
    std::string whisperModelPath;
    std::string vadModelPath;
    std::string language = "en";
    std::string deviceName;
    AudioMode mode = AudioMode::PushToTalk;
    float vadThreshold = 0.5f;
    float silenceDurationMs = 500.0f;
    int transcriptionThreads = 4;
};

/// @brief Orchestrates audio capture, VAD, and transcription.
///
/// In push-to-talk mode, the user explicitly starts/stops recording.
/// In VAD mode, speech is automatically detected and transcribed.
class AudioPipeline
{
  public:
    AudioPipeline();
    ~AudioPipeline();

    AudioPipeline(const AudioPipeline&) = delete;
    AudioPipeline& operator=(const AudioPipeline&) = delete;

    /// @brief Initializes the audio pipeline components.
    /// @param config Pipeline configuration.
    /// @param callback Called when a transcription is ready.
    /// @return Success or an error.
    [[nodiscard]] auto initialize(const AudioPipelineConfig& config, TranscriptionCallback callback)
        -> VoidResult;

    /// @brief Starts the audio pipeline.
    /// @return Success or an error.
    [[nodiscard]] auto start() -> VoidResult;

    /// @brief Stops the audio pipeline.
    void stop();

    /// @brief In push-to-talk mode, starts recording.
    void startRecording();

    /// @brief In push-to-talk mode, stops recording and triggers transcription.
    void stopRecording();

    /// @brief Pauses audio processing without stopping the capture device.
    ///
    /// Incoming audio data is silently discarded. VAD state and buffers are cleared
    /// so that stale audio is not processed when recording resumes.
    void pauseRecording();

    /// @brief Resumes audio processing after a pause.
    void resumeRecording();

    /// @brief Returns true if the pipeline is active.
    [[nodiscard]] auto isActive() const -> bool;

    /// @brief Returns the current peak audio level (0.0 to 1.0).
    ///
    /// Delegates to AudioCapture. Safe to call from any thread.
    [[nodiscard]] auto peakLevel() const -> float;

  private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
};

} // namespace mychat
