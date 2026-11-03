// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include "DonnerAsioData.h"
#include "../../engine/protocol.hpp"

namespace studio {
inline constexpr wchar_t AppShowEvent[] = L"Local\\DonnerLive.Application.Show";
inline constexpr wchar_t AppQuitEvent[] = L"Local\\DonnerLive.Application.Quit";

inline bool signalApplication(const wchar_t *name) {
    auto event = OpenEventW(EVENT_MODIFY_STATE, FALSE, name);
    if (!event) {
        return false;
    }
    bool result = SetEvent(event) != 0;
    CloseHandle(event);
    return result;
}

inline juce::Image trayIcon() {
    juce::Image image(juce::Image::ARGB, 32, 32, true, juce::SoftwareImageType());
    juce::Graphics g(image);
    g.setColour(juce::Colour(0xff1c2026));
    g.fillRoundedRectangle(1, 1, 30, 30, 7);
    g.setColour(juce::Colour(0xff9de1c0));
    for (int i = 0; i < 3; i++) {
        g.fillRoundedRectangle(8.f + i * 7, i == 1 ? 7.f : 11.f, 2.5f, i == 1 ? 18.f : 10.f, 1.2f);
    }
    return image;
}

class ApplicationTray final : public juce::SystemTrayIconComponent {
    std::function<void()> restore_, exit_;

  public:
    ApplicationTray(std::function<void()> restore, std::function<void()> exit)
        : restore_(std::move(restore)), exit_(std::move(exit)) {
        setIconImage(trayIcon(), {});
        setIconTooltip(juce::String::fromUTF8("DonnerLive · 点击显示主界面"));
    }

    void activate(int action) {
        if (action == 1) {
            restore_();
        }
        if (action == 2) {
            exit_();
        }
    }

    juce::PopupMenu menu() const {
        juce::PopupMenu result;
        result.addItem(1, juce::String::fromUTF8("显示主界面"));
        result.addSeparator();
        result.addItem(2, juce::String::fromUTF8("完全退出"));
        return result;
    }

    void mouseDown(const juce::MouseEvent &event) override {
        if (event.mods.isPopupMenu()) {
            auto safe = juce::Component::SafePointer<ApplicationTray>(this);
            auto p = juce::Desktop::getMousePosition();
            menu().showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea({p.x, p.y, 1, 1}),
                                 [safe](int id) {
                                     if (safe) {
                                         safe->activate(id);
                                     }
                                 });
        } else if (event.mods.isLeftButtonDown()) {
            restore_();
        }
    }

    void mouseDoubleClick(const juce::MouseEvent &event) override {
        if (!event.mods.isPopupMenu()) {
            restore_();
        }
    }
};

inline juce::File installBundledAsio() {
    int size = 0;
    auto *bytes = DonnerAsio::getNamedResource(DonnerAsio::namedResourceList[0], size);
    if (!bytes || size <= 0) {
        throw std::runtime_error("Embedded ASIO driver is missing");
    }
    auto target = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
                      .getSiblingFile("DonnerLive.ASIO.dll");
    juce::MemoryBlock current;
    if (target.loadFileAsData(current) && current.getSize() == (size_t)size &&
        std::memcmp(current.getData(), bytes, (size_t)size) == 0) {
        return target;
    }
    juce::TemporaryFile temporary(target);
    if (!temporary.getFile().replaceWithData(bytes, (size_t)size) ||
        !temporary.overwriteTargetFileWithTemporary()) {
        throw std::runtime_error("ASIO driver could not be updated; close the DAW and reopen DonnerLive");
    }
    return target;
}

inline juce::String registryString(const wchar_t *path, const wchar_t *name) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0, KEY_READ | KEY_WOW64_64KEY, &key) != ERROR_SUCCESS) {
        return {};
    }
    wchar_t value[32768]{};
    DWORD type = 0, size = sizeof(value);
    auto status = RegQueryValueExW(key, name, nullptr, &type, (BYTE *)value, &size);
    RegCloseKey(key);
    return status == ERROR_SUCCESS && type == REG_SZ ? juce::String(value) : juce::String{};
}

inline bool asioRegistered(const juce::File &dll) {
    return juce::File(registryString(
               L"SOFTWARE\\Classes\\CLSID\\{6223CC56-3DB6-48D1-8CC0-A3CFB72683BE}\\InprocServer32", L"")) ==
               dll &&
           registryString(L"SOFTWARE\\ASIO\\DonnerLive ASIO", L"CLSID") ==
               "{6223CC56-3DB6-48D1-8CC0-A3CFB72683BE}";
}

} // namespace studio
