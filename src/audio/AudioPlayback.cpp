// SPDX-License-Identifier: Apache-2.0
#include "AudioPlayback.hpp"

#include <core/Log.hpp>

#include <miniaudio.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <format>
#include <mutex>
#include <span>

namespace mychat
{

struct AudioPlayback::Impl
{
    ma_device device {};
    bool initialized = false;

    // Buffer state — guarded by mutex + signalled via condvar
    std::span<const float> buffer;
    std::size_t readPos = 0;
    std::mutex mutex;
    std::condition_variable done;
    std::atomic<bool> cancelled { false };
};

namespace
{

    void playbackDataCallback(ma_device* device, void* output, const void* /*input*/, ma_uint32 frameCount)
    {
        auto* impl = static_cast<AudioPlayback::Impl*>(device->pUserData);
        auto* out = static_cast<float*>(output);
        auto const channels = device->playback.channels;
        auto const totalSamples = static_cast<std::size_t>(frameCount) * channels;

        auto lock = std::unique_lock(impl->mutex);
        auto const remaining = impl->buffer.size() - impl->readPos;
        auto const toCopy = std::min(totalSamples, remaining);

        if (toCopy > 0)
        {
            std::copy_n(impl->buffer.data() + impl->readPos, toCopy, out);
            impl->readPos += toCopy;
        }

        // Zero-fill any remaining output frames
        if (toCopy < totalSamples)
            std::fill_n(out + toCopy, totalSamples - toCopy, 0.0f);

        // Signal completion when all samples consumed or cancelled
        if (impl->readPos >= impl->buffer.size() || impl->cancelled.load(std::memory_order_relaxed))
        {
            lock.unlock();
            impl->done.notify_one();
        }
    }

} // namespace

AudioPlayback::AudioPlayback(): _impl(std::make_unique<Impl>())
{
}

AudioPlayback::~AudioPlayback()
{
    stop();
    if (_impl->initialized)
        ma_device_uninit(&_impl->device);
}

auto AudioPlayback::initialize(unsigned sampleRate, unsigned channels) -> VoidResult
{
    auto config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32;
    config.playback.channels = channels;
    config.sampleRate = sampleRate;
    config.dataCallback = playbackDataCallback;
    config.pUserData = _impl.get();

    auto const result = ma_device_init(nullptr, &config, &_impl->device);
    if (result != MA_SUCCESS)
        return makeError(ErrorCode::AudioError,
                         std::format("Failed to initialize playback device: {}", static_cast<int>(result)));

    _impl->initialized = true;
    log::info("Audio playback initialized ({}Hz, {} channel(s), f32)", sampleRate, channels);
    return {};
}

auto AudioPlayback::play(std::span<const float> samples) -> VoidResult
{
    if (!_impl->initialized)
        return makeError(ErrorCode::AudioError, "Playback device not initialized");

    if (samples.empty())
        return {};

    {
        auto lock = std::lock_guard(_impl->mutex);
        _impl->buffer = samples;
        _impl->readPos = 0;
        _impl->cancelled.store(false, std::memory_order_relaxed);
    }

    auto const startResult = ma_device_start(&_impl->device);
    if (startResult != MA_SUCCESS)
        return makeError(ErrorCode::AudioError,
                         std::format("Failed to start playback: {}", static_cast<int>(startResult)));

    // Block until all samples consumed or cancelled
    {
        auto lock = std::unique_lock(_impl->mutex);
        _impl->done.wait(lock, [this] {
            return _impl->readPos >= _impl->buffer.size() || _impl->cancelled.load(std::memory_order_relaxed);
        });
    }

    ma_device_stop(&_impl->device);
    return {};
}

void AudioPlayback::stop()
{
    _impl->cancelled.store(true, std::memory_order_relaxed);
    _impl->done.notify_one();

    if (_impl->initialized)
        ma_device_stop(&_impl->device);
}

} // namespace mychat
