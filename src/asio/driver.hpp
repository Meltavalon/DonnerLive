// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "sdk.hpp"
#include "../engine/protocol.hpp"
#include <atomic>
#include <thread>
#include <string>

namespace dl::asio {
extern HMODULE moduleHandle;
extern std::atomic<long> liveObjects;
extern std::atomic<long> serverLocks;
inline constexpr CLSID DriverId = {
    0x6223cc56, 0x3db6, 0x48d1, {0x8c, 0xc0, 0xa3, 0xcf, 0xb7, 0x26, 0x83, 0xbe}};

class Driver final : public IASIO {
    std::atomic<ULONG> refs_{1};
    HANDLE mapping_ = nullptr, event_ = nullptr;
    dl::Shared *shared_ = nullptr;
    ASIOCallbacks callbacks_{};
    bool initialized_ = false, buffersCreated_ = false, supportsTimeInfo_ = false, ownsClient_ = false;
    uint32_t inputMask_ = 0, outputMask_ = 0;
    std::atomic<bool> stopRequested_{true};
    std::thread callbackThread_;
    float input_[2][dl::Channels][dl::MaxFrames]{}, output_[2][dl::Channels][dl::MaxFrames]{};
    std::atomic<uint64_t> samplePosition_{0}, systemNanoseconds_{0};
    std::string error_ = "Open DonnerLive before selecting DonnerLive ASIO";

    void runCallbacks();

  public:
    Driver();

    ~Driver();

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override;

    ULONG STDMETHODCALLTYPE AddRef() override;

    ULONG STDMETHODCALLTYPE Release() override;

    ASIOBool init(void *) override;

    void getDriverName(char *name) override;

    long getDriverVersion() override;

    void getErrorMessage(char *text) override;

    ASIOError start() override;

    ASIOError stop() override;

    ASIOError getChannels(long *in, long *out) override;

    ASIOError getLatencies(long *in, long *out) override;

    ASIOError getBufferSize(long *min, long *max, long *preferred, long *granularity) override;

    ASIOError canSampleRate(ASIOSampleRate rate) override;

    ASIOError getSampleRate(ASIOSampleRate *rate) override;

    ASIOError setSampleRate(ASIOSampleRate rate) override;

    ASIOError getClockSources(ASIOClockSource *clocks, long *count) override;

    ASIOError setClockSource(long reference) override;

    ASIOError getSamplePosition(ASIOSamples *sample, ASIOTimeStamp *stamp) override;

    ASIOError getChannelInfo(ASIOChannelInfo *info) override;

    ASIOError createBuffers(ASIOBufferInfo *info, long count, long frames, ASIOCallbacks *callbacks) override;

    ASIOError disposeBuffers() override;

    ASIOError controlPanel() override;

    ASIOError future(long, void *) override;

    ASIOError outputReady() override;
};
} // namespace dl::asio
