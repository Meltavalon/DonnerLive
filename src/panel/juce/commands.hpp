// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include <filesystem>

namespace studio {
std::filesystem::path applicationRoot();
int command(const std::filesystem::path &root, const juce::StringArray &args);
} // namespace studio
