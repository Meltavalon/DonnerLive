// SPDX-License-Identifier: AGPL-3.0-only
#include "workbench.hpp"
#include "commands.hpp"
#include "application_support.hpp"
#include "../../engine/mixer_guard.hpp"

namespace studio {
class App final : public JUCEApplication, private Timer {
    class Window final : public DocumentWindow {
        std::function<void()> close_;

      public:
        Window(Workbench *ui, std::function<void()> close)
            : DocumentWindow("DonnerLive", colour(bg), allButtons), close_(std::move(close)) {
            setUsingNativeTitleBar(true);
            setContentOwned(ui, true);
            setResizable(true, false);
            setResizeLimits(1200, 800, 3840, 2160);
            centreWithSize(1440, 900);
            setIcon(trayIcon());
            if (auto *peer = getPeer()) {
                peer->setCurrentRenderingEngine(0);
            }
            setVisible(true);
            if (auto *peer = getPeer()) {
                using Apply = HRESULT(WINAPI *)(HWND, DWORD, LPCVOID, DWORD);
                if (auto module = LoadLibraryW(L"dwmapi.dll")) {
                    auto apply = (Apply)GetProcAddress(module, "DwmSetWindowAttribute");
                    BOOL dark = TRUE;
                    DWORD caption = RGB(28, 32, 38);
                    if (apply) {
                        apply((HWND)peer->getNativeHandle(), 20, &dark, sizeof(dark));
                        apply((HWND)peer->getNativeHandle(), 35, &caption, sizeof(caption));
                    }
                    FreeLibrary(module);
                }
            }
        }

        void closeButtonPressed() override {
            close_();
        }
    };

    std::unique_ptr<Window> window;
    std::unique_ptr<ApplicationTray> tray;
    std::unique_ptr<AlertWindow> closeDialog;
    Workbench *workbench = nullptr;
    dl::EngineService engine;
    dl::EngineOptions options;
    LookAndFeel_V4 applicationLook;
    HANDLE mutex = nullptr, showEvent = nullptr, quitEvent = nullptr;
    bool headless = false, quitting = false;
    dl::EngineService::State previousState = dl::EngineService::State::stopped;

    dl::EngineOptions parseOptions(const std::filesystem::path &root, const StringArray &args) {
        dl::EngineOptions result;
        result.root = root;
        for (int i = 0; i < args.size(); i++) {
            const auto &a = args[i];
            if (a == "--engine" || a == "--tray") {
                continue;
            }
            if (a == "--resample") {
                result.resample = true;
                continue;
            }
            if (i + 1 >= args.size()) {
                throw std::runtime_error("Missing startup option value");
            }
            auto value = args[++i];
            if (a == "--frames") {
                result.frames = value.getIntValue();
            } else if (a == "--rate") {
                result.rate = value.getDoubleValue();
            } else if (a == "--routing-config") {
                result.config = std::filesystem::path(value.toWideCharPointer());
            } else if (a == "--log-dir") {
                result.logDirectory = std::filesystem::path(value.toWideCharPointer());
            } else {
                throw std::runtime_error("Unknown startup option");
            }
        }
        if ((result.frames && (result.frames < 16 || result.frames > dl::MaxFrames ||
                               (result.frames & (result.frames - 1)))) ||
            result.rate < 0 || result.rate > 192000 || !std::isfinite(result.rate)) {
            throw std::runtime_error("Invalid audio startup parameters");
        }
        return result;
    }

    void makeWindow(const std::filesystem::path &root) {
        applicationLook.setDefaultSansSerifTypefaceName("Microsoft YaHei UI");
        applicationLook.setColour(AlertWindow::backgroundColourId, colour(surface));
        applicationLook.setColour(AlertWindow::textColourId, colour(ink));
        applicationLook.setColour(AlertWindow::outlineColourId, colour(border));
        applicationLook.setColour(TextButton::buttonColourId, colour(0x304339));
        applicationLook.setColour(TextButton::textColourOffId, colour(ink));
        LookAndFeel::setDefaultLookAndFeel(&applicationLook);
        workbench = new Workbench(root);

        workbench->toggleEngine = [this] {
            auto state = engine.state();
            if (state == dl::EngineService::State::starting || state == dl::EngineService::State::running) {
                if (workbench->session.hostActive) {
                    throw std::runtime_error("请先关闭 DAW 的音频设备，再停止引擎或完全退出程序");
                }
                engine.requestStop();
            } else {
                engine.start(options);
            }
        };
        workbench->engineState = [this] { return engine.state(); };
        window = std::make_unique<Window>(workbench, [this] { systemRequestedQuit(); });
    }

    void showWindow() {
        if (window) {
            window->setVisible(true);
            window->setMinimised(false);
            window->toFront(true);
            if (closeDialog) {
                closeDialog->toFront(true);
            }
        }
    }

    void hideWindow() {
        if (window && tray) {
            window->setVisible(false);
            tray->setIconTooltip(String::fromUTF8("DonnerLive · 后台运行，点击显示主界面"));
        }
    }

