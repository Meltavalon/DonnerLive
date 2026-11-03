// SPDX-License-Identifier: AGPL-3.0-only
#include "driver_paths.hpp"
#include "runtime.hpp"

namespace dl {
using namespace v2;

void Engine::acquireOwnership() {
    owner_ = CreateMutexW(nullptr, FALSE, EngineMutexName);
    auto ownership = owner_ ? WaitForSingleObject(owner_, 0) : WAIT_FAILED;
    if (ownership != WAIT_OBJECT_0 && ownership != WAIT_ABANDONED) {
        if (owner_) {
            CloseHandle(owner_);
            owner_ = nullptr;
        }
        throw std::runtime_error("Another DonnerLive engine owns the hardware");
    }
}

void Engine::initialiseAudioMapping() {
    map_ = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(Shared), MappingName);
    if (!map_) {
        throw std::runtime_error("Audio mapping creation failed");
    }
    shared_ = (Shared *)MapViewOfFile(map_, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Shared));
    if (!shared_) {
        throw std::runtime_error("Audio mapping failed");
    }
    // New engine epoch invalidates any client attached to an earlier process.
    std::memset((void *)shared_, 0, sizeof(Shared));
    shared_->magic = Magic;
    shared_->version = Version;
    shared_->bytes = sizeof(Shared);
    shared_->enginePid = GetCurrentProcessId();
    shared_->epoch = GetTickCount64();
    shared_->hostInputs = host_.inputs;
    shared_->hostOutputs = host_.outputs;
    for (int c = 0; c < Channels; c++) {
        strncpy(shared_->inputNames[c], host_.inputNames[c].c_str(), 31);
        strncpy(shared_->outputNames[c], host_.outputNames[c].c_str(), 31);
    }
    inputEvent_ = CreateEventW(nullptr, FALSE, FALSE, InputEventName);
    stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, StopEventName);
    if (!inputEvent_ || !stopEvent_) {
        throw std::runtime_error("Audio event creation failed");
    }
    ResetEvent(stopEvent_);
    window_ = CreateWindowExW(0,
                              L"STATIC",
                              L"DonnerLive engine",
                              WS_OVERLAPPED,
                              0,
                              0,
                              0,
                              0,
                              nullptr,
                              nullptr,
                              GetModuleHandleW(nullptr),
                              nullptr);
    if (!window_) {
        throw std::runtime_error("ASIO host window creation failed");
    }
}

void Engine::initialiseRoutingControl(const std::filesystem::path &config) {
    configPath_ = std::filesystem::absolute(config);
    bool existingConfig = std::filesystem::exists(configPath_);
    Routes initial = existingConfig ? loadRoutes(configPath_) : preset();
    initial.revision = 1;
    if (!existingConfig) {
        saveRoutes(configPath_, initial);
    }
    routes_.publish(initial);
    controlMap_ =
        CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(Control), ControlName);
    if (!controlMap_) {
        throw std::runtime_error("Routing control mapping creation failed");
    }
    control_ = (Control *)MapViewOfFile(controlMap_, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Control));
    controlMutex_ = CreateMutexW(nullptr, FALSE, ControlMutexName);
    if (!control_ || !controlMutex_) {
        throw std::runtime_error("Routing control initialization failed");
    }
    auto lock = WaitForSingleObject(controlMutex_, 1000);
    if (lock != WAIT_OBJECT_0 && lock != WAIT_ABANDONED) {
        throw std::runtime_error("Routing control is busy");
    }
    std::memset((void *)control_, 0, sizeof(Control));
    control_->magic = ControlMagic;
    control_->bytes = sizeof(Control);
    control_->enginePid = GetCurrentProcessId();
    control_->epoch = shared_->epoch;
    control_->hostInputs = host_.inputs;
    control_->hostOutputs = host_.outputs;
    control_->current = initial;
    control_->requested = initial;
    control_->request = control_->accepted = 1;
    memcpy(control_->inputNames, shared_->inputNames, sizeof(control_->inputNames));
    memcpy(control_->outputNames, shared_->outputNames, sizeof(control_->outputNames));
    wcsncpy(control_->configPath, configPath_.c_str(), 1023);
    ReleaseMutex(controlMutex_);
}

