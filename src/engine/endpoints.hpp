// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "../platform/windows.hpp"
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <propvarutil.h>
#include <wrl/client.h>
#include <vector>
#include <string>
#include <stdexcept>
#include <algorithm>

namespace dl {
using Microsoft::WRL::ComPtr;

inline void comCheck(HRESULT hr, const char *op) {
    if (FAILED(hr)) {
        throw std::runtime_error(std::string(op) + " HRESULT=" + std::to_string((unsigned)hr));
    }
}

// Windows policy interface, isolated here so failure never becomes a registry fallback.
struct PolicyConfig : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetMixFormat() = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDeviceFormat() = 0;
    virtual HRESULT STDMETHODCALLTYPE ResetDeviceFormat() = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDeviceFormat() = 0;
    virtual HRESULT STDMETHODCALLTYPE GetProcessingPeriod() = 0;
    virtual HRESULT STDMETHODCALLTYPE SetProcessingPeriod() = 0;
    virtual HRESULT STDMETHODCALLTYPE GetShareMode() = 0;
    virtual HRESULT STDMETHODCALLTYPE SetShareMode() = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPropertyValue() = 0;
    virtual HRESULT STDMETHODCALLTYPE SetPropertyValue() = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDefaultEndpoint(const wchar_t *, ERole) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetEndpointVisibility(const wchar_t *, INT) = 0;
};

struct Endpoint {
    std::wstring id, name, hardware;
    bool playback = false, active = false;
    int channel = -1;
    DWORD state = 0;
};

inline std::wstring property(IPropertyStore *p, const PROPERTYKEY &key) {
    PROPVARIANT v;
    PropVariantInit(&v);
    if (FAILED(p->GetValue(key, &v))) {
        return {};
    }
    std::wstring s;
    if (v.vt == VT_LPWSTR && v.pwszVal) {
        s = v.pwszVal;
    } else if (v.vt == (VT_VECTOR | VT_LPWSTR) && v.calpwstr.cElems) {
        s = v.calpwstr.pElems[0];
    }
    PropVariantClear(&v);
    return s;
}

inline int endpointChannel(const std::wstring &hardware, bool playback) {
    auto text = hardware;
    for (auto &c : text) {
        c = towupper(c);
    }
    if (text.find(L"VID_0FD9") == std::wstring::npos || text.find(L"PID_0096") == std::wstring::npos) {
        return -1;
    }
    auto key = playback ? L"PCM_OUT_00_V_" : L"PCM_IN_01_V_";
    auto pos = text.find(key);
    if (pos == std::wstring::npos) {
        return -1;
    }
    auto number = text.substr(pos + wcslen(key), 2);
    if (number.size() != 2 || !iswdigit(number[0]) || !iswdigit(number[1])) {
        return -1;
    }
    int c = std::stoi(number);
    return c % 2 == 0 && c < (playback ? 16 : 10) ? c : -1;
}

class EndpointChanges final : public IMMNotificationClient {
    LONG refs_ = 1;
    volatile LONG generation_ = 0;

