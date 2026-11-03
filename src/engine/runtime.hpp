// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "protocol.hpp"
#include "asio_backend.hpp"
#include "audio_fifo.hpp"
#include "mixer_guard.hpp"
#include "routing_v2.hpp"
#include "host_config.hpp"
#include "host_usage.hpp"
#include "driver_settings.hpp"
#include "audio_config.hpp"
#include <filesystem>
#include <fstream>
#include <ostream>
#include <atomic>
#include <cmath>

namespace dl {
class Engine {
    std::ostream &log_;
    std::ostream &errors_;
    Backend hardware_, virtual_;
    AudioFifo virtualInputQueue_{v2::VirtualChannels}, virtualOutputQueue_{v2::VirtualChannels};
    HostConfig host_;
    MixerGuard mixer_;
    Shared *shared_ = nullptr;
    HANDLE map_ = nullptr, inputEvent_ = nullptr, stopEvent_ = nullptr, owner_ = nullptr;
    HWND window_ = nullptr;
    HANDLE controlMap_ = nullptr, controlMutex_ = nullptr;
    v2::Control *control_ = nullptr;
    v2::RouteExchange routes_;
    v2::Router router_;
    std::filesystem::path configPath_;
    int frames_ = Frames;
    double requestedRate_ = 0;
    float sources_[v2::Ports][MaxFrames]{}, destinations_[v2::Ports][MaxFrames]{};
    bool resample_ = false;
    float virtualInput_[MaxFrames * v2::VirtualChannels]{}, virtualOutput_[MaxFrames * v2::VirtualChannels]{},
        virtualInputAtHardwareRate_[MaxFrames * v2::VirtualChannels]{},
        virtualOutputAtHardwareRate_[MaxFrames * v2::VirtualChannels]{};
    std::atomic<bool> fault_{false};
    ASIOCallbacks hardwareCallbacks_{hardwareSwitch, rateChanged, message, hardwareTime},
        virtualCallbacks_{virtualSwitch, rateChanged, message, virtualTime};
    bool closed_ = false;
    uint64_t hostCheckTick_ = 0;
    static Engine *active_;

    static long message(long s, long v, void *, double *);

    static void rateChanged(ASIOSampleRate);

    static ASIOTime *hardwareTime(ASIOTime *t, long i, ASIOBool);

    static ASIOTime *virtualTime(ASIOTime *, long i, ASIOBool);

    static void hardwareSwitch(long i, ASIOBool);

    static void virtualSwitch(long i, ASIOBool);

    void hardwareBlock(long index, ASIOTime *time);

    void virtualBlock(long index);

    void acquireOwnership();
    void initialiseAudioMapping();
    void initialiseRoutingControl(const std::filesystem::path &config);
    void openBackends(DonnerSettings &settings,
                      const AudioConfig &desiredAudio,
                      const DonnerSettings::State &beforeAudio);
    void startAudio(const std::filesystem::path &folder,
                    const std::filesystem::path &audioPath,
                    DonnerSettings &settings,
                    const AudioConfig &desiredAudio);

  public:
    explicit Engine(bool resample, int frames, double rate, std::ostream &log, std::ostream &errors);

    ~Engine();

    void open(const std::filesystem::path &folder, const std::filesystem::path &config);

    bool wait(int milliseconds);

    void close();
};
} // namespace dl
