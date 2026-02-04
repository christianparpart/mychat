// SPDX-License-Identifier: Apache-2.0
#pragma once

namespace mychat
{

/// @brief Configuration for LLM token sampling.
struct SamplerConfig
{
    float temperature = 0.7f;
    float topP = 0.9f;
    int topK = 40;
    float repeatPenalty = 1.1f;
    int repeatLastN = 64;
    int seed = -1; // -1 means random
};

} // namespace mychat
