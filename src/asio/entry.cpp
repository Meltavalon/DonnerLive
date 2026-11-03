// SPDX-License-Identifier: AGPL-3.0-only
#include "driver.hpp"
#include <new>

namespace dl::asio {
HMODULE moduleHandle = nullptr;
std::atomic<long> liveObjects{0}, serverLocks{0};

class Factory final : public IClassFactory {
    std::atomic<ULONG> refs_{1};

  public:
    Factory() {
        liveObjects++;
    }

    ~Factory() {
        liveObjects--;
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override {
        if (!out) {
            return E_POINTER;
        }
        *out = nullptr;
        if (IsEqualIID(iid, IID_IUnknown) || IsEqualIID(iid, IID_IClassFactory)) {
            *out = this;
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return ++refs_;
    }

    ULONG STDMETHODCALLTYPE Release() override {
        ULONG n = --refs_;
        if (!n) {
            delete this;
        }
        return n;
    }

    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown *outer, REFIID iid, void **out) override {
        if (!out) {
            return E_POINTER;
        }
        *out = nullptr;
        if (outer) {
            return CLASS_E_NOAGGREGATION;
        }
        try {
            auto *driver = new Driver;
            const auto result = driver->QueryInterface(iid, out);
            driver->Release();
            return result;
        } catch (const std::bad_alloc &) {
            return E_OUTOFMEMORY;
        } catch (...) {
            return E_UNEXPECTED;
        }
    }

    HRESULT STDMETHODCALLTYPE LockServer(BOOL lock) override {
        serverLocks += lock ? 1 : -1;
        return S_OK;
    }
};
} // namespace dl::asio

using namespace dl::asio;

STDAPI DllGetClassObject(REFCLSID clsid, REFIID iid, void **out) {
    if (!out) {
        return E_POINTER;
    }
    *out = nullptr;
    if (!IsEqualCLSID(clsid, DriverId)) {
        return CLASS_E_CLASSNOTAVAILABLE;
    }
    auto *factory = new (std::nothrow) Factory;
    if (!factory) {
        return E_OUTOFMEMORY;
    }
    const auto result = factory->QueryInterface(iid, out);
    factory->Release();
    return result;
}

STDAPI DllCanUnloadNow() {
    return liveObjects.load() == 0 && serverLocks.load() == 0 ? S_OK : S_FALSE;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        moduleHandle = instance;
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}
