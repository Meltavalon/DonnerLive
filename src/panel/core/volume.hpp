// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <cmath>

namespace ui {
inline int volumeRaw(int percent) {
    // Original CPL's 101 calibration entries, as documented by the reference panel.
    static constexpr short calibration[] = {
        -32512, -17920, -15104, -13568, -12544, -11520, -11008, -10240, -9728, -9472, -8960, -8704, -8192,
        -7936,  -7680,  -7424,  -7168,  -6912,  -6656,  -6400,  -6144,  -6144, -5888, -5632, -5632, -5376,
        -5120,  -5120,  -4864,  -4864,  -4608,  -4608,  -4352,  -4352,  -4096, -4096, -4096, -3840, -3840,
        -3584,  -3584,  -3584,  -3328,  -3328,  -3072,  -3072,  -2816,  -2816, -2816, -2816, -2816, -2560,
        -2560,  -2560,  -2304,  -2304,  -2304,  -2304,  -2048,  -2048,  -2048, -2048, -1792, -1792, -1792,
        -1792,  -1536,  -1536,  -1536,  -1536,  -1280,  -1280,  -1280,  -1280, -1280, -1024, -1024, -1024,
        -1024,  -1024,  -768,   -768,   -768,   -768,   -768,   -512,   -512,  -512,  -512,  -512,  -512,
        -256,   -256,   -256,   -256,   -256,   -256,   -256,   -256,   -256,  0};
    return calibration[std::clamp(percent, 0, 100)];
}

inline int volumePercent(int raw) {
    int best = 0;
    for (int i = 1; i <= 100; i++) {
        if (std::abs(volumeRaw(i) - raw) < std::abs(volumeRaw(best) - raw)) {
            best = i;
        }
    }
    return best;
}

inline int snapVolume(int raw, int minimum, int maximum, int step) {
    if (step <= 0 || minimum > maximum) {
        return minimum;
    }
    raw = std::clamp(raw, minimum, maximum);
    int steps = (int)std::round((raw - minimum) / (double)step);
    return std::clamp(minimum + steps * step, minimum, maximum);
}
} // namespace ui