    void requestClose() {
        if (quitting || closeDialog) {
            return;
        }
        if (window) {
            window->toFront(true);
        }
        closeDialog = std::make_unique<AlertWindow>(
            String::fromUTF8("关闭 DonnerLive"),
            String::fromUTF8(
                "隐藏到系统托盘后，音频和 ASIO 连接会继续运行。\n完全退出将停止音频并释放设备。"),
            MessageBoxIconType::NoIcon,
            window.get());
        closeDialog->setLookAndFeel(&applicationLook);
        closeDialog->addButton(String::fromUTF8("隐藏到托盘"), 1, KeyPress(KeyPress::returnKey));
        closeDialog->addButton(String::fromUTF8("完全退出"), 2);
        closeDialog->addButton(String::fromUTF8("取消"), 0, KeyPress(KeyPress::escapeKey));
        auto safeWindow = Component::SafePointer<Window>(window.get());
        closeDialog->enterModalState(true,
                                     ModalCallbackFunction::create([this, safeWindow](int choice) {
                                         if (!safeWindow || quitting) {
                                             return;
                                         }
                                         closeDialog.reset();
                                         if (choice == 1) {
                                             hideWindow();
                                         } else if (choice == 2) {
                                             exitCompletely();
                                         }
                                     }),
                                     false);
    }

    void exitCompletely() {
        if (quitting) {
            return;
        }
        quitting = true;
        stopTimer();
        if (closeDialog) {
            closeDialog->exitModalState(0);
            closeDialog.reset();
        }
        engine.stopAndWait();

        tray.reset();
        window.reset();
        workbench = nullptr;
        quit();
    }

    void timerCallback() override {
        if (quitting) {
            return;
        }
        try {
            if (quitEvent && WaitForSingleObject(quitEvent, 0) == WAIT_OBJECT_0) {
                exitCompletely();
                return;
            }
            if (showEvent && WaitForSingleObject(showEvent, 0) == WAIT_OBJECT_0) {
                showWindow();
            }
            const auto state = engine.state();
            if (state != previousState) {
                previousState = state;
                if (state == dl::EngineService::State::failed) {
                    if (workbench) {
                        workbench->notify(String::fromUTF8(engine.error().c_str()), true);
                    }
                    if (headless) {
                        throw std::runtime_error(engine.error());
                    }
                }
            }
            if (headless &&
                (state == dl::EngineService::State::stopped || state == dl::EngineService::State::failed)) {
                exitCompletely();
                return;
            }
        } catch (const std::exception &e) {
            std::cerr << e.what() << std::endl;
            setApplicationReturnValue(1);
            exitCompletely();
        }
    }

  public:
    const String getApplicationName() override {
        return "DonnerLive";
    }

    const String getApplicationVersion() override {
        return "1.0.0";
    }

    bool moreThanOneInstanceAllowed() override {
        return true;
    }

    void initialise(const String &commandLine) override {
        try {
            StringArray args;
            args.addTokens(commandLine, true);
            args.removeEmptyStrings();
            for (auto &a : args) {
                a = a.unquoted();
            }
            auto root = applicationRoot();

            if (args.size() > 0 && args[0] == "--quit") {
                setApplicationReturnValue(signalApplication(AppQuitEvent) ? 0 : 3);
                quit();
                return;
            }
            if (args.size() > 0 && args[0] == "--stop") {
                setApplicationReturnValue(dl::EngineService::signalStop());
                quit();
                return;
            }
            if (args.size() == 2 && args[0] == "--restore-mixer") {
                dl::MixerGuard guard;
                guard.recover(args[1].toWideCharPointer());
                std::cout << "{\"mixerRestored\":true}\n";
                quit();
                return;
            }

            const bool engineCommand = args.size() > 0 && args[0] == "--engine";
            const bool trayStart = args.size() > 0 && args[0] == "--tray";
            const bool startupFlags =
                args.size() > 0 && (args[0] == "--frames" || args[0] == "--rate" || args[0] == "--resample" ||
                                    args[0] == "--log-dir" || args[0] == "--routing-config");
            if (args.size() > 0 && !engineCommand && !trayStart && !startupFlags) {
                setApplicationReturnValue(command(root, args));
                quit();
                return;
            }

            options = parseOptions(root, args);

            if (!engineCommand) {
                mutex = CreateMutexW(nullptr, TRUE, r::PanelMutexName);
                if (!mutex || GetLastError() == ERROR_ALREADY_EXISTS) {
                    signalApplication(AppShowEvent);

                    quit();
                    return;
                }
                makeWindow(root);

                showEvent = CreateEventW(nullptr, FALSE, FALSE, AppShowEvent);
                quitEvent = CreateEventW(nullptr, FALSE, FALSE, AppQuitEvent);
                tray =
                    std::make_unique<ApplicationTray>([this] { showWindow(); }, [this] { exitCompletely(); });
                auto dll = installBundledAsio();
                if (!asioRegistered(dll)) {
                    workbench->notify(
                        String::fromUTF8("DonnerLive ASIO 尚未安装；如需在 DAW 中使用，请运行 Setup 安装程序。"),
                        true);
                }
                if (trayStart) {
                    hideWindow();
                }
            }

            headless = engineCommand;
            engine.start(options);
            startTimer(100);
        } catch (const std::exception &e) {
            std::cerr << e.what() << std::endl;
            setApplicationReturnValue(1);
            exitCompletely();
        }
    }

    void shutdown() override {
        stopTimer();
        quitting = true;
        if (closeDialog) {
            closeDialog->exitModalState(0);
            closeDialog.reset();
        }
        engine.stopAndWait();
        tray.reset();
        window.reset();
        workbench = nullptr;
        LookAndFeel::setDefaultLookAndFeel(nullptr);
        if (mutex) {
            ReleaseMutex(mutex);
            CloseHandle(mutex);
            mutex = nullptr;
        }
        for (auto event : {showEvent, quitEvent}) {
            if (event) {
                CloseHandle(event);
            }
        }
        showEvent = quitEvent = nullptr;
    }

    void systemRequestedQuit() override {
        requestClose();
    }

    void anotherInstanceStarted(const String &) override {
        showWindow();
    }
};
} // namespace studio
START_JUCE_APPLICATION(studio::App)
