// SPDX-License-Identifier: AGPL-3.0-only
#include "commands.hpp"
#include "style.hpp"
#include "../core/session.hpp"
#include "../../engine/driver_settings.hpp"
#include "../../engine/driver_paths.hpp"
#include <iostream>

namespace studio {
std::filesystem::path applicationRoot() {
    auto exe = File::getSpecialLocation(File::currentExecutableFile);
    auto dir = exe.getParentDirectory();
    for (int i = 0; i < 8; i++) {
        if (dir.getChildFile("config").isDirectory() || dir.getChildFile("src/engine").isDirectory()) {
            return std::filesystem::path(dir.getFullPathName().toWideCharPointer());
        }
        dir = dir.getParentDirectory();
    }
    return dl::environmentDirectory(L"LOCALAPPDATA") / L"DonnerLive";
}

int command(const std::filesystem::path &root, const StringArray &args) {
    ui::Session session(root);
    auto action = args[0];

    if (action == "--device-status") {
        dl::DonnerSettings device;
        auto s = device.read();
        std::cout << "{\"sampleRate\":" << s.rate << ",\"preferredFrames\":" << s.preferred
                  << ",\"flags\":" << s.flags << ",\"inputLatencySamples\":" << s.inputLatency
                  << ",\"outputLatencySamples\":" << s.outputLatency << "}\n";
        return 0;
    }

    session.refresh();

    if (action == "--status" || action == "--snapshot") {
        std::cout << "{\"online\":" << (session.online ? "true" : "false")
                  << ",\"hostInputs\":" << session.inputs << ",\"hostOutputs\":" << session.outputs
                  << ",\"hostInputMask\":" << session.hostInputMask
                  << ",\"hostOutputMask\":" << session.hostOutputMask
                  << ",\"hostUsageMode\":" << session.hostUsageMode
                  << ",\"hostActive\":" << (session.hostActive ? "true" : "false")
                  << ",\"sampleRate\":" << session.rate << ",\"bufferFrames\":" << session.frames
                  << ",\"revision\":" << session.routes.revision << ",\"routes\":[";
        bool first = true;
        for (int s = 0; s < r::Ports; s++) {
            for (int d = 0; d < r::Ports; d++) {
                if (session.routes.gain[s][d] > 0) {
                    if (!first) {
                        std::cout << ',';
                    }
                    first = false;
                    std::cout << '[' << s + 1 << ',' << d + 1 << ',' << session.routes.gain[s][d] << ']';
                }
            }
        }
        std::cout << "]}\n";
        return 0;
    }
    auto next = session.routes;
    if (action == "--preset" && args.size() == 2) {
        if (args[1] != "host" && args[1] != "direct" && args[1] != "mute") {
            throw std::runtime_error("Unknown preset");
        }
        next = r::preset(args[1] == "host" ? 0 : args[1] == "direct" ? 1 : 2);
    } else if (action == "--load" && args.size() == 2) {
        next = r::loadRoutes(std::filesystem::path(args[1].toWideCharPointer()));
    } else if ((action == "--connect" || action == "--set") && args.size() == 4) {
        int s = args[1].getIntValue() - 1, d = args[2].getIntValue() - 1;
        bool legacy = action == "--set";
        if (s < 0 || d < 0 || s >= (legacy ? 6 : r::Ports) || d >= (legacy ? 6 : r::Ports)) {
            throw std::runtime_error("Invalid port");
        }
        float gain = 0;
        if (args[3] != "off") {
            auto val = args[3].toStdString();
            size_t used = 0;
            float db = std::stof(val, &used);
            if (used != val.size() || !std::isfinite(db) || db < -96 || db > 6) {
                throw std::runtime_error("Gain must be -96..6 dB or off");
            }
            gain = std::pow(10.f, db / 20);
        }
        if (legacy) {
            for (int c = 0; c < 2; c++) {
                next.gain[r::oldSource(s) + c][r::oldTarget(d) + c] = gain;
            }
        } else {
            next.gain[s][d] = gain;
        }
    } else {
        throw std::runtime_error("Usage: --status | --device-status | --connect source destination dB/off | "
                                 "--preset host/direct/mute | --load file");
    }
    session.commit(next);
    std::cout << "{\"saved\":true}\n";
    return 0;
}
} // namespace studio
