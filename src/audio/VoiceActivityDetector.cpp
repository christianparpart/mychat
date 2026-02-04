// SPDX-License-Identifier: Apache-2.0
#include "VoiceActivityDetector.hpp"

#include <core/Log.hpp>

#include <whisper.h>

#include <cmath>

namespace mychat
{

struct VoiceActivityDetector::Impl
{
    // whisper.cpp includes a built-in VAD implementation
    // We use a simple energy-based VAD as fallback
    float energyThreshold = 0.01f;
    bool modelLoaded = false;
};

VoiceActivityDetector::VoiceActivityDetector(): _impl(std::make_unique<Impl>())
{
}

VoiceActivityDetector::~VoiceActivityDetector() = default;

auto VoiceActivityDetector::initialize(std::string_view modelPath) -> VoidResult
{
    // The Silero-VAD model path is optional; if not provided, use energy-based detection
    if (!modelPath.empty())
    {
        log::info("VAD model path specified: {} (using energy-based fallback for now)", modelPath);
    }

    _impl->modelLoaded = true;
    log::info("Voice activity detector initialized");
    return {};
}

auto VoiceActivityDetector::process(std::span<const float> samples) -> Result<float>
{
    if (!_impl->modelLoaded)
        return makeError(ErrorCode::AudioError, "VAD not initialized");

    // Energy-based VAD: compute RMS energy
    auto energy = 0.0f;
    for (auto const sample: samples)
        energy += sample * sample;

    if (!samples.empty())
        energy = std::sqrt(energy / static_cast<float>(samples.size()));

    // Normalize to 0..1 range with the threshold
    auto const probability = std::min(1.0f, energy / (_impl->energyThreshold * 2.0f));
    return probability;
}

auto VoiceActivityDetector::isSpeech(float probability, float threshold) -> bool
{
    return probability >= threshold;
}

void VoiceActivityDetector::reset()
{
    // Reset any internal state
}

} // namespace mychat
