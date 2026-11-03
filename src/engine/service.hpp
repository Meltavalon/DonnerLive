// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>

namespace dl {
struct EngineOptions {
    std::filesystem::path root, config, logDirectory;
    bool resample = false;
    int frames = 0;
    double rate = 0;
};

class EngineService {
  public:
    enum class State { stopped, starting, running, stopping, failed };
    ~EngineService();
    bool start(EngineOptions options);
    void requestStop();
    void stopAndWait();

    State state() const {
        return state_.load();
    }

    std::string error() const;
    static int signalStop();

  private:
    std::thread worker_;
    std::atomic<State> state_{State::stopped};
    std::atomic<bool> stop_{false};
    mutable std::mutex errorMutex_;
    std::string error_;
};
} // namespace dl
