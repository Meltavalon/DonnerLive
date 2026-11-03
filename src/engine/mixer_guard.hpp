// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "../platform/windows.hpp"
#include <cwctype>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <cstring>
#include <vector>
#include "driver_paths.hpp"

namespace dl {
// Own all 24 ASIO sends to the 24 virtual sinks; snapshot every changed cell.
class MixerGuard {
    struct Point {
        uint8_t sourceType, sourceIndex, sinkType, sinkIndex;
        int32_t gain;
    };

    struct Request {
        uint32_t magic = 77, id = 200;
        Point p[576]{};

        Request() {
            for (int s = 0; s < 24; s++) {
                for (int d = 0; d < 24; d++) {
                    p[s * 24 + d] = {(uint8_t)2, (uint8_t)(24 + s), (uint8_t)2, (uint8_t)d, 0};
                }
            }
        }
    };

    HMODULE module_ = nullptr;
    uint32_t handle_ = 0;
    bool changed_ = false;
    using Property = uint32_t(WINAPI *)(uint32_t, void *, uint32_t);
    Property get_ = nullptr, set_ = nullptr;
    uint32_t(WINAPI *close_)(uint32_t) = nullptr;
    Request original_;
    std::vector<char> legacy_;

    template <class T> T load(const char *name) {
        T out = nullptr;
        auto p = GetProcAddress(module_, name);
        memcpy(&out, &p, sizeof(out));
        if (!out) {
            throw std::runtime_error("Mixer export missing");
        }
        return out;
    }

