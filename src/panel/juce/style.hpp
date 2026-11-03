// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include "../../engine/routing_v2.hpp"

namespace studio {
using namespace juce;
namespace r = dl::v2;
using Box = juce::Rectangle<float>;

inline Colour colour(unsigned v) {
    return Colour(0xff000000u | v);
}

inline String str(const std::wstring &s) {
    return String(s.c_str());
}

inline std::wstring wide(const std::string &s) {
    return String::fromUTF8(s.c_str()).toWideCharPointer();
}

inline String number(float v, int decimals = 1) {
    return decimals == 0 ? String((int)std::round(v)) : String(v, decimals);
}

constexpr unsigned bg = 0x14171b, surface = 0x1c2026, card = 0x252a31, border = 0x353d47, ink = 0xe8ecf0,
                   muted = 0xa6b0bc, mint = 0x9de1c0, blue = 0xaec9f0, amber = 0xe4bc85;
} // namespace studio
