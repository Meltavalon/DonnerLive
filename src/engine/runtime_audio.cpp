// SPDX-License-Identifier: AGPL-3.0-only
#include "runtime.hpp"

namespace dl {
using namespace v2;

Engine *Engine::active_ = nullptr;

ASIOTime *Engine::hardwareTime(ASIOTime *t, long i, ASIOBool) {
    if (active_) {
        active_->hardwareBlock(i, t);
    }
    return nullptr;
}

ASIOTime *Engine::virtualTime(ASIOTime *, long i, ASIOBool) {
    if (active_) {
        active_->virtualBlock(i);
    }
    return nullptr;
}

long Engine::message(long s, long v, void *, double *) {
    if (s == kAsioSelectorSupported) {
        return v == kAsioEngineVersion || v == kAsioSupportsTimeInfo || v == kAsioResetRequest ||
               v == kAsioResyncRequest;
    }
    if (s == kAsioEngineVersion) {
        return 2;
    }
    if (s == kAsioSupportsTimeInfo) {
        return 1;
    }
    if (s == kAsioResetRequest || s == kAsioResyncRequest) {
        if (active_) {
            active_->fault_ = true;
        }
        return 1;
    }
    return 0;
}

void Engine::rateChanged(ASIOSampleRate) {
    if (active_) {
        active_->fault_ = true;
    }
}

void Engine::hardwareSwitch(long i, ASIOBool) {
    if (!active_) {
        return;
    }
    ASIOTime t{};
    auto d = active_->hardware_.driver;
    if (d->getSamplePosition(&t.timeInfo.samplePosition, &t.timeInfo.systemTime) == ASE_OK) {
        t.timeInfo.flags = kSamplePositionValid | kSystemTimeValid;
    }
    active_->hardwareBlock(i, &t);
}

void Engine::virtualSwitch(long i, ASIOBool) {
    if (active_) {
        active_->virtualBlock(i);
    }
}

void Engine::hardwareBlock(long index, ASIOTime *time) {
    if (index < 0 || index > 1) {
        return;
    }
    hardware_.readInputs(index);
    auto sequence = InterlockedIncrement64(&shared_->hardwareCallbacks);
    if (resample_) {
        virtualInputQueue_.pull(
            virtualInputAtHardwareRate_, frames_, virtual_.rate / hardware_.rate, frames_ * 2);
    } else {
        virtualInputQueue_.pullExact(virtualInputAtHardwareRate_, frames_, frames_ * 2);
    }
    bool ready = read32(&shared_->clientReady) == 1;
    auto &outputSlot = shared_->slots[(sequence - 1) % Slots];
    bool outputAvailable = ready && sequence > 1 && read64(&outputSlot.outputSequence) == sequence - 1;
    if (ready && !outputAvailable && sequence > 8 && read64(&shared_->latestOutput) > 0) {
        InterlockedIncrement64(&shared_->lateOutputs);
    }
    for (int c = 0; c < 2; c++) {
        memcpy(sources_[c], hardware_.input(c, index), sizeof(float) * frames_);
    }
    for (int c = 0; c < v2::VirtualChannels; c++) {
        for (int f = 0; f < frames_; f++) {
            sources_[c + 2][f] = virtualInputAtHardwareRate_[f * v2::VirtualChannels + c];
        }
    }
    for (int c = 0; c < Channels; c++) {
        for (int f = 0; f < frames_; f++) {
            sources_[c + HostSource][f] =
                outputAvailable && c < host_.outputs ? outputSlot.output[c][f] : 0.f;
        }
    }
    auto routing = routes_.read();
    router_.process(sources_, destinations_, routing, control_, frames_);
    InterlockedExchange(&control_->activeRevision, (LONG)routing.revision);
    InterlockedExchange(&control_->hostActive, ready && outputAvailable ? 1 : 0);
    for (int f = 0; f < frames_; f++) {
        for (int c = 0; c < 2; c++) {
            hardware_.output(c, index)[f] = destinations_[c + HardwareTarget][f];
        }
        for (int c = 0; c < v2::VirtualChannels; c++) {
            virtualOutputAtHardwareRate_[f * v2::VirtualChannels + c] = destinations_[c + VirtualTarget][f];
        }
    }
    virtualOutputQueue_.push(virtualOutputAtHardwareRate_, frames_);
    hardware_.writeOutputs(index);
    auto &inputSlot = shared_->slots[sequence % Slots];
    uint64_t position = (uint64_t)(sequence - 1) * frames_, nanoseconds = GetTickCount64() * 1000000ULL;
    if (time && (time->timeInfo.flags & kSamplePositionValid)) {
        position = ((uint64_t)time->timeInfo.samplePosition.hi << 32) | time->timeInfo.samplePosition.lo;
    }
    if (time && (time->timeInfo.flags & kSystemTimeValid)) {
        nanoseconds = ((uint64_t)time->timeInfo.systemTime.hi << 32) | time->timeInfo.systemTime.lo;
    }
    inputSlot.samplePosition = position;
    inputSlot.systemNanoseconds = nanoseconds;
    for (int c = 0; c < host_.inputs; c++) {
        memcpy(inputSlot.input[c], destinations_[c], sizeof(float) * frames_);
    }
    InterlockedExchange64(&inputSlot.inputSequence, sequence);
    InterlockedExchange64(&shared_->latestInput, sequence);
    if (ready) {
        SetEvent(inputEvent_);
    }
}

void Engine::virtualBlock(long index) {
    if (index < 0 || index > 1) {
        return;
    }
    virtual_.readInputs(index);
    InterlockedIncrement64(&shared_->virtualCallbacks);
    for (int c = 0; c < v2::VirtualChannels; c++) {
        for (int f = 0; f < frames_; f++) {
            virtualInput_[f * v2::VirtualChannels + c] = virtual_.input(c, index)[f];
        }
    }
    virtualInputQueue_.push(virtualInput_, frames_);
    if (resample_) {
        virtualOutputQueue_.pull(virtualOutput_, frames_, hardware_.rate / virtual_.rate, frames_ * 2);
    } else {
        virtualOutputQueue_.pullExact(virtualOutput_, frames_, frames_ * 2);
    }
    for (int c = 0; c < v2::VirtualChannels; c++) {
        for (int f = 0; f < frames_; f++) {
            virtual_.output(c, index)[f] = virtualOutput_[f * v2::VirtualChannels + c];
        }
    }
    virtual_.writeOutputs(index);
}
} // namespace dl
