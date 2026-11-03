// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <cstdint>
#include "../platform/windows.hpp"
#include <objbase.h>
#include <vector>
#include <string>
#include <stdexcept>
#include <cstring>
#include <algorithm>
#include <cmath>
#include "../asio/sdk.hpp"

namespace dl {
inline bool supportsFrames(long frames, long minimum, long maximum, long preferred, long granularity) {
    return frames >= minimum && frames <= maximum && (granularity != 0 || frames == preferred) &&
           (granularity <= 0 || (frames - minimum) % granularity == 0) &&
           (granularity >= 0 || !(frames & (frames - 1)));
}

inline void checked(ASIOError r, const char *operation) {
    if (r != ASE_OK) {
        throw std::runtime_error(std::string(operation) + " returned " + std::to_string(r));
    }
}

class Backend {
    HMODULE module_ = nullptr;

  public:
    Backend() = default;
    Backend(const Backend &) = delete;
    Backend &operator=(const Backend &) = delete;

    IASIO *driver = nullptr;
    std::vector<ASIOBufferInfo> buffers;
    std::vector<ASIOSampleType> types;
    std::vector<float> inputScratch, outputScratch;
    int activeInputs = 0, activeOutputs = 0;
    long inputs = 0, outputs = 0, inputLatency = 0, outputLatency = 0;
    double rate = 0;
    bool allocated = false, started = false;
    int frames = 256;
    long bufferMinimum = 0, bufferMaximum = 0, bufferPreferred = 0, bufferGranularity = 0;

    ~Backend() {
        close();
    }

    void open(const wchar_t *path,
              const wchar_t *clsid,
              void *window,
              int needIn,
              int needOut,
              int requestedFrames = 256) {
        if (module_ || driver) {
            throw std::runtime_error("ASIO backend is already open");
        }
        frames = requestedFrames > 0 ? requestedFrames : 256;
        module_ = LoadLibraryExW(path, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (!module_) {
            throw std::runtime_error("Backend DLL load failed: " + std::to_string(GetLastError()));
        }
        using FactoryProc = HRESULT(WINAPI *)(REFCLSID, REFIID, void **);
        FactoryProc get = nullptr;
        auto p = GetProcAddress(module_, "DllGetClassObject");
        memcpy(&get, &p, sizeof(get));
        if (!get) {
            throw std::runtime_error("Backend class factory missing");
        }
        CLSID id;
        if (FAILED(CLSIDFromString(clsid, &id))) {
            throw std::runtime_error("Invalid ASIO backend class identifier");
        }
        IClassFactory *f = nullptr;
        HRESULT hr = get(id, IID_IClassFactory, (void **)&f);
        if (FAILED(hr)) {
            throw std::runtime_error("Backend DllGetClassObject failed");
        }
        hr = f->CreateInstance(nullptr, id, (void **)&driver);
        f->Release();
        if (FAILED(hr)) {
            throw std::runtime_error("Backend CreateInstance failed");
        }
        if (!driver->init(window)) {
            char error[256]{};
            driver->getErrorMessage(error);
            throw std::runtime_error(std::string("Backend init: ") + error);
        }
        checked(driver->getChannels(&inputs, &outputs), "getChannels");
        checked(driver->getSampleRate(&rate), "getSampleRate");
        char driverName[64]{};
        driver->getDriverName(driverName);
        if (inputs < needIn || outputs < needOut) {
            throw std::runtime_error("Backend has fewer channels than required");
        }
        activeInputs = needIn;
        activeOutputs = needOut;
        inputScratch.resize(needIn * frames);
        outputScratch.resize(needOut * frames);
        long min = 0, max = 0, pref = 0, gran = 0;
        checked(driver->getBufferSize(&min, &max, &pref, &gran), "getBufferSize");
        bufferMinimum = min;
        bufferMaximum = max;
        bufferPreferred = pref;
        bufferGranularity = gran;
        if (requestedFrames > 0 && !supportsFrames(frames, min, max, pref, gran)) {
            throw std::runtime_error("Backend does not support requested buffer frames");
        }
        for (int input = 1; input >= 0; input--) {
            for (int c = 0; c < (input ? needIn : needOut); c++) {
                ASIOChannelInfo info{};
                info.isInput = input;
                info.channel = c;
                checked(driver->getChannelInfo(&info), "getChannelInfo");
                if (info.type != ASIOSTFloat32LSB && info.type != ASIOSTInt32LSB) {
                    throw std::runtime_error(std::string(driverName) + ": unsupported channel type " +
                                             std::to_string(info.type));
                }
                ASIOBufferInfo b{};
                b.isInput = input;
                b.channelNum = c;
                buffers.push_back(b);
                types.push_back(info.type);
            }
        }
    }

    void create(ASIOCallbacks *callbacks) {
        checked(driver->createBuffers(buffers.data(), (long)buffers.size(), frames, callbacks),
                "createBuffers");
        allocated = true;
        for (auto &b : buffers) {
            if (!b.isInput) {
                for (auto ptr : b.buffers) {
                    memset(ptr, 0, frames * sizeof(float));
                }
            }
        }
        checked(driver->getLatencies(&inputLatency, &outputLatency), "getLatencies");
    }

    float *input(int c, int) {
        return inputScratch.data() + c * frames;
    }

    float *output(int c, int) {
        return outputScratch.data() + c * frames;
    }

    void readInputs(int index) {
        for (int c = 0; c < activeInputs; c++) {
            auto *destination = input(c, index);
            auto *source = buffers[c].buffers[index];
            if (types[c] == ASIOSTFloat32LSB) {
                memcpy(destination, source, frames * sizeof(float));
            } else {
                for (int f = 0; f < frames; f++) {
                    int32_t v;
                    memcpy(&v, (char *)source + f * 4, 4);
                    destination[f] = (float)(v / 2147483648.0);
                }
            }
        }
    }

    void writeOutputs(int index) {
        for (int c = 0; c < activeOutputs; c++) {
            auto *source = output(c, index);
            auto *destination = buffers[activeInputs + c].buffers[index];
            if (types[activeInputs + c] == ASIOSTFloat32LSB) {
                memcpy(destination, source, frames * sizeof(float));
            } else {
                for (int f = 0; f < frames; f++) {
                    double value = std::isfinite(source[f])
                                       ? std::clamp((double)source[f], -1.0, 1.0 - 1.0 / 2147483648.0)
                                       : 0;
                    int32_t v = (int32_t)std::llround(value * 2147483648.0);
                    memcpy((char *)destination + f * 4, &v, 4);
                }
            }
        }
    }

    void start() {
        checked(driver->start(), "start");
        started = true;
    }

    void stop() {
        if (started) {
            driver->stop();
            started = false;
        }
    }

    void close() {
        stop();
        if (allocated) {
            driver->disposeBuffers();
            allocated = false;
        }
        if (driver) {
            driver->Release();
            driver = nullptr;
        }
        if (module_) {
            FreeLibrary(module_);
            module_ = nullptr;
        }
        buffers.clear();
        types.clear();
        inputScratch.clear();
        outputScratch.clear();
        activeInputs = activeOutputs = 0;
    }
};
} // namespace dl
