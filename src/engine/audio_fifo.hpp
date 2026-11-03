// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace dl {
// One producer, one consumer. Only the consumer owns fractional read position.
class AudioFifo {
    static constexpr uint64_t Capacity = 32768, Mask = Capacity - 1;
    std::vector<float> data_;
    int channels_;
    alignas(64) std::atomic<uint64_t> write_{0};
    alignas(64) std::atomic<uint64_t> read_{0};
    double readPosition_ = 0, filteredOccupancyError_ = 0;
    bool primed_ = false;

  public:
    explicit AudioFifo(int channels = 4) : data_(Capacity * channels), channels_(channels) {}

    std::atomic<uint64_t> overflows{0}, underflows{0};

    void pullExact(float *interleaved, int frames, int targetFrames) {
        auto writeFrame = write_.load(std::memory_order_acquire),
             readFrame = read_.load(std::memory_order_relaxed);
        std::fill(interleaved, interleaved + frames * channels_, 0.f);
        if (!primed_) {
            if (writeFrame - readFrame < (uint64_t)(targetFrames + frames)) {
                return;
            }
            primed_ = true;
        }
        if (writeFrame - readFrame < (uint64_t)frames) {
            underflows++;
            primed_ = false;
            return;
        }
        for (int i = 0; i < frames; i++) {
            for (int c = 0; c < channels_; c++) {
                interleaved[i * channels_ + c] = data_[((readFrame + i) & Mask) * channels_ + c];
            }
        }
        read_.store(readFrame + frames, std::memory_order_release);
    }

    bool push(const float *interleaved, int frames) {
        auto writeFrame = write_.load(std::memory_order_relaxed),
             readFrame = read_.load(std::memory_order_acquire);
        if (writeFrame - readFrame + frames >= Capacity) {
            overflows++;
            return false;
        }
        for (int i = 0; i < frames; i++) {
            for (int c = 0; c < channels_; c++) {
                data_[((writeFrame + i) & Mask) * channels_ + c] = interleaved[i * channels_ + c];
            }
        }
        write_.store(writeFrame + frames, std::memory_order_release);
        return true;
    }

    void pull(float *interleaved, int frames, double nominalStep, int targetFrames) {
        auto writeFrame = write_.load(std::memory_order_acquire);
        auto readFrame = read_.load(std::memory_order_relaxed);
        double availableFrames = static_cast<double>(writeFrame - readFrame);
        double requiredFrames = frames * nominalStep + 2;
        std::fill(interleaved, interleaved + frames * channels_, 0.f);
        if (!primed_) {
            if (availableFrames < targetFrames + requiredFrames) {
                return;
            }
            readPosition_ = (double)readFrame;
            filteredOccupancyError_ = 0;
            primed_ = true;
        }
        double error = (double)writeFrame - readPosition_ - targetFrames - requiredFrames;
        filteredOccupancyError_ += 0.01 * (error - filteredOccupancyError_);
        double step = nominalStep * (1 + std::clamp(filteredOccupancyError_ * 0.000001, -0.002, 0.002));
        if (readPosition_ + frames * step + 1 >= (double)writeFrame) {
            underflows++;
            primed_ = false;
            return;
        }
        for (int i = 0; i < frames; i++) {
            auto firstFrame = (uint64_t)readPosition_;
            float fraction = (float)(readPosition_ - firstFrame);
            for (int c = 0; c < channels_; c++) {
                float firstSample = data_[(firstFrame & Mask) * channels_ + c],
                      nextSample = data_[((firstFrame + 1) & Mask) * channels_ + c];
                interleaved[i * channels_ + c] = firstSample + (nextSample - firstSample) * fraction;
            }
            readPosition_ += step;
        }
        read_.store((uint64_t)readPosition_, std::memory_order_release);
    }
};
} // namespace dl
