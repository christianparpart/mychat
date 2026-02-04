// SPDX-License-Identifier: Apache-2.0

#include "AudioCapture.hpp"

#include <core/Log.hpp>

#include <miniaudio.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <optional>
#include <string>

namespace mychat
{

struct AudioCapture::Impl
{
    ma_context context {};
    ma_device device {};
    AudioCallback callback;
    std::atomic<float> peakLevel { 0.0f };
    bool contextInitialized = false;
    bool capturing = false;
    bool initialized = false;
};

namespace
{

    void audioDataCallback(ma_device* device, void* /*output*/, const void* input, ma_uint32 frameCount)
    {
        auto* impl = static_cast<AudioCapture::Impl*>(device->pUserData);
        if (impl && impl->callback && input)
        {
            auto const* samples = static_cast<const float*>(input);

            // Compute peak amplitude for the voice meter (lock-free)
            auto peak = 0.0f;
            for (auto i = ma_uint32 { 0 }; i < frameCount; ++i)
                peak = std::max(peak, std::abs(samples[i]));
            impl->peakLevel.store(peak, std::memory_order_relaxed);

            impl->callback(std::span<const float>(samples, frameCount));
        }
    }

} // namespace

AudioCapture::AudioCapture(): _impl(std::make_unique<Impl>())
{
}

AudioCapture::~AudioCapture()
{
    stop();
    if (_impl->initialized)
        ma_device_uninit(&_impl->device);
    if (_impl->contextInitialized)
        ma_context_uninit(&_impl->context);
}

auto AudioCapture::initialize(AudioCallback callback, std::string_view deviceName) -> VoidResult
{
    _impl->callback = std::move(callback);

    // Initialize a persistent context — must outlive the device
    auto const ctxResult = ma_context_init(nullptr, 0, nullptr, &_impl->context);
    if (ctxResult != MA_SUCCESS)
        return makeError(ErrorCode::AudioError,
                         std::format("Failed to initialize audio context: {}", static_cast<int>(ctxResult)));
    _impl->contextInitialized = true;

    // Enumerate capture devices
    ma_device_info* pCaptureDevices = nullptr;
    auto captureCount = ma_uint32 { 0 };
    auto const enumResult =
        ma_context_get_devices(&_impl->context, nullptr, nullptr, &pCaptureDevices, &captureCount);

    auto matchedDeviceId = std::optional<ma_device_id> {};
    if (enumResult == MA_SUCCESS)
    {
        log::info("Available capture devices:");
        for (auto i = ma_uint32 { 0 }; i < captureCount; ++i)
            log::info("  [{}] {}", i, pCaptureDevices[i].name);

        if (!deviceName.empty())
        {
            // Case-insensitive substring match against user-specified name
            auto const lowerTarget = [&] {
                auto s = std::string(deviceName);
                std::ranges::transform(s, s.begin(), [](unsigned char c) { return std::tolower(c); });
                return s;
            }();

            for (auto i = ma_uint32 { 0 }; i < captureCount; ++i)
            {
                auto name = std::string(pCaptureDevices[i].name);
                std::ranges::transform(name, name.begin(), [](unsigned char c) { return std::tolower(c); });
                if (name.find(lowerTarget) != std::string::npos)
                {
                    log::info(
                        "Matched capture device '{}' for filter '{}'", pCaptureDevices[i].name, deviceName);
                    matchedDeviceId = pCaptureDevices[i].id;
                    break;
                }
            }

            if (!matchedDeviceId)
                log::warning("No capture device matching '{}' found, falling back to auto-select",
                             deviceName);
        }

        // Auto-select: pick the first non-monitor device (monitors are loopback sources, not microphones)
        if (!matchedDeviceId)
        {
            for (auto i = ma_uint32 { 0 }; i < captureCount; ++i)
            {
                auto name = std::string(pCaptureDevices[i].name);
                std::ranges::transform(name, name.begin(), [](unsigned char c) { return std::tolower(c); });
                if (!name.starts_with("monitor"))
                {
                    log::info("Auto-selected capture device '{}' (skipping monitor sources)",
                              pCaptureDevices[i].name);
                    matchedDeviceId = pCaptureDevices[i].id;
                    break;
                }
            }
        }
    }
    else
    {
        log::warning("Failed to enumerate capture devices (code: {}), using default",
                     static_cast<int>(enumResult));
    }

    auto deviceConfig = ma_device_config_init(ma_device_type_capture);
    deviceConfig.capture.format = ma_format_f32;
    deviceConfig.capture.channels = 1;
    deviceConfig.sampleRate = 16000;
    deviceConfig.dataCallback = audioDataCallback;
    deviceConfig.pUserData = _impl.get();

    if (matchedDeviceId)
        deviceConfig.capture.pDeviceID = &*matchedDeviceId;

    auto const result = ma_device_init(&_impl->context, &deviceConfig, &_impl->device);
    if (result != MA_SUCCESS)
        return makeError(ErrorCode::AudioError,
                         std::format("Failed to initialize audio device: {}", static_cast<int>(result)));

    log::info("Audio capture device: {}", _impl->device.capture.name);

    _impl->initialized = true;
    log::info("Audio capture initialized (16kHz, mono, float32)");
    return {};
}

auto AudioCapture::start() -> VoidResult
{
    if (!_impl->initialized)
        return makeError(ErrorCode::AudioError, "Audio device not initialized");

    if (_impl->capturing)
        return {};

    auto const result = ma_device_start(&_impl->device);
    if (result != MA_SUCCESS)
        return makeError(ErrorCode::AudioError,
                         std::format("Failed to start audio capture: {}", static_cast<int>(result)));

    _impl->capturing = true;
    log::info("Audio capture started");
    return {};
}

void AudioCapture::stop()
{
    if (!_impl->capturing)
        return;

    ma_device_stop(&_impl->device);
    _impl->capturing = false;
    log::info("Audio capture stopped");
}

auto AudioCapture::isCapturing() const -> bool
{
    return _impl->capturing;
}

auto AudioCapture::peakLevel() const -> float
{
    return _impl->peakLevel.load(std::memory_order_relaxed);
}

} // namespace mychat
