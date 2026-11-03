// SPDX-License-Identifier: AGPL-3.0-only
#include "driver.hpp"
#include <avrt.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <shellapi.h>

namespace dl::asio {

void Driver::runCallbacks() {
    DWORD task = 0;
    HANDLE av = AvSetMmThreadCharacteristicsW(L"Pro Audio", &task);
    auto epoch = shared_->epoch;
    LONG64 last = dl::read64(&shared_->latestInput);
    while (!stopRequested_.load(std::memory_order_acquire)) {
        if (WaitForSingleObject(event_, 100) != WAIT_OBJECT_0) {
            if (!dl::valid(shared_)) {
                break;
            }
            continue;
        }
        if (stopRequested_.load(std::memory_order_acquire) || !dl::valid(shared_) ||
            shared_->epoch != epoch) {
            break;
        }
        LONG64 seq = dl::read64(&shared_->latestInput);
        if (seq <= last) {
            continue;
        }
        if (seq - last > 1) {
            InterlockedExchangeAdd64(&shared_->hostSkipped, seq - last - 1);
        }
        last = seq;
        auto &slot = shared_->slots[seq % dl::Slots];
        if (dl::read64(&slot.inputSequence) != seq) {
            continue;
        }
        int index = (int)(seq & 1);
        for (int c = 0; c < shared_->hostInputs; c++) {
            memcpy(input_[index][c], slot.input[c], sizeof(float) * shared_->bufferFrames);
        }
        samplePosition_ = slot.samplePosition;
        systemNanoseconds_ = slot.systemNanoseconds;
        MemoryBarrier();
        if (dl::read64(&slot.inputSequence) != seq) {
            continue;
        }
        std::memset(output_[index], 0, sizeof(output_[index]));
        if (supportsTimeInfo_) {
            ASIOTime time{};
            time.timeInfo.speed = 1;
            time.timeInfo.sampleRate = shared_->sampleRate;
            time.timeInfo.samplePosition.hi = (ULONG)(samplePosition_.load() >> 32);
            time.timeInfo.samplePosition.lo = (ULONG)samplePosition_.load();
            time.timeInfo.systemTime.hi = (ULONG)(systemNanoseconds_.load() >> 32);
            time.timeInfo.systemTime.lo = (ULONG)systemNanoseconds_.load();
            time.timeInfo.flags = kSystemTimeValid | kSamplePositionValid | kSampleRateValid | kSpeedValid;
            callbacks_.bufferSwitchTimeInfo(&time, index, ASIOTrue);
        } else {
            callbacks_.bufferSwitch(index, ASIOTrue);
        }
        if (stopRequested_.load(std::memory_order_acquire)) {
            break;
        }
        for (int c = 0; c < shared_->hostOutputs; c++) {
            memcpy(slot.output[c], output_[index][c], sizeof(float) * shared_->bufferFrames);
        }
        InterlockedExchange64(&slot.outputSequence, seq);
        InterlockedExchange64(&shared_->latestOutput, seq);
        InterlockedIncrement64(&shared_->hostCallbacks);
    }
    if (av) {
        AvRevertMmThreadCharacteristics(av);
    }
}

Driver::Driver() {
    liveObjects++;
}

Driver::~Driver() {
    stop();
    disposeBuffers();
    if (shared_) {
        UnmapViewOfFile(shared_);
    }
    if (mapping_) {
        CloseHandle(mapping_);
    }
    if (event_) {
        CloseHandle(event_);
    }
    liveObjects--;
}

HRESULT STDMETHODCALLTYPE Driver::QueryInterface(REFIID iid, void **out) {
    if (!out) {
        return E_POINTER;
    }
    *out = nullptr;
    if (IsEqualIID(iid, IID_IUnknown) || IsEqualIID(iid, DriverId)) {
        *out = static_cast<IASIO *>(this);
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE Driver::AddRef() {
    return ++refs_;
}

ULONG STDMETHODCALLTYPE Driver::Release() {
    ULONG n = --refs_;
    if (!n) {
        delete this;
    }
    return n;
}

ASIOBool Driver::init(void *) {
    if (initialized_) {
        return ASIOTrue;
    }
    if (shared_) {
        UnmapViewOfFile(shared_);
        shared_ = nullptr;
    }
    if (mapping_) {
        CloseHandle(mapping_);
        mapping_ = nullptr;
    }
    if (event_) {
        CloseHandle(event_);
        event_ = nullptr;
    }
    mapping_ = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, dl::MappingName);
    if (!mapping_) {
        return ASIOFalse;
    }
    shared_ = (dl::Shared *)MapViewOfFile(mapping_, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(dl::Shared));
    event_ = OpenEventW(SYNCHRONIZE, FALSE, dl::InputEventName);
    initialized_ = dl::valid(shared_) && event_;
    if (initialized_) {
        error_.clear();
    }

    return initialized_ ? ASIOTrue : ASIOFalse;
}

void Driver::getDriverName(char *name) {
    strcpy(name, "DonnerLive ASIO");
}

long Driver::getDriverVersion() {
    return 0x00010000;
}

void Driver::getErrorMessage(char *text) {
    strncpy(text, error_.c_str(), 123);
    text[123] = 0;
}

ASIOError Driver::start() {
    if (!buffersCreated_ || !dl::valid(shared_)) {
        return ASE_NotPresent;
    }
    if (!stopRequested_.load()) {
        return ASE_OK;
    }
    LONG pid = (LONG)GetCurrentProcessId();
    LONG owner = InterlockedCompareExchange(&shared_->clientPid, pid, 0);
    if (owner && !(ownsClient_ && owner == pid)) {
        HANDLE p = OpenProcess(SYNCHRONIZE, FALSE, (DWORD)owner);
        DWORD openError = p ? 0 : GetLastError();
        bool gone = p ? WaitForSingleObject(p, 0) == WAIT_OBJECT_0 : openError == ERROR_INVALID_PARAMETER;
        if (p) {
            CloseHandle(p);
        }
        if (!gone) {
            error_ = "Another ASIO host instance is connected";
            return ASE_HWMalfunction;
        }
        if (InterlockedCompareExchange(&shared_->clientPid, pid, owner) != owner) {
            return ASE_HWMalfunction;
        }
    }
    ownsClient_ = true;
    InterlockedExchange(&shared_->bufferInputMask, (LONG)inputMask_);
    InterlockedExchange(&shared_->bufferOutputMask, (LONG)outputMask_);
    if (callbackThread_.joinable()) {
        callbackThread_.join();
    }
    for (auto &slot : shared_->slots) {
        InterlockedExchange64(&slot.outputSequence, 0);
    }
    InterlockedExchange64(&shared_->latestOutput, 0);
    stopRequested_ = false;
    try {
        callbackThread_ = std::thread([this] { runCallbacks(); });
    } catch (...) {
        stopRequested_ = true;
        InterlockedExchange(&shared_->clientReady, 0);
        InterlockedExchange(&shared_->bufferInputMask, 0);
        InterlockedExchange(&shared_->bufferOutputMask, 0);
        InterlockedCompareExchange(&shared_->clientPid, 0, pid);
        ownsClient_ = false;
        return ASE_HWMalfunction;
    }
    InterlockedExchange(&shared_->clientReady, 1);

    return ASE_OK;
}

ASIOError Driver::stop() {
    stopRequested_ = true;
    if (shared_ && ownsClient_ && dl::read32(&shared_->clientPid) == (LONG)GetCurrentProcessId()) {
        InterlockedExchange(&shared_->clientReady, 0);
    }
    if (callbackThread_.joinable()) {
        if (callbackThread_.get_id() == std::this_thread::get_id()) {
            return ASE_InvalidMode;
        }
        callbackThread_.join();
    }
    return ASE_OK;
}

ASIOError Driver::getChannels(long *in, long *out) {
    if (!in || !out) {
        return ASE_InvalidParameter;
    }
    if (!dl::valid(shared_)) {
        return ASE_NotPresent;
    }
    *in = shared_->hostInputs;
    *out = shared_->hostOutputs;
    return ASE_OK;
}

ASIOError Driver::getLatencies(long *in, long *out) {
    if (!in || !out) {
        return ASE_InvalidParameter;
    }
    if (!shared_) {
        return ASE_NotPresent;
    }
    *in = shared_->inputLatency;
    *out = shared_->outputLatency;
    return ASE_OK;
}

ASIOError Driver::getBufferSize(long *min, long *max, long *preferred, long *granularity) {
    if (!min || !max || !preferred || !granularity) {
        return ASE_InvalidParameter;
    }
    if (!shared_ || !dl::valid(shared_)) {
        return ASE_NotPresent;
    }
    *min = *max = *preferred = shared_->bufferFrames;
    *granularity = 0;
    return ASE_OK;
}

ASIOError Driver::canSampleRate(ASIOSampleRate rate) {
    return shared_ && std::abs(rate - shared_->sampleRate) < 0.1 ? ASE_OK : ASE_NoClock;
}

ASIOError Driver::getSampleRate(ASIOSampleRate *rate) {
    if (!rate) {
        return ASE_InvalidParameter;
    }
    if (!shared_) {
        return ASE_NotPresent;
    }
    *rate = shared_->sampleRate;
    return ASE_OK;
}

ASIOError Driver::setSampleRate(ASIOSampleRate rate) {
    return canSampleRate(rate);
}

ASIOError Driver::getClockSources(ASIOClockSource *clocks, long *count) {
    if (!count) {
        return ASE_InvalidParameter;
    }
    if (clocks && *count > 0) {
        clocks[0] = {};
        clocks[0].index = 0;
        clocks[0].associatedChannel = -1;
        clocks[0].associatedGroup = -1;
        clocks[0].isCurrentSource = ASIOTrue;
        strcpy(clocks[0].name, "Donner hardware clock");
    }
    *count = 1;
    return ASE_OK;
}

ASIOError Driver::setClockSource(long reference) {
    return reference == 0 ? ASE_OK : ASE_InvalidParameter;
}

ASIOError Driver::getSamplePosition(ASIOSamples *sample, ASIOTimeStamp *stamp) {
    if (!sample || !stamp) {
        return ASE_InvalidParameter;
    }
    uint64_t p = samplePosition_.load(), t = systemNanoseconds_.load();
    sample->hi = (ULONG)(p >> 32);
    sample->lo = (ULONG)p;
    stamp->hi = (ULONG)(t >> 32);
    stamp->lo = (ULONG)t;
    return ASE_OK;
}

ASIOError Driver::getChannelInfo(ASIOChannelInfo *info) {
    if (!info || info->channel < 0 ||
        (!shared_ || info->channel >= (info->isInput ? shared_->hostInputs : shared_->hostOutputs))) {
        return ASE_InvalidParameter;
    }
    info->isActive =
        buffersCreated_ && ((info->isInput ? inputMask_ : outputMask_) & (uint32_t(1) << info->channel))
            ? ASIOTrue
            : ASIOFalse;
    info->channelGroup = info->channel / 2;
    info->type = ASIOSTFloat32LSB;
    auto *name = (info->isInput ? shared_->inputNames : shared_->outputNames)[info->channel];
    if (*name) {
        strncpy(info->name, name, 31);
        info->name[31] = 0;
    } else {
        snprintf(info->name, 32, "Host %s %ld", info->isInput ? "Input" : "Return", info->channel + 1);
    }
    return ASE_OK;
}

ASIOError Driver::createBuffers(ASIOBufferInfo *info, long count, long frames, ASIOCallbacks *callbacks) {
    if (!initialized_ || !dl::valid(shared_)) {
        return ASE_NotPresent;
    }
    if (!info || count < 1 || count > dl::Channels * 2 || frames != shared_->bufferFrames || !callbacks) {
        return ASE_InvalidParameter;
    }
    if (buffersCreated_) {
        return ASE_InvalidMode;
    }
    bool used[2][dl::Channels]{};
    for (int i = 0; i < count; i++) {
        int direction = info[i].isInput ? 1 : 0;
        long c = info[i].channelNum;
        if (c < 0 || c >= (direction ? shared_->hostInputs : shared_->hostOutputs) || used[direction][c]) {
            return ASE_InvalidParameter;
        }
        used[direction][c] = true;
    }
    callbacks_ = *callbacks;
    supportsTimeInfo_ = callbacks_.asioMessage && callbacks_.bufferSwitchTimeInfo &&
                        callbacks_.asioMessage(kAsioSupportsTimeInfo, 0, nullptr, nullptr) == 1;
    if (!supportsTimeInfo_ && !callbacks_.bufferSwitch) {
        return ASE_InvalidParameter;
    }
    inputMask_ = outputMask_ = 0;
    for (int c = 0; c < dl::Channels; c++) {
        if (used[1][c]) {
            inputMask_ |= uint32_t(1) << c;
        }
        if (used[0][c]) {
            outputMask_ |= uint32_t(1) << c;
        }
    }
    memset(input_, 0, sizeof(input_));
    memset(output_, 0, sizeof(output_));
    for (int i = 0; i < count; i++) {
        for (int k = 0; k < 2; k++) {
            info[i].buffers[k] = info[i].isInput ? (void *)input_[k][info[i].channelNum]
                                                 : (void *)output_[k][info[i].channelNum];
        }
    }
    buffersCreated_ = true;

    return ASE_OK;
}

ASIOError Driver::disposeBuffers() {
    auto r = stop();
    if (r != ASE_OK) {
        return r;
    }
    buffersCreated_ = false;
    callbacks_ = {};
    inputMask_ = outputMask_ = 0;
    if (shared_ && ownsClient_ && dl::read32(&shared_->clientPid) == (LONG)GetCurrentProcessId()) {
        InterlockedExchange(&shared_->bufferInputMask, 0);
        InterlockedExchange(&shared_->bufferOutputMask, 0);
        InterlockedExchange(&shared_->clientPid, 0);
    }
    ownsClient_ = false;
    return ASE_OK;
}

ASIOError Driver::controlPanel() {
    wchar_t path[32768]{};
    if (!GetModuleFileNameW(moduleHandle, path, 32768)) {
        return ASE_NotPresent;
    }
    auto panel = std::filesystem::path(path).parent_path() / L"DonnerLive.exe";
    if (!std::filesystem::exists(panel)) {
        return ASE_NotPresent;
    }
    return (INT_PTR)ShellExecuteW(nullptr, L"open", panel.c_str(), nullptr, nullptr, SW_SHOWNORMAL) > 32
               ? ASE_OK
               : ASE_HWMalfunction;
}

ASIOError Driver::future(long, void *) {
    return ASE_NotPresent;
}

ASIOError Driver::outputReady() {
    return ASE_OK;
}
} // namespace dl::asio
