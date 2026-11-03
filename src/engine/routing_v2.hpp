// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "protocol.hpp"
#include <atomic>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace dl::v2 {
constexpr int HardwareChannels = 2;
constexpr int VirtualChannels = 24;
constexpr int HostSource = HardwareChannels + VirtualChannels;
constexpr int HardwareTarget = Channels;
constexpr int VirtualTarget = HardwareTarget + HardwareChannels;
constexpr int Ports = Channels + HardwareChannels + VirtualChannels;
constexpr wchar_t ControlName[] = L"Local\\DonnerLive.Routing.v2",
                  ControlMutexName[] = L"Local\\DonnerLive.Routing.Lock.v2",
                  PanelMutexName[] = L"Local\\DonnerLive.Panel.v2";
constexpr uint32_t ControlMagic = 0x37524C44;

struct Routes {
    float gain[Ports][Ports]{};
    uint32_t revision = 0;
};

inline bool validRoutes(const Routes &r) {
    for (auto &row : r.gain) {
        for (float g : row) {
            if (!std::isfinite(g) || g < 0 || g > 2) {
                return false;
            }
        }
    }
    return true;
}

inline Routes preset(int kind = 0) {
    Routes r;
    if (kind != 2) {
        for (int c = 0; c < 6; c++) {
            r.gain[c][c] = 1;
        }
    }
    if (kind == 0) {
        for (int c = 0; c < 2; c++) {
            r.gain[HostSource + c][HardwareTarget + c] = 1;
        }
        for (int c = 0; c < 4; c++) {
            r.gain[HostSource + 2 + c][VirtualTarget + c] = 1;
        }
    }
    if (kind == 1) {
        for (int c = 0; c < 4; c++) {
            r.gain[2 + c][VirtualTarget + c] = 1;
        }
    }
    return r;
}

inline int oldSource(int s) {
    return s < 3 ? s * 2 : HostSource + (s - 3) * 2;
}

inline int oldTarget(int d) {
    return d < 3 ? d * 2 : d == 3 ? HardwareTarget : VirtualTarget + (d - 4) * 2;
}

inline Routes loadRoutes(const std::filesystem::path &path) {
    std::ifstream f(path);
    std::string magic;
    int version;
    Routes r;
    if (!(f >> magic >> version) || magic != "DonnerLiveRoutes" || (version != 1 && version != 2)) {
        throw std::runtime_error("Invalid routing file");
    }
    if (version == 1) {
        for (int s = 0; s < 6; s++) {
            for (int d = 0; d < 6; d++) {
                float g;
                if (!(f >> g)) {
                    throw std::runtime_error("Incomplete v1 routes");
                }
                for (int c = 0; c < 2; c++) {
                    r.gain[oldSource(s) + c][oldTarget(d) + c] = g;
                }
            }
        }
    } else {
        int ports;
        if (!(f >> ports) || ports != Ports) {
            throw std::runtime_error("Invalid port count");
        }
        for (auto &row : r.gain) {
            for (float &g : row) {
                if (!(f >> g)) {
                    throw std::runtime_error("Incomplete routes");
                }
            }
        }
    }
    std::string extra;
    if (f >> extra) {
        throw std::runtime_error("Unexpected routing data");
    }
    if (!validRoutes(r)) {
        throw std::runtime_error("Invalid routing gain");
    }
    return r;
}

inline void saveRoutes(const std::filesystem::path &path, const Routes &r) {
    if (!validRoutes(r)) {
        throw std::runtime_error("Invalid routes");
    }
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }
    auto temp = path;
    temp += L".tmp";
    {
        std::ofstream f(temp);
        f.precision(9);
        f << "DonnerLiveRoutes 2 " << Ports << '\n';
        for (auto &row : r.gain) {
            for (float g : row) {
                f << g << ' ';
            }
            f << '\n';
        }
        f.flush();
        if (!f) {
            throw std::runtime_error("Routing save failed");
        }
    }
    if (!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        throw std::runtime_error("Routing replace failed");
    }
}