    void connect() {
        const auto library = virtualControlLibrary();
        module_ = LoadLibraryExW(library.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (!module_) {
            throw std::runtime_error("Mixer API load failed");
        }
        auto enumerate = load<uint32_t(WINAPI *)()>("TUSBAUDIO_EnumerateDevices");
        auto count = load<uint32_t(WINAPI *)()>("TUSBAUDIO_GetDeviceCount");
        auto open = load<uint32_t(WINAPI *)(uint32_t, uint32_t *)>("TUSBAUDIO_OpenDeviceByIndex");
        auto instance =
            load<uint32_t(WINAPI *)(uint32_t, wchar_t *, uint32_t)>("TUSBAUDIO_GetDeviceInstanceIdString");
        close_ = load<uint32_t(WINAPI *)(uint32_t)>("TUSBAUDIO_CloseDevice");
        get_ = load<Property>("TUSBAUDIO_GetDspProperty");
        set_ = load<Property>("TUSBAUDIO_SetDspProperty");
        if (enumerate() != 0 || count() != 1 || open(0, &handle_) != 0) {
            throw std::runtime_error("Mixer virtual device selection failed");
        }
        wchar_t raw[1024]{};
        auto status = instance(handle_, raw, 1024);
        raw[1023] = 0;
        std::wstring id = raw;
        for (auto &c : id) {
            c = (wchar_t)towupper(c);
        }
        if (status || id.find(L"VID_0FD9&PID_0096") == std::wstring::npos) {
            throw std::runtime_error("Mixer device is not Elgato Virtual Audio");
        }
    }

  public:
    MixerGuard() = default;
    MixerGuard(const MixerGuard &) = delete;
    MixerGuard &operator=(const MixerGuard &) = delete;

    bool restored = true;

    ~MixerGuard() {
        restore();
    }

    void apply(const std::wstring &snapshot) {
        connect();

        struct Scalar {
            uint32_t magic = 77, id, value = 0;
        } sources{77, 104, 0}, sinks{77, 105, 0};

        if (get_(handle_, &sources, sizeof(sources)) || get_(handle_, &sinks, sizeof(sinks)) ||
            sources.value != 48 || sinks.value != 34) {
            throw std::runtime_error("Unverified virtual mixer topology");
        }
        if (get_(handle_, &original_, sizeof(original_))) {
            throw std::runtime_error("Mixer snapshot query failed");
        }
        HANDLE file = CreateFileW(
            snapshot.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        DWORD written = 0;
        bool saved = file != INVALID_HANDLE_VALUE &&
                     WriteFile(file, &original_, sizeof(original_), &written, nullptr) &&
                     written == sizeof(original_) && FlushFileBuffers(file);
        if (file != INVALID_HANDLE_VALUE) {
            CloseHandle(file);
        }
        if (!saved) {
            throw std::runtime_error("Mixer snapshot save failed");
        }
        Request desired;
        for (int c = 0; c < 24; c++) {
            desired.p[c * 24 + c].gain = 16777216;
        }
        changed_ = true;
        restored = false;
        if (set_(handle_, &desired, sizeof(desired))) {
            throw std::runtime_error("Mixer route write failed");
        }
        Request after;
        if (get_(handle_, &after, sizeof(after)) || memcmp(&after, &desired, sizeof(after))) {
            throw std::runtime_error("Mixer route readback mismatch");
        }
    }

    void recover(const std::wstring &snapshot) {
        Request request, expected;
        HANDLE legacyFile =
            CreateFileW(snapshot.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        if (legacyFile != INVALID_HANDLE_VALUE) {
            if (GetFileSize(legacyFile, nullptr) == 56) {
                legacy_.resize(56);
                DWORD bytes = 0;
                bool read = ReadFile(legacyFile, legacy_.data(), 56, &bytes, nullptr) && bytes == 56;
                CloseHandle(legacyFile);
                uint32_t header[2];
                memcpy(header, legacy_.data(), 8);
                Point keys[6] = {{2, 24, 2, 0, 0},
                                 {2, 25, 2, 1, 0},
                                 {2, 24, 2, 2, 0},
                                 {2, 25, 2, 3, 0},
                                 {2, 26, 2, 2, 0},
                                 {2, 27, 2, 3, 0}};
                if (!read || header[0] != 77 || header[1] != 200) {
                    throw std::runtime_error("Invalid legacy snapshot");
                }
                for (int i = 0; i < 6; i++) {
                    Point point;
                    memcpy(&point, legacy_.data() + 8 + i * 8, 8);
                    if (memcmp(&point, &keys[i], 4) || point.gain < 0 || point.gain > 67108864) {
                        throw std::runtime_error("Invalid legacy crosspoint");
                    }
                }
                connect();
                changed_ = true;
                restored = false;
                restore();
                if (!restored) {
                    throw std::runtime_error("Legacy restore failed");
                }
                return;
            }
            CloseHandle(legacyFile);
        }
        HANDLE file =
            CreateFileW(snapshot.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        DWORD n = 0;
        bool ok = file != INVALID_HANDLE_VALUE && GetFileSize(file, nullptr) == sizeof(request) &&
                  ReadFile(file, &request, sizeof(request), &n, nullptr) && n == sizeof(request);
        if (file != INVALID_HANDLE_VALUE) {
            CloseHandle(file);
        }
        if (!ok || request.magic != 77 || request.id != 200) {
            throw std::runtime_error("Invalid engine mixer restore snapshot");
        }
        for (int i = 0; i < 576; i++) {
            if (memcmp(&request.p[i], &expected.p[i], 4) || request.p[i].gain < 0 ||
                request.p[i].gain > 67108864) {
                throw std::runtime_error("Snapshot has unexpected crosspoint keys or gain");
            }
        }
        connect();
        original_ = request;
        changed_ = true;
        restored = false;
        restore();
        if (!restored) {
            throw std::runtime_error("Mixer snapshot restore readback failed");
        }
    }

    void restore() {
        if (changed_ && !legacy_.empty()) {
            auto after = legacy_;
            auto status = set_(handle_, legacy_.data(), (uint32_t)legacy_.size());
            restored =
                status == 0 && get_(handle_, after.data(), (uint32_t)after.size()) == 0 && after == legacy_;
            changed_ = false;
        }
        if (changed_) {
            Request after;
            auto status = set_(handle_, &original_, sizeof(original_));
            restored = status == 0 && get_(handle_, &after, sizeof(after)) == 0 &&
                       memcmp(&after, &original_, sizeof(after)) == 0;
            changed_ = false;
        }
        if (handle_ && close_) {
            close_(handle_);
            handle_ = 0;
        }
        if (module_) {
            FreeLibrary(module_);
            module_ = nullptr;
        }
    }
};
} // namespace dl
