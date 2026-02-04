// SPDX-License-Identifier: Apache-2.0

// Single translation unit for the miniaudio implementation.
// This file must be compiled exactly once. All other sources (AudioCapture, AudioPlayback)
// include <miniaudio.h> without the IMPLEMENTATION define.
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>