  public:
    LONG consume() {
        return InterlockedExchange(&generation_, 0);
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void **out) override {
        if (!out) {
            return E_POINTER;
        }
        *out = nullptr;
        if (id == IID_IUnknown || id == __uuidof(IMMNotificationClient)) {
            *out = this;
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return InterlockedIncrement(&refs_);
    }

    ULONG STDMETHODCALLTYPE Release() override {
        auto n = InterlockedDecrement(&refs_);
        if (!n) {
            delete this;
        }
        return n;
    }

    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR, DWORD) override {
        InterlockedIncrement(&generation_);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override {
        InterlockedIncrement(&generation_);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override {
        InterlockedIncrement(&generation_);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow, ERole, LPCWSTR) override {
        InterlockedIncrement(&generation_);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override {
        InterlockedIncrement(&generation_);
        return S_OK;
    }
};

class EndpointStore {
    ComPtr<IMMDeviceEnumerator> enumerator_;
    ComPtr<PolicyConfig> policy_;
    ComPtr<EndpointChanges> changes_;
    static constexpr PROPERTYKEY HardwareKey = {
        {0x233164c8, 0x1b2c, 0x4c7d, {0xbc, 0x68, 0xb6, 0x71, 0x68, 0x7a, 0x25, 0x67}}, 1};

    ComPtr<IMMDevice> device(const std::wstring &id) {
        ComPtr<IMMDevice> d;
        comCheck(enumerator_->GetDevice(id.c_str(), &d), "GetDevice");
        return d;
    }

    std::wstring defaultId(EDataFlow flow, ERole role) {
        ComPtr<IMMDevice> d;
        if (FAILED(enumerator_->GetDefaultAudioEndpoint(flow, role, &d))) {
            return {};
        }
        LPWSTR id = nullptr;
        comCheck(d->GetId(&id), "Default endpoint id");
        std::wstring result = id;
        CoTaskMemFree(id);
        return result;
    }

  public:
    EndpointStore() {
        comCheck(
            CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator_)),
            "Endpoint enumeration");
        changes_.Attach(new EndpointChanges);
        comCheck(enumerator_->RegisterEndpointNotificationCallback(changes_.Get()), "Endpoint notifications");
    }

    ~EndpointStore() {
        if (enumerator_ && changes_) {
            enumerator_->UnregisterEndpointNotificationCallback(changes_.Get());
        }
    }

    EndpointStore(const EndpointStore &) = delete;
    EndpointStore &operator=(const EndpointStore &) = delete;

    bool changed() {
        return changes_->consume() != 0;
    }

    std::vector<Endpoint> list() {
        std::vector<Endpoint> out;
        for (int flow = 0; flow < 2; flow++) {
            ComPtr<IMMDeviceCollection> all;
            comCheck(enumerator_->EnumAudioEndpoints((EDataFlow)flow, DEVICE_STATEMASK_ALL, &all),
                     "EnumAudioEndpoints");
            UINT count = 0;
            all->GetCount(&count);
            for (UINT i = 0; i < count; i++) {
                ComPtr<IMMDevice> d;
                ComPtr<IPropertyStore> p;
                if (FAILED(all->Item(i, &d)) || FAILED(d->OpenPropertyStore(STGM_READ, &p))) {
                    continue;
                }
                Endpoint e;
                e.playback = flow == 0;
                e.hardware = property(p.Get(), HardwareKey);
                e.channel = endpointChannel(e.hardware, e.playback);
                if (e.channel < 0) {
                    continue;
                }
                LPWSTR id = nullptr;
                comCheck(d->GetId(&id), "Endpoint id");
                e.id = id;
                CoTaskMemFree(id);
                e.name = property(p.Get(), PKEY_Device_DeviceDesc);
                d->GetState(&e.state);
                e.active = e.state == DEVICE_STATE_ACTIVE;
                out.push_back(e);
            }
        }
        std::sort(out.begin(), out.end(), [](auto &a, auto &b) {
            return a.playback != b.playback ? a.playback > b.playback : a.channel < b.channel;
        });
        return out;
    }

    Endpoint find(const std::wstring &id) {
        for (auto e : list()) {
            if (e.id == id) {
                return e;
            }
        }
        throw std::runtime_error("Endpoint not found or not a supported Wave Link slot");
    }

    bool isDefault(const std::wstring &id) {
        for (int f = 0; f < 2; f++) {
            for (int r = 0; r < 3; r++) {
                ComPtr<IMMDevice> d;
                if (SUCCEEDED(enumerator_->GetDefaultAudioEndpoint((EDataFlow)f, (ERole)r, &d))) {
                    LPWSTR key = nullptr;
                    d->GetId(&key);
                    bool match = key && id == key;
                    CoTaskMemFree(key);
                    if (match) {
                        return true;
                    }
                }
            }
        }
        return false;
    }

    void visibility(const std::wstring &id, bool active) {
        auto before = find(id);
        if (before.active == active) {
            return;
        }
        if (!active && isDefault(id)) {
            throw std::runtime_error(
                "Choose another default device in Windows before removing this endpoint");
        }
        const auto flow = before.playback ? eRender : eCapture;
        std::wstring defaults[3];
        if (active) {
            for (int role = 0; role < 3; role++) {
                defaults[role] = defaultId(flow, (ERole)role);
            }
        }
        if (!policy_) {
            CLSID cls;
            IID iid;
            CLSIDFromString(L"{870af99c-171d-4f9e-af0d-e63df40c2bc9}", &cls);
            CLSIDFromString(L"{f8679f50-850a-41cf-9c72-430f290290c8}", &iid);
            comCheck(CoCreateInstance(cls, nullptr, CLSCTX_ALL, iid, (void **)policy_.GetAddressOf()),
                     "Endpoint policy");
        }
        comCheck(policy_->SetEndpointVisibility(id.c_str(), active ? 1 : 0), "Endpoint visibility");
        try {
            bool matched = false;
            for (int i = 0; i < 20; i++) {
                if (find(id).active == active) {
                    matched = true;
                    break;
                }
                Sleep(25);
            }
            if (!matched) {
                throw std::runtime_error("Endpoint state readback failed");
            }
            // Windows can promote a re-enabled endpoint to a remembered default role.
            // Enabling a slot must preserve existing defaults; only undo promotion to this slot.
            if (active) {
                for (int attempt = 0; attempt < 10; attempt++) {
                    Sleep(25);
                    for (int role = 0; role < 3; role++) {
                        if (!defaults[role].empty() && defaults[role] != id &&
                            defaultId(flow, (ERole)role) == id) {
                            comCheck(policy_->SetDefaultEndpoint(defaults[role].c_str(), (ERole)role),
                                     "Preserve default endpoint");
                            if (defaultId(flow, (ERole)role) != defaults[role]) {
                                throw std::runtime_error("Default endpoint readback mismatch");
                            }
                        }
                    }
                }
            }
        } catch (...) {
            policy_->SetEndpointVisibility(id.c_str(), before.active ? 1 : 0);
            throw;
        }
    }

    void rename(const std::wstring &id, const std::wstring &name) {
        if (name.empty() || name.size() > 64 || name.find_first_of(L"\r\n") != std::wstring::npos) {
            throw std::runtime_error("Device name must contain 1..64 characters");
        }
        auto before = find(id);
        auto d = device(id);
        ComPtr<IPropertyStore> p;
        comCheck(d->OpenPropertyStore(STGM_READWRITE, &p), "Endpoint property write access");
        PROPVARIANT v;
        PropVariantInit(&v);
        comCheck(InitPropVariantFromString(name.c_str(), &v), "Device name");
        auto hr = p->SetValue(PKEY_Device_DeviceDesc, v);
        PropVariantClear(&v);
        comCheck(hr, "Rename endpoint");
        comCheck(p->Commit(), "Commit endpoint name");
        if (find(id).name != name) {
            InitPropVariantFromString(before.name.c_str(), &v);
            p->SetValue(PKEY_Device_DeviceDesc, v);
            p->Commit();
            PropVariantClear(&v);
            throw std::runtime_error("Device name readback mismatch");
        }
    }
};
} // namespace dl