struct Control {
    uint32_t magic = 0, bytes = 0, enginePid = 0;
    uint64_t epoch = 0;
    volatile LONG running = 0, hostActive = 0;
    double rate = 0;
    LONG resampling = 0, bufferFrames = Frames, hostInputs = Channels, hostOutputs = Channels;
    uint32_t hostInputMask = 0, hostOutputMask = 0;
    LONG hostUsageMode = 0;
    char inputNames[Channels][32]{}, outputNames[Channels][32]{};
    Routes current{}, requested{};
    uint32_t request = 0, accepted = 0;
    LONG error = 0;
    volatile LONG activeRevision = 0;
    volatile LONG sourcePeak[Ports]{}, destinationPeak[Ports]{};
    volatile LONG64 clippedSamples = 0;
    wchar_t configPath[1024]{};
};

class RouteExchange {
    struct Slot {
        Routes routes;
        std::atomic<int> readers{0};
    } slots_[3];

    std::atomic<int> published_{0};

  public:
    explicit RouteExchange(const Routes &r = preset()) {
        for (auto &s : slots_) {
            s.routes = r;
        }
    }

    bool publish(const Routes &r) {
        int current = published_.load();
        for (int i = 0; i < 3; i++) {
            if (i != current && slots_[i].readers.load() == 0) {
                slots_[i].routes = r;
                published_.store(i);
                return true;
            }
        }
        return false;
    }

    Routes read() {
        for (;;) {
            int i = published_.load();
            slots_[i].readers.fetch_add(1);
            if (i == published_.load()) {
                Routes r = slots_[i].routes;
                slots_[i].readers.fetch_sub(1);
                return r;
            }
            slots_[i].readers.fetch_sub(1);
        }
    }
};

inline void peakMax(volatile LONG *p, float peak) {
    LONG value = (LONG)(std::min(peak, 32.f) * 1000000.f), old = read32(p);
    while (value > old) {
        LONG before = InterlockedCompareExchange(p, value, old);
        if (before == old) {
            break;
        }
        old = before;
    }
}

class Router {
    Routes previous_ = preset();

    static void
    mixSend(const float *source, float *destination, float previousGain, float targetGain, int frames) {
        if (previousGain == 0 && targetGain == 0) {
            return;
        }
        for (int frame = 0; frame < frames; ++frame) {
            const float sample = source[frame];
            if (std::isfinite(sample)) {
                const float gain = previousGain + (targetGain - previousGain) * (frame + 1) / frames;
                destination[frame] += sample * gain;
            }
        }
    }

    static void
    measureAndLimit(const float *source, float *destination, int channel, Control *control, int frames) {
        float inputPeak = 0, outputPeak = 0;
        for (int frame = 0; frame < frames; ++frame) {
            const float input = source[frame];
            if (std::isfinite(input)) {
                inputPeak = std::max(inputPeak, std::abs(input));
            }
            float &output = destination[frame];
            if (!std::isfinite(output)) {
                output = 0;
            }
            outputPeak = std::max(outputPeak, std::abs(output));
            if (channel >= HardwareTarget && std::abs(output) > 1) {
                output = std::clamp(output, -1.f, 1.f);
                if (control) {
                    InterlockedIncrement64(&control->clippedSamples);
                }
            }
        }
        if (control) {
            peakMax(&control->sourcePeak[channel], inputPeak);
            peakMax(&control->destinationPeak[channel], outputPeak);
        }
    }

  public:
    template <int Capacity>
    void process(const float (&source)[Ports][Capacity],
                 float (&destination)[Ports][Capacity],
                 const Routes &routes,
                 Control *control,
                 int frames = Frames) {
        if (frames < 1 || frames > Capacity) {
            return;
        }
        for (auto &ch : destination) {
            std::fill(ch, ch + frames, 0.f);
        }
        for (int sourcePort = 0; sourcePort < Ports; ++sourcePort) {
            for (int targetPort = 0; targetPort < Ports; ++targetPort) {
                mixSend(source[sourcePort],
                        destination[targetPort],
                        previous_.gain[sourcePort][targetPort],
                        routes.gain[sourcePort][targetPort],
                        frames);
            }
        }
        previous_ = routes;
        for (int channel = 0; channel < Ports; ++channel) {
            measureAndLimit(source[channel], destination[channel], channel, control, frames);
        }
    }
};
} // namespace dl::v2
