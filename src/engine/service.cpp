// SPDX-License-Identifier: AGPL-3.0-only
#include "service.hpp"
#include "runtime.hpp"
#include "driver_paths.hpp"

namespace dl {
EngineService::~EngineService() {
    stopAndWait();
}

std::string EngineService::error() const {
    std::lock_guard<std::mutex> lock(errorMutex_);
    return error_;
}

bool EngineService::start(EngineOptions options) {
    const auto current = state();
    if (current == State::starting || current == State::running || current == State::stopping) {
        return false;
    }
    if (worker_.joinable()) {
        worker_.join();
    }
    {
        std::lock_guard<std::mutex> lock(errorMutex_);
        error_.clear();
    }
    stop_ = false;
    state_ = State::starting;
    worker_ = std::thread([this, options] {
        const auto com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        auto directory = options.logDirectory;
        try {
            if (FAILED(com)) {
                throw std::runtime_error("Audio thread initialization failed");
            }
            if (directory.empty()) {
                directory = defaultLogDirectory();
            }
            std::filesystem::create_directories(directory);
            std::ofstream log(directory / L"engine.jsonl"), errors(directory / L"engine.stderr.txt");
            if (!log || !errors) {
                throw std::runtime_error("Audio log directory is not writable");
            }
            auto engine =
                std::make_unique<Engine>(options.resample, options.frames, options.rate, log, errors);
            engine->open(directory,
                         options.config.empty() ? options.root / L"config/routing.txt" : options.config);
            state_ = stop_ ? State::stopping : State::running;
            while (!stop_ && engine->wait(5)) {
            }
            state_ = State::stopping;
            engine->close();
            state_ = State::stopped;
        } catch (const std::exception &e) {
            {
                std::ofstream errors(directory / L"engine.stderr.txt", std::ios::app);
                if (errors) {
                    errors << e.what() << std::endl;
                }
            }
            {
                std::lock_guard<std::mutex> lock(errorMutex_);
                error_ = e.what();
            }
            state_ = State::failed;
        }
        if (SUCCEEDED(com)) {
            CoUninitialize();
        }
    });
    return true;
}

void EngineService::requestStop() {
    stop_ = true;
    if (state() == State::running) {
        state_ = State::stopping;
    }
}

void EngineService::stopAndWait() {
    requestStop();
    if (worker_.joinable()) {
        worker_.join();
    }
}

int EngineService::signalStop() {
    auto event = OpenEventW(EVENT_MODIFY_STATE, FALSE, StopEventName);
    if (!event) {
        return 3;
    }
    const bool ok = SetEvent(event) != 0;
    CloseHandle(event);
    return ok ? 0 : 1;
}
} // namespace dl
