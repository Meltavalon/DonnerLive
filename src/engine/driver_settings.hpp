// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "../platform/windows.hpp"
#include <array>
#include <vector>
#include <string>
#include <stdexcept>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <optional>
#include <cstring>
#include <cwctype>
#include <winver.h>
#include "driver_paths.hpp"

namespace dl {
// ABI 5.7 layouts checked against topsai/donner-livejack-control-panel.
// This is a small C++ binding to the installed driver, not a port of its UI.
class DonnerSettings {
    HMODULE module_ = nullptr;
    unsigned token_ = 0;
    std::wstring libraryPath_, identity_;

    template <class T> T api(const char *name) {
        auto p = GetProcAddress(module_, name);
        if (!p) {
            throw std::runtime_error("Driver API export unavailable");
        }
        T result;
        memcpy(&result, &p, sizeof(result));
        return result;
    }

    static void check(unsigned code, const char *operation) {
        if (code) {
            std::ostringstream s;
            s << operation << " (0x" << std::hex << code << ")";
            throw std::runtime_error(s.str());
        }
    }

  public:
    struct Channel {
        unsigned direction = 0, number = 0;
        bool canVolume = false, canMute = false;
        int minimum = 0, maximum = 0, step = 0;
        std::optional<int> volume;
        std::optional<bool> muted;
    };

    struct State {
        unsigned rate = 0, preferred = 0, flags = 0, bufferRate = 0, inputLatency = 0, outputLatency = 0;
        std::vector<unsigned> rates, buffers;
        std::array<Channel, 4> channels{};
        bool clockAvailable = false, clockLocked = false;
        std::wstring clockName, driverVersion;
    };

    using None = unsigned(WINAPI *)();
    using Open = unsigned(WINAPI *)(unsigned, unsigned *);
    using Token = unsigned(WINAPI *)(unsigned);
    using Get = unsigned(WINAPI *)(unsigned, unsigned *);
    using Write = unsigned(WINAPI *)(unsigned, unsigned);
    using Rates = unsigned(WINAPI *)(unsigned, unsigned, unsigned *, unsigned *);
    using Info = unsigned(WINAPI *)(unsigned, void *);
    using Buffer = unsigned(WINAPI *)(unsigned, unsigned, unsigned, unsigned);
    using ChannelInfo = unsigned(WINAPI *)(unsigned, unsigned, unsigned, void *);
    using ChannelGet = unsigned(WINAPI *)(unsigned, unsigned, unsigned, unsigned short *);
    using ChannelWrite = unsigned(WINAPI *)(unsigned, unsigned, unsigned, unsigned short);

