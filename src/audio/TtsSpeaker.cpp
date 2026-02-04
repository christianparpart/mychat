// SPDX-License-Identifier: Apache-2.0
#include "TtsSpeaker.hpp"

#include <core/Log.hpp>

#include <condition_variable>
#include <deque>
#include <format>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "AudioPlayback.hpp"

extern "C"
{
#include <piper.h>
}

namespace mychat
{

namespace
{

    /// @brief Piper output format: float32 PCM, 22050 Hz, mono.
    constexpr auto PiperSampleRate = 22050u;
    constexpr auto PiperChannels = 1u;

} // namespace

struct TtsSpeaker::Impl
{
    TtsSpeakerConfig config;
    AudioPlayback playback;
    piper_synthesizer* synth = nullptr;

    std::jthread worker;
    std::mutex mutex;
    std::condition_variable_any cv;
    std::deque<std::string> queue;
    bool shutdownRequested = false;
    bool flushRequested = false;
    bool busy = false;

    ~Impl()
    {
        if (synth)
            piper_free(synth);
    }

    /// @brief Worker thread function that processes the sentence queue.
    /// @param stopToken The stop token for cooperative cancellation.
    void run(const std::stop_token& stopToken)
    {
        while (!stopToken.stop_requested())
        {
            auto sentence = std::string {};
            {
                auto lock = std::unique_lock(mutex);
                cv.wait(lock, stopToken, [this] { return !queue.empty() || shutdownRequested; });

                if (stopToken.stop_requested() || shutdownRequested)
                {
                    // Notify flush waiters before exiting
                    flushRequested = false;
                    cv.notify_all();
                    return;
                }

                if (queue.empty())
                    continue;

                sentence = std::move(queue.front());
                queue.pop_front();
                busy = true;
            }

            synthesizeAndPlay(sentence);

            // If queue is now empty and a flush is pending, notify the flusher
            {
                auto lock = std::lock_guard(mutex);
                busy = false;
                if (queue.empty() && flushRequested)
                {
                    flushRequested = false;
                    cv.notify_all();
                }
            }
        }
    }

    /// @brief Synthesizes speech for a single sentence and plays it via the piper C API.
    /// @param text The text to synthesize.
    void synthesizeAndPlay(const std::string& text)
    {
        auto opts = piper_default_synthesize_options(synth);
        auto const startResult = piper_synthesize_start(synth, text.c_str(), &opts);
        if (startResult != 0)
        {
            log::error("TTS: piper_synthesize_start failed ({})", startResult);
            return;
        }

        auto audioData = std::vector<float> {};
        auto chunk = piper_audio_chunk {};

        while (true)
        {
            auto const rc = piper_synthesize_next(synth, &chunk);
            if (rc == 1) // PIPER_DONE
                break;
            if (rc < 0) // PIPER_ERR_GENERIC
            {
                log::error("TTS: piper_synthesize_next failed ({})", rc);
                break;
            }
            // rc == 0: PIPER_OK
            audioData.insert(audioData.end(), chunk.samples, chunk.samples + chunk.num_samples);
        }

        if (!audioData.empty())
        {
            auto result = playback.play(audioData);
            if (!result)
                log::error("TTS playback failed: {}", result.error().message);
        }
    }
};

TtsSpeaker::TtsSpeaker(): _impl(std::make_unique<Impl>())
{
}

TtsSpeaker::~TtsSpeaker()
{
    shutdown();
}

auto TtsSpeaker::initialize(const TtsSpeakerConfig& config) -> VoidResult
{
    _impl->config = config;

    auto const configPath = config.modelPath + ".json";

    auto const& espeakData =
        config.espeakDataPath.empty() ? std::string(PIPER_ESPEAK_DATA_DIR) : config.espeakDataPath;

    _impl->synth = piper_create(config.modelPath.c_str(), configPath.c_str(), espeakData.c_str());
    if (!_impl->synth)
        return makeError(ErrorCode::AudioError,
                         std::format("Failed to create piper synthesizer (model: {}, config: {}, espeak: {})",
                                     config.modelPath,
                                     configPath,
                                     espeakData));

    auto result = _impl->playback.initialize(PiperSampleRate, PiperChannels);
    if (!result)
        return result;

    // Start worker thread
    _impl->worker = std::jthread([this](const std::stop_token& token) { _impl->run(token); });

    log::info("TTS speaker initialized (model: {}, espeak: {})", config.modelPath, espeakData);
    return {};
}

void TtsSpeaker::speak(std::string text)
{
    auto lock = std::lock_guard(_impl->mutex);
    _impl->queue.push_back(std::move(text));
    _impl->cv.notify_one();
}

void TtsSpeaker::flush()
{
    auto lock = std::unique_lock(_impl->mutex);
    if (_impl->queue.empty())
        return;
    _impl->flushRequested = true;
    _impl->cv.notify_one();
    _impl->cv.wait(lock, [this] { return !_impl->flushRequested || _impl->shutdownRequested; });
}

auto TtsSpeaker::idle() const -> bool
{
    auto lock = std::lock_guard(_impl->mutex);
    return _impl->queue.empty() && !_impl->busy;
}

void TtsSpeaker::cancel()
{
    {
        auto lock = std::lock_guard(_impl->mutex);
        _impl->queue.clear();
    }

    _impl->playback.stop();
}

void TtsSpeaker::shutdown()
{
    {
        auto lock = std::lock_guard(_impl->mutex);
        _impl->shutdownRequested = true;
        _impl->queue.clear();
    }

    _impl->playback.stop();
    _impl->cv.notify_all();

    if (_impl->worker.joinable())
        _impl->worker.request_stop();
}

} // namespace mychat
