// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "../../engine/routing_v2.hpp"
#include "../../engine/host_config.hpp"
#include <vector>

namespace ui {
namespace r = dl::v2;

class Session {
    HANDLE map_ = nullptr, mutex_ = nullptr;
    r::Control *control_ = nullptr;
    uint64_t epoch_ = 0;

  public:
    std::filesystem::path root;
    dl::HostConfig host;
    r::Routes routes{};
    bool online = false, hostActive = false;
    int inputs = dl::Channels, outputs = dl::Channels;
    uint32_t enginePid = 0, hostInputMask = 0, hostOutputMask = 0;
    int hostUsageMode = 0;
    double rate = 0;
    int frames = 0;
    float sourcePeaks[r::Ports]{}, targetPeaks[r::Ports]{};

    explicit Session(std::filesystem::path dir) : root(std::move(dir)) {
        host = dl::loadHost(root / L"config/host.txt");
        auto path = root / L"config/routing.txt";
        routes = std::filesystem::exists(path) ? r::loadRoutes(path) : r::preset();
        inputs = host.inputs;
        outputs = host.outputs;
    }

    ~Session() {
        disconnect();
    }

    Session(const Session &) = delete;
    Session &operator=(const Session &) = delete;

    void disconnect() {
        if (control_) {
            UnmapViewOfFile(control_);
        }
        if (map_) {
            CloseHandle(map_);
        }
        if (mutex_) {
            CloseHandle(mutex_);
        }
        control_ = nullptr;
        map_ = mutex_ = nullptr;
        online = false;
        hostActive = false;
        enginePid = hostInputMask = hostOutputMask = 0;
        hostUsageMode = 0;
    }

    bool refresh() {
        if (!control_) {
            map_ = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, r::ControlName);
            if (!map_) {
                return online = false;
            }
            control_ = (r::Control *)MapViewOfFile(map_, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(r::Control));
            mutex_ = OpenMutexW(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, r::ControlMutexName);
            if (!control_ || !mutex_) {
                disconnect();
                return false;
            }
        }
        auto lock = WaitForSingleObject(mutex_, 30);
        if (lock != WAIT_OBJECT_0 && lock != WAIT_ABANDONED) {
            return online;
        }
        bool live = control_->magic == r::ControlMagic && control_->bytes == sizeof(r::Control) &&
                    dl::read32(&control_->running) == 1;
        HANDLE process = live ? OpenProcess(SYNCHRONIZE, FALSE, control_->enginePid) : nullptr;
        if (process) {
            live = WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
            CloseHandle(process);
        } else if (live) {
            live = GetLastError() == ERROR_ACCESS_DENIED;
        }
        if (live) {
            enginePid = control_->enginePid;
            if (epoch_ != control_->epoch) {
                for (int c = 0; c < dl::Channels; c++) {
                    if (control_->inputNames[c][0]) {
                        host.inputNames[c] =
                            std::string(control_->inputNames[c], strnlen(control_->inputNames[c], 32));
                    }
                    if (control_->outputNames[c][0]) {
                        host.outputNames[c] =
                            std::string(control_->outputNames[c], strnlen(control_->outputNames[c], 32));
                    }
                }
            }
            epoch_ = control_->epoch;
            routes = control_->current;
            inputs = control_->hostInputs;
            outputs = control_->hostOutputs;
            hostInputMask = control_->hostInputMask;
            hostOutputMask = control_->hostOutputMask;
            hostUsageMode = control_->hostUsageMode;
            hostActive = dl::read32(&control_->hostActive) != 0;
            rate = control_->rate;
            frames = control_->bufferFrames;
            for (int c = 0; c < r::Ports; c++) {
                sourcePeaks[c] = InterlockedExchange(&control_->sourcePeak[c], 0) / 1000000.f;
                targetPeaks[c] = InterlockedExchange(&control_->destinationPeak[c], 0) / 1000000.f;
            }
        }
        ReleaseMutex(mutex_);
        if (!live) {
            disconnect();
            inputs = host.inputs;
            outputs = host.outputs;
        }
        return online = live;
    }

    void commit(r::Routes next) {
        if (!r::validRoutes(next)) {
            throw std::runtime_error("Invalid routing gain");
        }

        if (!online) {
            r::saveRoutes(root / L"config/routing.txt", next);
            routes = next;
            return;
        }
        auto lock = WaitForSingleObject(mutex_, 100);
        if (lock != WAIT_OBJECT_0 && lock != WAIT_ABANDONED) {
            throw std::runtime_error("Routing engine is busy");
        }
        if (control_->request != control_->accepted || control_->current.revision != routes.revision ||
            control_->epoch != epoch_ || dl::read32(&control_->running) != 1) {
            ReleaseMutex(mutex_);
            throw std::runtime_error("Routing changed; refresh and retry");
        }
        uint32_t revision = control_->request + 1;
        if (!revision) {
            revision = 1;
        }
        control_->requested = next;
        control_->request = revision;
        ReleaseMutex(mutex_);
        for (int i = 0; i < 100; i++) {
            Sleep(5);
            auto w = WaitForSingleObject(mutex_, 20);
            if (w != WAIT_OBJECT_0 && w != WAIT_ABANDONED) {
                continue;
            }
            bool accepted = control_->accepted == revision;
            bool active = dl::read32(&control_->activeRevision) == (LONG)revision;
            auto error = control_->error;
            ReleaseMutex(mutex_);
            if (accepted && error) {
                throw std::runtime_error("Engine could not apply/save routes");
            }
            if (accepted && active) {
                refresh();
                return;
            }
        }
        throw std::runtime_error("Route update timed out; refresh before retrying");
    }
};
} // namespace ui