    explicit DonnerSettings(const wchar_t *library = nullptr) {
        libraryPath_ = library ? std::filesystem::path(library).wstring() : donnerControlLibrary().wstring();
        module_ = LoadLibraryExW(libraryPath_.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (!module_) {
            throw std::runtime_error("Donner control API not installed");
        }
        try {
            if (api<None>("TUSBAUDIO_GetApiVersion")() != 0x50007) {
                throw std::runtime_error("Unverified Donner control API ABI");
            }
            check(api<None>("TUSBAUDIO_EnumerateDevices")(), "EnumerateDevices");
            if (api<None>("TUSBAUDIO_GetDeviceCount")() != 1) {
                throw std::runtime_error("Expected one Donner Livejack");
            }
            check(api<Open>("TUSBAUDIO_OpenDeviceByIndex")(0, &token_), "OpenDevice");
            wchar_t id[1024]{};
            using Id = unsigned(WINAPI *)(unsigned, wchar_t *, unsigned);
            check(api<Id>("TUSBAUDIO_GetDeviceInstanceIdString")(token_, id, 1024), "Device identity");
            std::wstring identity(id);
            for (auto &c : identity) {
                c = towupper(c);
            }
            if (identity.find(L"VID_152A&PID_885C") == std::wstring::npos) {
                throw std::runtime_error("Unverified Donner device");
            }
            identity_ = identity;
        } catch (...) {
            close();
            throw;
        }
    }

    ~DonnerSettings() {
        close();
    }

    DonnerSettings(const DonnerSettings &) = delete;
    DonnerSettings &operator=(const DonnerSettings &) = delete;

    void close() {
        if (token_ && module_) {
            auto p = GetProcAddress(module_, "TUSBAUDIO_CloseDevice");
            if (p) {
                Token fn;
                memcpy(&fn, &p, sizeof(fn));
                fn(token_);
            }
            token_ = 0;
        }
        if (module_) {
            FreeLibrary(module_);
            module_ = nullptr;
        }
    }

    State read() {
        State s;
        check(api<Get>("TUSBAUDIO_GetCurrentSampleRate")(token_, &s.rate), "GetCurrentSampleRate");
        std::array<unsigned, 32> rates{};
        unsigned count = 0;
        check(api<Rates>("TUSBAUDIO_GetSupportedSampleRates")(token_, 32, rates.data(), &count),
              "GetSupportedSampleRates");
        if (count > 32) {
            throw std::runtime_error("Invalid driver rate count");
        }
        s.rates.assign(rates.begin(), rates.begin() + count);
        std::array<unsigned, 49> info{};
        check(api<Info>("TUSBAUDIO_GetASIOInstanceInfo")(0, info.data()), "GetASIOInstanceInfo");
        if (info[8] > 32) {
            throw std::runtime_error("Invalid driver buffer count");
        }
        s.bufferRate = info[1];
        s.preferred = info[7];
        s.flags = info[4];
        s.inputLatency = info[5];
        s.outputLatency = info[6];
        for (unsigned i = 0; i < info[8]; i++) {
            if (info[9 + i] >= 16 && info[9 + i] <= 2048 && !(info[9 + i] & (info[9 + i] - 1))) {
                s.buffers.push_back(info[9 + i]);
            }
        }
        for (int i = 0; i < 4; i++) {
            auto &c = s.channels[i];
            c.direction = i < 2 ? 1 : 0;
            c.number = i % 2 + 1;
            try {
                std::array<unsigned char, 16> bytes{};
                check(api<ChannelInfo>("TUSBAUDIO_GetVolumeMuteInfo")(
                          token_, c.direction, c.number, bytes.data()),
                      "GetVolumeMuteInfo");
                unsigned flags = 0;
                short minimum = 0, maximum = 0;
                unsigned short step = 0;
                memcpy(&flags, bytes.data(), 4);
                memcpy(&minimum, bytes.data() + 4, 2);
                memcpy(&maximum, bytes.data() + 6, 2);
                memcpy(&step, bytes.data() + 8, 2);
                c.minimum = minimum;
                c.maximum = maximum;
                c.step = step;
                c.canVolume = (flags & 1) && minimum < maximum && step;
                c.canMute = (flags & 2) != 0;
                unsigned short volume = 0;
                if (c.canVolume &&
                    api<ChannelGet>("TUSBAUDIO_GetVolume")(token_, c.direction, c.number, &volume) == 0) {
                    c.volume = (short)volume;
                }
                unsigned muted = 0;
                if (c.canMute &&
                    api<ChannelInfo>("TUSBAUDIO_GetMute")(token_, c.direction, c.number, &muted) == 0 &&
                    muted <= 1) {
                    c.muted = muted != 0;
                }
            } catch (const std::exception &) {
            } // Unsupported channels stay unavailable.
        }
        try {
            std::array<unsigned, 85> clock{};
            check(api<Info>("TUSBAUDIO_GetCurrentClockSource")(token_, clock.data()),
                  "GetCurrentClockSource");
            if (clock[0] && clock[3] <= 1) {
                wchar_t name[129]{};
                memcpy(name, (unsigned char *)clock.data() + 20, 256);
                s.clockName = name;
                s.clockLocked = clock[3] != 0;
                s.clockAvailable = true;
            }
        } catch (const std::exception &) {
        }
        DWORD ignored = 0, n = GetFileVersionInfoSizeW(libraryPath_.c_str(), &ignored);
        if (n) {
            std::vector<unsigned char> bytes(n);
            if (GetFileVersionInfoW(libraryPath_.c_str(), 0, n, bytes.data())) {
                VS_FIXEDFILEINFO *version = nullptr;
                UINT size = 0;
                if (VerQueryValueW(bytes.data(), L"\\", (void **)&version, &size) &&
                    size >= sizeof(*version)) {
                    s.driverVersion = std::to_wstring(HIWORD(version->dwFileVersionMS)) + L"." +
                                      std::to_wstring(LOWORD(version->dwFileVersionMS)) + L"." +
                                      std::to_wstring(HIWORD(version->dwFileVersionLS)) + L"." +
                                      std::to_wstring(LOWORD(version->dwFileVersionLS));
                }
            }
        }
        return s;
    }

    State setVolumes(const std::vector<std::pair<int, int>> &values) {
        auto before = read();
        for (auto [index, raw] : values) {
            if (index < 0 || index >= 4) {
                throw std::runtime_error("Unknown channel");
            }
            auto c = before.channels[index];
            if (!c.canVolume || !c.volume || raw < c.minimum || raw > c.maximum || c.step <= 0 ||
                (raw - c.minimum) % c.step) {
                throw std::runtime_error("Unsupported volume value");
            }
        }
        try {
            for (auto [index, raw] : values) {
                auto c = before.channels[index];
                if (raw != *c.volume) {
                    check(api<ChannelWrite>("TUSBAUDIO_SetVolume")(
                              token_, c.direction, c.number, (unsigned short)(short)raw),
                          "SetVolume");
                }
            }
            auto after = read();
            for (auto [index, raw] : values) {
                if (after.channels[index].volume != raw) {
                    throw std::runtime_error("Volume readback mismatch");
                }
            }
            return after;
        } catch (...) {
            for (auto [index, raw] : values) {
                (void)raw;
                auto c = before.channels[index];
                api<ChannelWrite>("TUSBAUDIO_SetVolume")(
                    token_, c.direction, c.number, (unsigned short)(short)*c.volume);
            }
            throw;
        }
    }

    State setMuted(const std::vector<std::pair<int, bool>> &values) {
        auto before = read();
        for (auto [index, muted] : values) {
            (void)muted;
            if (index < 0 || index >= 4 || !before.channels[index].canMute || !before.channels[index].muted) {
                throw std::runtime_error("Mute unavailable");
            }
        }
        try {
            for (auto [index, muted] : values) {
                auto c = before.channels[index];
                check(api<ChannelWrite>("TUSBAUDIO_SetMute")(token_, c.direction, c.number, muted ? 1 : 0),
                      "SetMute");
            }
            auto after = read();
            for (auto [index, muted] : values) {
                if (after.channels[index].muted != muted) {
                    throw std::runtime_error("Mute readback mismatch");
                }
            }
            return after;
        } catch (...) {
            for (auto [index, muted] : values) {
                (void)muted;
                auto c = before.channels[index];
                api<ChannelWrite>("TUSBAUDIO_SetMute")(token_, c.direction, c.number, *c.muted ? 1 : 0);
            }
            throw;
        }
    }

    State apply(unsigned rate, unsigned frames, std::optional<bool> safeMode = std::nullopt) {
        auto before = read();
        if (std::find(before.rates.begin(), before.rates.end(), rate) == before.rates.end() ||
            std::find(before.buffers.begin(), before.buffers.end(), frames) == before.buffers.end()) {
            throw std::runtime_error("Unsupported driver setting");
        }
        try {
            if (rate != before.rate) {
                check(api<Write>("TUSBAUDIO_SetSampleRate")(token_, rate), "SetSampleRate");
            }
            auto current = read();
            unsigned flags =
                safeMode ? (current.flags & ~0x10000u) | (*safeMode ? 0x10000u : 0) : current.flags;
            check(api<Buffer>("TUSBAUDIO_SetASIOBufferPreferredSize")(0, current.bufferRate, frames, flags),
                  "SetASIOBufferPreferredSize");
            auto after = read();
            if (after.rate != rate || after.preferred != frames || after.flags != flags) {
                throw std::runtime_error("Driver settings readback mismatch");
            }
            return after;
        } catch (...) {
            api<Write>("TUSBAUDIO_SetSampleRate")(token_, before.rate);
            api<Buffer>("TUSBAUDIO_SetASIOBufferPreferredSize")(
                0, before.bufferRate, before.preferred, before.flags);
            throw;
        }
    }
};
} // namespace dl
