// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "protocol.hpp"
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <array>
#include <string>
#include <stdexcept>

namespace dl {
struct HostConfig {
    int inputs = Channels, outputs = Channels;
    std::array<std::string, Channels> inputNames{}, outputNames{};

    HostConfig() {
        for (int c = 0; c < Channels; c++) {
            inputNames[c] = "Host Input " + std::to_string(c + 1);
            outputNames[c] = "Host Return " + std::to_string(c + 1);
        }
    }
};

inline void validateHost(const HostConfig &c) {
    if (c.inputs < 1 || c.inputs > Channels || c.outputs < 1 || c.outputs > Channels) {
        throw std::runtime_error("Host channel count must be 1..32");
    }
    for (auto *names : {&c.inputNames, &c.outputNames}) {
        for (auto &name : *names) {
            if (name.empty() || name.size() > 31 || name.find_first_of("\r\n") != std::string::npos) {
                throw std::runtime_error("Host names must be 1..31 UTF-8 bytes");
            }
        }
    }
}

inline HostConfig loadHost(const std::filesystem::path &path) {
    HostConfig c;
    if (!std::filesystem::exists(path)) {
        return c;
    }
    std::ifstream f(path);
    std::string magic;
    int version;
    if (!(f >> magic >> version >> c.inputs >> c.outputs) || magic != "DonnerLiveHost" || version != 1) {
        throw std::runtime_error("Invalid host configuration");
    }
    for (int i = 0; i < Channels; i++) {
        if (!(f >> std::quoted(c.inputNames[i]) >> std::quoted(c.outputNames[i]))) {
            throw std::runtime_error("Incomplete host names");
        }
    }
    std::string extra;
    if (f >> extra) {
        throw std::runtime_error("Unexpected host configuration data");
    }
    validateHost(c);
    c.inputs = c.outputs = Channels;
    return c;
}

inline void saveHost(const std::filesystem::path &path, const HostConfig &c) {
    validateHost(c);
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }
    auto tmp = path;
    tmp += L".tmp";
    {
        std::ofstream f(tmp);
        f << "DonnerLiveHost 1\n" << Channels << ' ' << Channels << '\n';
        for (int i = 0; i < Channels; i++) {
            f << std::quoted(c.inputNames[i]) << ' ' << std::quoted(c.outputNames[i]) << '\n';
        }
        f.flush();
        if (!f) {
            throw std::runtime_error("Host configuration save failed");
        }
    }
    if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        throw std::runtime_error("Host configuration replace failed");
    }
}
} // namespace dl
