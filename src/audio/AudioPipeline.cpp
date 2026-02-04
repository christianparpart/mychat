// SPDX-License-Identifier: Apache-2.0
#include "AudioPipeline.hpp"

#include <core/Log.hpp>

#include <mutex>
#include <vector>

namespace mychat
{

struct AudioPipeline::Impl
{
    AudioPipelineConfig config;
    TranscriptionCallback callback;

    AudioCapture capture;
    VoiceActivityDetector vad;
    Transcriber transcriber;

    std::vector<float> audioBuffer;
    std::mutex bufferMutex;

    bool active = false;
    bool recording = false;
    bool speechDetected = false;
    int silenceFrames = 0;
    int silenceThresholdFrames = 0; // Computed from silenceDurationMs

    void handleAudioData(std::span<const float> samples)
    {
        if (!recording)
            return;

        if (config.mode == AudioMode::VoiceActivityDetection)
        {
            auto probability = vad.process(samples);
            if (!probability)
                return;

            if (VoiceActivityDetector::isSpeech(*probability, config.vadThreshold))
            {
                speechDetected = true;
                silenceFrames = 0;

                auto lock = std::lock_guard(bufferMutex);
                audioBuffer.insert(audioBuffer.end(), samples.begin(), samples.end());
            }
            else if (speechDetected)
            {
                ++silenceFrames;

                {
                    auto lock = std::lock_guard(bufferMutex);
                    audioBuffer.insert(audioBuffer.end(), samples.begin(), samples.end());
                }

                if (silenceFrames >= silenceThresholdFrames)
                {
                    speechDetected = false;
                    silenceFrames = 0;

                    auto buffer = std::vector<float> {};
                    {
                        auto lock = std::lock_guard(bufferMutex);
                        buffer = std::move(audioBuffer);
                    }

                    if (!buffer.empty())
                    {
                        auto result = transcriber.transcribe(buffer);
                        if (result && !result->empty())
                        {
                            log::info("VAD transcription: {}", *result);
                            callback(std::move(*result));
                        }
                    }
                }
            }
        }
        else
        {
            auto lock = std::lock_guard(bufferMutex);
            audioBuffer.insert(audioBuffer.end(), samples.begin(), samples.end());
        }
    }
};

AudioPipeline::AudioPipeline(): _impl(std::make_unique<Impl>())
{
}

AudioPipeline::~AudioPipeline()
{
    stop();
}

auto AudioPipeline::initialize(const AudioPipelineConfig& config, TranscriptionCallback callback)
    -> VoidResult
{
    _impl->config = config;
    _impl->callback = std::move(callback);

    constexpr auto samplesPerFrame = 512;
    _impl->silenceThresholdFrames = static_cast<int>((config.silenceDurationMs / 1000.0f) * 16000.0f
                                                     / static_cast<float>(samplesPerFrame));

    auto transConfig = TranscriberConfig {
        .modelPath = config.whisperModelPath,
        .language = config.language,
        .threads = config.transcriptionThreads,
    };

    auto transResult = _impl->transcriber.initialize(transConfig);
    if (!transResult)
        return transResult;

    if (config.mode == AudioMode::VoiceActivityDetection)
    {
        auto vadResult = _impl->vad.initialize(config.vadModelPath);
        if (!vadResult)
            return vadResult;
    }

    auto captureCallback = [this](std::span<const float> samples) {
        _impl->handleAudioData(samples);
    };

    auto captureResult = _impl->capture.initialize(std::move(captureCallback), config.deviceName);
    if (!captureResult)
        return captureResult;

    log::info("Audio pipeline initialized (mode: {})",
              config.mode == AudioMode::PushToTalk ? "push-to-talk" : "vad");
    return {};
}

auto AudioPipeline::start() -> VoidResult
{
    if (_impl->active)
        return {};

    auto result = _impl->capture.start();
    if (!result)
        return result;

    _impl->active = true;

    if (_impl->config.mode == AudioMode::VoiceActivityDetection)
        _impl->recording = true;

    return {};
}

void AudioPipeline::stop()
{
    if (!_impl->active)
        return;

    _impl->capture.stop();
    _impl->active = false;
    _impl->recording = false;
}

void AudioPipeline::startRecording()
{
    if (_impl->config.mode != AudioMode::PushToTalk)
        return;

    auto lock = std::lock_guard(_impl->bufferMutex);
    _impl->audioBuffer.clear();
    _impl->recording = true;
    log::debug("Push-to-talk: recording started");
}

void AudioPipeline::stopRecording()
{
    if (_impl->config.mode != AudioMode::PushToTalk || !_impl->recording)
        return;

    _impl->recording = false;

    auto buffer = std::vector<float> {};
    {
        auto lock = std::lock_guard(_impl->bufferMutex);
        buffer = std::move(_impl->audioBuffer);
    }

    if (buffer.empty())
        return;

    log::debug("Push-to-talk: transcribing {} samples", buffer.size());
    auto result = _impl->transcriber.transcribe(buffer);
    if (result && !result->empty())
    {
        log::info("Transcription: {}", *result);
        _impl->callback(std::move(*result));
    }
    else if (!result)
    {
        log::error("Transcription failed: {}", result.error().message);
    }
}

void AudioPipeline::pauseRecording()
{
    auto lock = std::lock_guard(_impl->bufferMutex);
    _impl->recording = false;
    _impl->audioBuffer.clear();
    _impl->speechDetected = false;
    _impl->silenceFrames = 0;
    log::debug("Audio pipeline: recording paused");
}

void AudioPipeline::resumeRecording()
{
    if (!_impl->active)
        return;

    auto lock = std::lock_guard(_impl->bufferMutex);
    _impl->audioBuffer.clear();
    _impl->speechDetected = false;
    _impl->silenceFrames = 0;
    _impl->recording = true;
    log::debug("Audio pipeline: recording resumed");
}

auto AudioPipeline::isActive() const -> bool
{
    return _impl->active;
}

auto AudioPipeline::peakLevel() const -> float
{
    return _impl->capture.peakLevel();
}

} // namespace mychat