void Engine::openBackends(DonnerSettings &settings,
                          const AudioConfig &desiredAudio,
                          const DonnerSettings::State &beforeAudio) {
    virtual_.open(dl::virtualAsioLibrary().c_str(),
                  dl::VirtualAsioClsid,
                  window_,
                  v2::VirtualChannels,
                  v2::VirtualChannels,
                  frames_);
    checked(virtual_.driver->canSampleRate(requestedRate_), "Wave Link canSampleRate");
    const auto restoredAudio = restoreAudioConfig(settings, desiredAudio);
    log_ << "{\"event\":\"audioSettings\",\"restored\":"
         << (!matchesAudioConfig(desiredAudio, beforeAudio) ? "true" : "false")
         << ",\"previousRate\":" << beforeAudio.rate << ",\"previousFrames\":" << beforeAudio.preferred
         << ",\"sampleRate\":" << restoredAudio.rate << ",\"bufferFrames\":" << restoredAudio.preferred
         << ",\"safeMode\":" << ((restoredAudio.flags & 0x10000u) ? "true" : "false") << "}" << std::endl;
    hardware_.open(dl::donnerAsioLibrary().c_str(), dl::DonnerAsioClsid, window_, 2, 2, frames_);
    if (requestedRate_ > 0) {
        checked(hardware_.driver->canSampleRate(requestedRate_), "Donner canSampleRate");
        checked(virtual_.driver->canSampleRate(requestedRate_), "Wave Link canSampleRate");
        double oldHardware = hardware_.rate, oldVirtual = virtual_.rate;
        try {
            if (std::abs(oldHardware - requestedRate_) > .1) {
                checked(hardware_.driver->setSampleRate(requestedRate_), "Donner setSampleRate");
            }
            if (std::abs(oldVirtual - requestedRate_) > .1) {
                checked(virtual_.driver->setSampleRate(requestedRate_), "Wave Link setSampleRate");
            }
            checked(hardware_.driver->getSampleRate(&hardware_.rate), "Donner getSampleRate");
            checked(virtual_.driver->getSampleRate(&virtual_.rate), "Wave Link getSampleRate");
            if (std::abs(hardware_.rate - requestedRate_) > .1 ||
                std::abs(virtual_.rate - requestedRate_) > .1) {
                throw std::runtime_error("Sample rate readback mismatch");
            }
        } catch (...) {
            hardware_.driver->setSampleRate(oldHardware);
            virtual_.driver->setSampleRate(oldVirtual);
            throw;
        }
    }
    log_ << "{\"event\":\"backendRates\",\"hardware\":" << hardware_.rate << ",\"virtual\":" << virtual_.rate
         << ",\"resampling\":" << (resample_ ? "true" : "false") << "}" << std::endl;
    if (!resample_ && std::abs(hardware_.rate - virtual_.rate) > 0.1) {
        throw std::runtime_error("Backend sample rates differ; select the same rate in the driver "
                                 "panels, or explicitly enable --resample");
    }
}

void Engine::startAudio(const std::filesystem::path &folder,
                        const std::filesystem::path &audioPath,
                        DonnerSettings &settings,
                        const AudioConfig &desiredAudio) {
    mixer_.apply((folder / (L"mixer-restore-" + std::to_wstring(GetCurrentProcessId()) + L".bin")).wstring());
    active_ = this;
    hardware_.create(&hardwareCallbacks_);
    virtual_.create(&virtualCallbacks_);
    shared_->sampleRate = hardware_.rate;
    shared_->virtualSampleRate = virtual_.rate;
    double ratio = hardware_.rate / virtual_.rate;
    shared_->inputLatency =
        (LONG)std::max((double)hardware_.inputLatency, (virtual_.inputLatency + frames_ * 2) * ratio);
    shared_->outputLatency = (LONG)std::max((double)(hardware_.outputLatency + frames_),
                                            (virtual_.outputLatency * ratio + frames_ * 3));
    shared_->bufferFrames = frames_;
    virtual_.start();
    hardware_.start();
    const auto verifiedAudio = settings.read();
    if (!matchesAudioConfig(desiredAudio, verifiedAudio)) {
        throw std::runtime_error("Audio settings changed during startup; saved settings retained");
    }
    saveAudioConfig(audioPath, audioConfig(verifiedAudio));
    InterlockedExchange(&shared_->engineRunning, 1);
    control_->bufferFrames = frames_;
    shared_->bufferFrames = frames_;
    control_->rate = hardware_.rate;
    control_->resampling = resample_ ? 1 : 0;
    InterlockedExchange(&control_->running, 1);
    log_ << "{\"event\":\"ready\",\"pid\":" << GetCurrentProcessId()
         << ",\"sampleRate\":" << shared_->sampleRate << ",\"bufferFrames\":" << frames_
         << ",\"inputs\":" << host_.inputs << ",\"outputs\":" << host_.outputs
         << ",\"clock\":\"Donner official ASIO\",\"resampling\":" << (resample_ ? "true" : "false") << "}"
         << std::endl;
}

