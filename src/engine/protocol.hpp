// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "../platform/windows.hpp"
#include <cstdint>

namespace dl {
constexpr int Frames = 256, MaxFrames = 2048, Channels = 32, Slots = 8;
constexpr uint32_t Magic = 0x344C4E44, Version = 4;
constexpr wchar_t MappingName[] = L"Local\\DonnerLive.P4.Audio.v1";
constexpr wchar_t InputEventName[] = L"Local\\DonnerLive.P4.Input.v1";
constexpr wchar_t StopEventName[] = L"Local\\DonnerLive.P4.Stop.v1";
constexpr wchar_t EngineMutexName[] = L"Local\\DonnerLive.P4.Engine.v1";
constexpr wchar_t DriverClsid[] = L"{6223CC56-3DB6-48D1-8CC0-A3CFB72683BE}";

struct alignas(64) Slot {
    volatile LONG64 inputSequence = 0, outputSequence = 0;
    uint64_t samplePosition = 0, systemNanoseconds = 0;
    float input[Channels][MaxFrames]{}, output[Channels][MaxFrames]{};
};

struct alignas(64) Shared {
    uint32_t magic = 0, version = 0, bytes = 0, enginePid = 0;
    double sampleRate = 0, virtualSampleRate = 0;
    uint64_t epoch = 0;
    volatile LONG engineRunning = 0, clientPid = 0, clientReady = 0;
    volatile LONG64 latestInput = 0, latestOutput = 0, hostCallbacks = 0, hostSkipped = 0,
                    hardwareCallbacks = 0, virtualCallbacks = 0, lateOutputs = 0;
    LONG inputLatency = 0, outputLatency = 0;
    LONG bufferFrames = Frames;
    LONG hostInputs = Channels, hostOutputs = Channels;
    volatile LONG bufferInputMask = 0, bufferOutputMask = 0;
    char inputNames[Channels][32]{}, outputNames[Channels][32]{};
    Slot slots[Slots];
};

inline LONG64 read64(volatile LONG64 *p) {
    return InterlockedCompareExchange64(p, 0, 0);
}

inline LONG read32(volatile LONG *p) {
    return InterlockedCompareExchange(p, 0, 0);
}

inline bool valid(Shared *s) {
    return s && s->magic == Magic && s->version == Version && s->bytes == sizeof(Shared) &&
           s->hostInputs == Channels && s->hostOutputs == Channels && s->bufferFrames >= 16 &&
           s->bufferFrames <= MaxFrames && !(s->bufferFrames & (s->bufferFrames - 1)) &&
           read32(&s->engineRunning) == 1;
}
} // namespace dl
