// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "../platform/windows.hpp"
#include <filesystem>
#include <stdexcept>
#include <string>

namespace dl {
inline std::filesystem::path environmentDirectory(const wchar_t *name) {
    const DWORD required = GetEnvironmentVariableW(name, nullptr, 0);
    if (!required) {
        throw std::runtime_error("Required Windows directory is unavailable");
    }
    std::wstring value(required, L'\0');
    const DWORD written = GetEnvironmentVariableW(name, value.data(), required);
    if (!written || written >= required) {
        throw std::runtime_error("Could not read the Windows directory");
    }
    value.resize(written);
    return std::filesystem::path(value);
}

inline std::filesystem::path donnerDriverDirectory() {
    return environmentDirectory(L"ProgramFiles") / L"Donner/USB Audio Device Driver/W10_x64";
}

inline std::filesystem::path virtualDriverDirectory() {
    return environmentDirectory(L"ProgramFiles") / L"Elgato/WaveLinkDriver/DriverSetup/drivers";
}

inline std::filesystem::path donnerAsioLibrary() {
    return donnerDriverDirectory() / L"DonnerUsbAudioasio_x64.dll";
}

inline std::filesystem::path donnerControlLibrary() {
    return donnerDriverDirectory() / L"DonnerUsbAudioapi_x64.dll";
}

inline std::filesystem::path virtualAsioLibrary() {
    return virtualDriverDirectory() / L"ElgatoUsbAudioasio_x64.dll";
}

inline std::filesystem::path virtualControlLibrary() {
    return virtualDriverDirectory() / L"ElgatoUsbAudioapi_x64.dll";
}

inline std::filesystem::path defaultLogDirectory() {
    return environmentDirectory(L"LOCALAPPDATA") / L"DonnerLive/logs" /
           (std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(GetCurrentProcessId()));
}

inline constexpr wchar_t DonnerAsioClsid[] = L"{5FC1A528-B0CB-4ED9-AD02-50502054ED59}";
inline constexpr wchar_t VirtualAsioClsid[] = L"{E2280112-ADA4-4409-AE6A-308925974AA8}";
} // namespace dl