Engine::Engine(bool resample, int frames, double rate, std::ostream &log, std::ostream &errors)
    : log_(log), errors_(errors), frames_(frames), requestedRate_(rate), resample_(resample) {}

Engine::~Engine() {
    close();
}

void Engine::open(const std::filesystem::path &folder, const std::filesystem::path &config) {
    host_ = loadHost(config.parent_path() / L"host.txt");
    acquireOwnership();
    const auto audioPath = std::filesystem::absolute(config).parent_path() / L"audio.txt";
    const auto savedAudio = loadAudioConfig(audioPath);
    DonnerSettings settings;
    const auto beforeAudio = settings.read();
    const auto desiredAudio = startupAudioConfig(savedAudio, beforeAudio, frames_, requestedRate_);
    frames_ = static_cast<int>(desiredAudio.frames);
    requestedRate_ = desiredAudio.rate;
    initialiseAudioMapping();
    initialiseRoutingControl(config);
    openBackends(settings, desiredAudio, beforeAudio);
    startAudio(folder, audioPath, settings, desiredAudio);
}

bool Engine::wait(int milliseconds) {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    auto lock = WaitForSingleObject(controlMutex_, 0);
    if (lock == WAIT_OBJECT_0 || lock == WAIT_ABANDONED) {
        auto now = GetTickCount64();
        if (now - hostCheckTick_ >= 250) {
            hostCheckTick_ = now;
            auto pid = read32(&shared_->clientPid);
            if (pid) {
                auto p = OpenProcess(SYNCHRONIZE, FALSE, (DWORD)pid);
                auto error = p ? 0 : GetLastError();
                bool gone = p ? WaitForSingleObject(p, 0) == WAIT_OBJECT_0 : error == ERROR_INVALID_PARAMETER;
                if (p) {
                    CloseHandle(p);
                }
                if (gone) {
                    InterlockedExchange(&shared_->clientReady, 0);
                    InterlockedExchange(&shared_->bufferInputMask, 0);
                    InterlockedExchange(&shared_->bufferOutputMask, 0);
                    InterlockedCompareExchange(&shared_->clientPid, 0, pid);
                }
            }
        }
        auto usage = readHostUsage(shared_);
        control_->hostInputMask = usage.inputs;
        control_->hostOutputMask = usage.outputs;
        control_->hostUsageMode = usage.mode;
        if (control_->request != control_->accepted) {
            control_->error = 0;
            auto r = control_->requested;
            r.revision = control_->request;
            if (!validRoutes(r)) {
                control_->error = 1;
            } else {
                try {
                    saveRoutes(configPath_, r);
                    if (!routes_.publish(r)) {
                        throw std::runtime_error("Routing snapshot busy");
                    }
                    control_->current = r;
                } catch (const std::exception &error) {
                    control_->error = 2;
                    errors_ << error.what() << std::endl;
                }
            }
            control_->accepted = control_->request;
            log_ << "{\"event\":\"routing\",\"revision\":" << control_->accepted
                 << ",\"error\":" << control_->error << "}" << std::endl;
        }
        ReleaseMutex(controlMutex_);
    }
    return !fault_ && WaitForSingleObject(stopEvent_, milliseconds) == WAIT_TIMEOUT;
}

void Engine::close() {
    if (closed_) {
        return;
    }
    closed_ = true;
    if (shared_) {
        InterlockedExchange(&shared_->engineRunning, 0);
    }
    if (control_) {
        InterlockedExchange(&control_->running, 0);
    }
    if (inputEvent_) {
        SetEvent(inputEvent_);
    }
    hardware_.stop();
    virtual_.stop();
    hardware_.close();
    virtual_.close();
    active_ = nullptr;
    mixer_.restore();
    log_ << "{\"event\":\"mixerRestored\",\"ok\":" << (mixer_.restored ? "true" : "false") << "}"
         << std::endl;
    if (window_) {
        DestroyWindow(window_);
        window_ = nullptr;
    }
    if (shared_) {
        UnmapViewOfFile(shared_);
        shared_ = nullptr;
    }
    for (auto h : {map_, inputEvent_, stopEvent_}) {
        if (h) {
            CloseHandle(h);
        }
    }
    map_ = inputEvent_ = stopEvent_ = nullptr;
    if (control_) {
        UnmapViewOfFile(control_);
        control_ = nullptr;
    }
    if (controlMap_) {
        CloseHandle(controlMap_);
    }
    if (controlMutex_) {
        CloseHandle(controlMutex_);
    }
    controlMap_ = controlMutex_ = nullptr;
    if (owner_) {
        ReleaseMutex(owner_);
        CloseHandle(owner_);
        owner_ = nullptr;
    }
}
} // namespace dl
