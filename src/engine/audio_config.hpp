// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "driver_settings.hpp"
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <cmath>

namespace dl {
struct AudioConfig {
    unsigned rate = 0, frames = 0;
    std::optional<bool> safeMode;
};

inline void validateAudioConfig(const AudioConfig &settings) {
    if (settings.rate < 8000 || settings.rate > 192000 || settings.frames < 16 || settings.frames > 2048 ||
        (settings.frames & (settings.frames - 1))) {
        throw std::runtime_error("Invalid saved audio settings");
    }
}

inline std::optional<AudioConfig> loadAudioConfig(const std::filesystem::path &path) {
    if (!std::filesystem::exists(path)) {
        return std::nullopt;
    }
    std::ifstream in(path);
    std::string magic, extra;
    int version = 0;
    AudioConfig result;
    if (!(in >> magic >> version >> result.rate >> result.frames) || magic != "DonnerLiveAudio" ||
        (version != 1 && version != 2)) {
        throw std::runtime_error("Invalid audio configuration");
    }
    if (version == 2) {
        int safe = -1;
        if (!(in >> safe) || (safe != 0 && safe != 1)) {
            throw std::runtime_error("Invalid saved safety mode");
        }
        result.safeMode = safe != 0;
    }
    if (in >> extra) {
        throw std::runtime_error("Unexpected audio configuration data");
    }
    validateAudioConfig(result);
    return result;
}

inline AudioConfig audioConfig(const DonnerSettings::State &state) {
    return {state.rate, state.preferred, (state.flags & 0x10000u) != 0};
}

inline AudioConfig startupAudioConfig(const std::optional<AudioConfig> &saved,
                                      const DonnerSettings::State &current,
                                      int frames = 0,
                                      double rate = 0) {
    auto result = saved.value_or(audioConfig(current));
    if (frames) {
        result.frames = (unsigned)frames;
    }
    if (rate) {
        if (!std::isfinite(rate) || rate < 8000 || rate > 192000 || rate != std::floor(rate)) {
            throw std::runtime_error("Invalid requested sample rate");
        }
        result.rate = (unsigned)rate;
    }
    if (!result.safeMode) {
        result.safeMode = (current.flags & 0x10000u) != 0;
    }
    validateAudioConfig(result);
    return result;
}

inline bool matchesAudioConfig(const AudioConfig &desired, const DonnerSettings::State &state) {
    return desired.rate == state.rate && desired.frames == state.preferred &&
           (!desired.safeMode || *desired.safeMode == ((state.flags & 0x10000u) != 0));
}

inline DonnerSettings::State restoreAudioConfig(DonnerSettings &driver, const AudioConfig &desired) {
    validateAudioConfig(desired);
    auto before = driver.read();
    if (matchesAudioConfig(desired, before)) {
        return before;
    }
    return driver.apply(desired.rate, desired.frames, desired.safeMode);
}

inline void saveAudioConfig(const std::filesystem::path &path, const AudioConfig &settings) {
    validateAudioConfig(settings);
    if (!settings.safeMode) {
        throw std::runtime_error("Safety mode must be read back before saving");
    }
    std::ostringstream out;
    out << "DonnerLiveAudio 2 " << settings.rate << ' ' << settings.frames << ' '
        << (*settings.safeMode ? 1 : 0) << '\n';
    auto content = out.str();
    if (std::filesystem::exists(path)) {
        std::ifstream old(path, std::ios::binary);
        std::string previous((std::istreambuf_iterator<char>(old)), {});
        if (old && !old.bad() && previous == content) {
            return;
        }
    }
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }
    auto temporary = path;
    temporary += L".tmp";
    try {
        {
            std::ofstream file(temporary, std::ios::binary);
            file << content;
            file.flush();
            if (!file) {
                throw std::runtime_error("Audio settings save failed");
            }
            file.close();
            if (!file) {
                throw std::runtime_error("Audio settings close failed");
            }
        }
        if (!MoveFileExW(
                temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            throw std::runtime_error("Audio settings replacement failed");
        }
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        throw;
    }
}
} // namespace dl
