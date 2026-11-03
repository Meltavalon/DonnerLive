// SPDX-License-Identifier: AGPL-3.0-only
#include "../../engine/driver_paths.hpp"
#include "workbench.hpp"
#include "../core/volume.hpp"
#include "../../engine/audio_config.hpp"
#include "../../engine/asio_backend.hpp"

namespace studio {
void Workbench::hit(Box b, Action a, int x, int y, bool enabled, String tip) {
    hits.push_back({b, a, x, y, enabled, tip});
}

void Workbench::message(String s, bool error) {
    notice = (error ? String::fromUTF8("操作未完成：") : String{}) + s;
    noticeUntil = static_cast<uint64>(Time::getMillisecondCounterHiRes()) + 7000;
    repaint();
}

float Workbench::gain() const {
    return selectedSource >= 0 && selectedDestination >= 0
               ? session.routes.gain[selectedSource][selectedDestination]
               : 0;
}

String Workbench::portLabel(bool source, int id) const {
    for (auto &p : ports) {
        if (p.source == source && p.id == id) {
            return str(p.name);
        }
    }
    return String::fromUTF8("通道 ") + String(id + 1);
}

void Workbench::buildPorts() {
    ports.clear();
    ports.push_back({0, 0, true, L"输入 1", L"Livejack", mint});
    ports.push_back({1, 0, true, L"输入 2", L"Livejack", mint});
    for (auto &e : devices) {
        if (e.active && e.playback) {
            for (int c = 0; c < 2; c++) {
                ports.push_back(
                    {2 + e.channel + c, 0, true, e.name + (c ? L" · R" : L" · L"), L"应用播放", 0xb9d6c7});
            }
        }
    }
    for (int c = 0; c < session.inputs; c++) {
        ports.push_back(
            {c, 1, false, wide(session.host.inputNames[c]), L"ASIO IN " + std::to_wstring(c + 1), blue});
    }
    for (int c = 0; c < session.outputs; c++) {
        ports.push_back({r::HostSource + c,
                         2,
                         true,
                         wide(session.host.outputNames[c]),
                         L"ASIO OUT " + std::to_wstring(c + 1),
                         blue});
    }
    for (int c = 0; c < 2; c++) {
        ports.push_back({r::HardwareTarget + c, 3, false, c ? L"输出 R" : L"输出 L", L"Livejack", amber});
    }
    for (auto &e : devices) {
        if (e.active && !e.playback) {
            for (int c = 0; c < 2; c++) {
                ports.push_back({r::VirtualTarget + e.channel + c,
                                 3,
                                 false,
                                 e.name + (c ? L" · R" : L" · L"),
                                 L"虚拟录音",
                                 amber});
            }
        }
    }
}

std::vector<Port> Workbench::visiblePatchPorts() const {
    if (session.online) {
        return hostUsagePorts(ports, session.hostInputMask, session.hostOutputMask);
    }
    return connectedHostPorts(ports, [this](int s, int d) {
        return session.routes.gain[s][d] > 0 || mutedGains.count(s * r::Ports + d) > 0;
    });
}

void Workbench::reloadDevices() {
    if (!endpointStore) {
        endpointStore = std::make_unique<dl::EndpointStore>();
    }
    devices = endpointStore->list();

    if (deviceIndex >= (int)devices.size()) {
        deviceIndex = -1;
    }
    buildPorts();
}

void Workbench::commitWithUndo(r::Routes next) {
    auto old = session.routes;
    session.commit(next);
    history.push_back(old);
    if (history.size() > 64) {
        history.erase(history.begin());
    }
    repaint();
}

void Workbench::undo() {
    if (history.empty()) {
        return;
    }
    auto next = history.back();
    session.commit(next);
    history.pop_back();
    mutedGains.clear();
    message(String::fromUTF8("已撤销上一次路由修改"));
}

void Workbench::connectPorts(int s, int d) {
    auto next = session.routes;
    next.gain[s][d] = 1;
    if (stereoLinked && s % 2 == 0 && d % 2 == 0) {
        bool hasS = false, hasD = false;
        for (auto &p : ports) {
            hasS |= p.source && p.id == s + 1;
            hasD |= !p.source && p.id == d + 1;
        }
        if (hasS && hasD) {
            next.gain[s + 1][d + 1] = 1;
        }
    }
    commitWithUndo(next);
    selectedSource = s;
    selectedDestination = d;
    wireAnchor = -1;
    draggingWire = false;
}

void Workbench::setGain(float db) {
    if (selectedSource < 0 || selectedDestination < 0) {
        return;
    }
    auto next = session.routes;
    next.gain[selectedSource][selectedDestination] = std::pow(10.f, std::clamp(db, -96.f, 6.f) / 20);
    commitWithUndo(next);
}

void Workbench::readDeviceSettings() {
    dl::DonnerSettings d;
    device = d.read();
    deviceAvailable = true;

    draftRate = device.rate;
    draftFrames = device.preferred;
    draftSafe = (device.flags & 0x10000) != 0;
}

bool Workbench::audioBusy() const {
    if (!engineState) {
        return session.online;
    }
    auto state = engineState();
    return session.online || state == dl::EngineService::State::starting ||
           state == dl::EngineService::State::running || state == dl::EngineService::State::stopping;
}

void Workbench::applyAudioSettings() {
    if (audioBusy()) {
        throw std::runtime_error("请先停止引擎并关闭宿主音频设备");
    }
    if (!deviceAvailable) {
        return;
    }

    dl::Backend virtualBackend;
    virtualBackend.open(dl::virtualAsioLibrary().c_str(),
                        dl::VirtualAsioClsid,
                        (HWND)getPeer()->getNativeHandle(),
                        0,
                        0,
                        draftFrames);
    dl::checked(virtualBackend.driver->canSampleRate(draftRate), "Wave Link sample rate");
    virtualBackend.close();
    dl::DonnerSettings d;
    auto before = device;
    device = d.apply(draftRate, draftFrames, draftSafe);
    try {
        dl::saveAudioConfig(session.root / L"config/audio.txt", dl::audioConfig(device));
    } catch (...) {
        device = d.apply(before.rate, before.preferred, (before.flags & 0x10000) != 0);
        throw;
    }
    message(String::fromUTF8("音频参数已应用，驱动回读确认"));
}

void Workbench::writeVolume(int i, int percent) {
    auto &c = device.channels[i];
    int raw = ui::snapVolume(ui::volumeRaw(percent), c.minimum, c.maximum, c.step);
    std::vector<std::pair<int, int>> values{{i, raw}};
    if (outputVolumesLinked && i >= 2) {
        auto &other = device.channels[5 - i];
        values.push_back({5 - i, ui::snapVolume(raw, other.minimum, other.maximum, other.step)});
    }

    dl::DonnerSettings d;
    device = d.setVolumes(values);
}

void Workbench::edit(String initial, Box bounds, std::function<void(String)> save) {
    editor = std::make_unique<TextEditor>();
    editor->setMultiLine(false);
    editor->setFont(Font(FontOptions("Microsoft YaHei UI", 13, Font::plain)));
    editor->setText(initial, false);
    editor->setSelectAllWhenFocused(true);
    editor->setColour(TextEditor::backgroundColourId, colour(0x2b343c));
    editor->setColour(TextEditor::textColourId, colour(ink));
    editor->setColour(TextEditor::outlineColourId, colour(mint));
    editor->setColour(TextEditor::highlightColourId, colour(0x456957));
    editCommit = std::move(save);
    addAndMakeVisible(*editor);
    editor->setBounds(bounds.toNearestInt());
    editor->grabKeyboardFocus();
    editor->selectAll();
    editor->onReturnKey = [this] {
        auto val = editor->getText();
        guard([&] { editCommit(val); });
        editor.reset();
        grabKeyboardFocus();
        repaint();
    };
    editor->onEscapeKey = [this] {
        editor.reset();
        grabKeyboardFocus();
        repaint();
    };
}

void Workbench::openMenu(std::vector<std::pair<String, Hit>> items, Box anchorBox) {
    menu = std::move(items);
    focusedHit = -1;
    float w = 212, h = (float)menu.size() * 36 + 12;
    menuBounds = {jlimit(12.f, (float)getWidth() - w - 12, anchorBox.getRight() - w),
                  jmax(70.f, jmin(anchorBox.getBottom() + 6, (float)getHeight() - h - 30)),
                  w,
                  h};
}

void Workbench::execute(Hit h) {
    if (!h.enabled) {
        return;
    }
    menu.clear();
    guard([&] {
        switch (h.action) {
        case Nav:
            page = h.a;
            tableScroll = scroll = 0;
            wireAnchor = -1;
            editor.reset();
            focusedHit = -1;
            break;
        case View:
            view = h.a;
            tableScroll = scroll = 0;
            wireAnchor = -1;
            break;
        case Pair:
            stereoLinked = !stereoLinked;
            break;
        case Focus:
            focusSelectedRoute = !focusSelectedRoute;
            break;
        case Undo:
            undo();
            break;
        case Reset:
            for (auto &m : sources) {
                m.reset();
            }
            for (auto &m : targets) {
                m.reset();
            }
            break;
        case Presets:
            openMenu({{String::fromUTF8("DAW 宿主处理"), {{}, MenuPreset, 0}},
                      {String::fromUTF8("虚拟设备直通"), {{}, MenuPreset, 1}},
                      {String::fromUTF8("清空全部连接"), {{}, MenuPreset, 2}}},
                     h.box);
            break;
        case MenuPreset:
            commitWithUndo(r::preset(h.a));
            mutedGains.clear();
            selectedSource = selectedDestination = -1;
            break;
        case SelectPort: {
            selectedPort = h.a;
            selectedRow = h.b ? (h.a >= r::HostSource ? 2 : 0) : (h.a < r::HardwareTarget ? 1 : 3);
            if (wireAnchor < 0) {
                wireAnchor = h.a;
                anchorSource = h.b != 0;
                selectedSource = selectedDestination = -1;
            } else if (anchorSource != (h.b != 0)) {
                connectPorts(anchorSource ? wireAnchor : h.a, anchorSource ? h.a : wireAnchor);
            } else {
                wireAnchor = h.a;
                anchorSource = h.b != 0;
            }
            break;
        }
        case SelectWire:
            selectedSource = h.a;
            selectedDestination = h.b;
            selectedPort = h.a;
            selectedRow = h.a >= r::HostSource ? 2 : 0;
            wireAnchor = -1;
            break;
        case GainEdit:
            edit(gain() > 0 ? number(20 * std::log10(gain())) : "0", h.box, [this](String value) {
                std::string s = value.toStdString();
                size_t used = 0;
                float db = std::stof(s, &used);
                if (used != s.size() || !std::isfinite(db) || db < -96 || db > 6) {
                    throw std::runtime_error("请输入 −96 到 +6 dB");
                }
                setGain(db);
            });
            break;
        case Mute: {
            int key = selectedSource * r::Ports + selectedDestination;
            auto next = session.routes;
            if (gain() > 0) {
                mutedGains[key] = gain();
                next.gain[selectedSource][selectedDestination] = 0;
            } else if (mutedGains.count(key)) {
                next.gain[selectedSource][selectedDestination] = mutedGains[key];
                mutedGains.erase(key);
            }
            commitWithUndo(next);
            break;
        }
        case Disconnect:
            if (selectedSource >= 0 && selectedDestination >= 0) {
                auto next = session.routes;
                next.gain[selectedSource][selectedDestination] = 0;
                commitWithUndo(next);
                mutedGains.erase(selectedSource * r::Ports + selectedDestination);
            }
            break;
        case Scroll:
            scroll = jmax(0, scroll + h.a);
            break;
        case HostName:
            edit(String::fromUTF8((h.b ? hostDraft.outputNames : hostDraft.inputNames)[h.a].c_str()),
                 h.box,
                 [this, h](String val) {
                     auto draft = hostDraft;
                     (h.b ? draft.outputNames : draft.inputNames)[h.a] = val.toStdString();
                     dl::validateHost(draft);
                     hostDraft = draft;
                 });
            break;
        case HostSave:
            if (audioBusy()) {
                throw std::runtime_error("请先停止引擎并关闭宿主音频设备");
            }
            dl::validateHost(hostDraft);

            dl::saveHost(session.root / L"config/host.txt", hostDraft);

            session.host = hostDraft;
            session.inputs = hostDraft.inputs;
            session.outputs = hostDraft.outputs;
            buildPorts();
            message(String::fromUTF8("宿主设置已保存，下次启动引擎生效"));
            break;
        case DeviceSelect:
            deviceIndex = h.a;
            break;
        case DeviceRename:
            if (deviceIndex >= 0) {
                auto i = deviceIndex;
                edit(str(devices[i].name),
                     {48, 262.f + (i - tableScroll) * 40, getWidth() * .36f - 48, 38},
                     [this, i](String val) {
                         if (val.isEmpty() || val.length() > 64) {
                             throw std::runtime_error("设备名称需为 1–64 个字符");
                         }

                         endpointStore->rename(devices[i].id, val.toWideCharPointer());

                         reloadDevices();
                     });
            }
            break;
        case DeviceToggle:
            toggleDevice();
            break;
        case DeviceAdd: {
            std::vector<std::pair<String, Hit>> choices;
            for (int i = 0; i < (int)devices.size(); i++) {
                if (!devices[i].active) {
                    choices.push_back(
                        {(devices[i].playback ? String::fromUTF8("播放 · ") : String::fromUTF8("录音 · ")) +
                             str(devices[i].name),
                         {{}, DeviceToggle, i, 1}});
                }
            }
            if (choices.empty()) {
                message(String::fromUTF8("没有空闲虚拟设备槽位"));
            } else {
                openMenu(choices, h.box);
            }
            break;
        }
        case Refresh:
            if (page == 3) {
                readDeviceSettings();
                message(String::fromUTF8("声卡状态已重新读取"));
            } else {
                reloadDevices();
            }
            break;
        case Rate: {
            std::vector<std::pair<String, Hit>> choices;
            for (auto v : device.rates) {
                choices.push_back({String(v) + " Hz", {{}, MenuRate, (int)v}});
            }
            openMenu(choices, h.box);
            break;
        }
        case Frames: {
            std::vector<std::pair<String, Hit>> choices;
            for (auto v : device.buffers) {
                choices.push_back({String(v) + String::fromUTF8(" 采样帧"), {{}, MenuFrames, (int)v}});
            }
            openMenu(choices, h.box);
            break;
        }
        case MenuRate:
            draftRate = h.a;
            break;
        case MenuFrames:
            draftFrames = h.a;
            break;
        case Safety:
            draftSafe = !draftSafe;
            break;
        case Apply:
            applyAudioSettings();
            break;
        case Link:
            outputVolumesLinked = !outputVolumesLinked;
            break;
        case ChannelMute: {
            bool mute = !(device.channels[h.a].muted && *device.channels[h.a].muted);
            std::vector<std::pair<int, bool>> values{{h.a, mute}};
            if (outputVolumesLinked && h.a >= 2) {
                values.push_back({5 - h.a, mute});
            }

            dl::DonnerSettings d;
            device = d.setMuted(values);

            break;
        }
        case Engine:
            if (toggleEngine) {
                toggleEngine();
            }
            break;
        default:
            break;
        }
    });
    repaint();
}

void Workbench::toggleDevice() {
    if (deviceIndex < 0) {
        return;
    }
    auto &e = devices[deviceIndex];
    bool active = !e.active;
    auto before = session.routes, next = before;
    if (!active) {
        for (int c = 0; c < 2; c++) {
            for (int p = 0; p < r::Ports; p++) {
                if (e.playback) {
                    next.gain[2 + e.channel + c][p] = 0;
                } else {
                    next.gain[p][r::VirtualTarget + e.channel + c] = 0;
                }
            }
        }
    }
    session.commit(next);
    try {
        endpointStore->visibility(e.id, active);
    } catch (...) {
        session.commit(before);
        throw;
    }
    history.clear();
    mutedGains.clear();
    reloadDevices();
}

void Workbench::timerCallback() {
    if (!isShowing()) {
        return;
    }
    ++refreshTicks;

    auto ins = session.inputs, outs = session.outputs;
    auto wasOnline = session.online;
    auto revision = session.routes.revision;
    session.refresh();
    if (!wasOnline && session.online) {
        guard([&] { readDeviceSettings(); });
    }
    if (ins != session.inputs || outs != session.outputs) {
        buildPorts();
    }
    if (revision != session.routes.revision) {
        mutedGains.clear();
    }
    if (refreshTicks % 20 == 0 && endpointStore && endpointStore->changed()) {
        guard([&] { reloadDevices(); });
    }

    for (int i = 0; i < r::Ports; i++) {
        sources[i].tick(session.online ? session.sourcePeaks[i] : 0);
        targets[i].tick(session.online ? session.targetPeaks[i] : 0);
    }
    int interval = session.online ? 40 : 200;
    if (getTimerInterval() != interval) {
        startTimer(interval);
    }
    if (page == 0 || refreshTicks % 10 == 0) {
        repaint();
    }
}

int Workbench::findHit(Point p) const {
    for (int i = (int)hits.size() - 1; i >= 0; i--) {
        if (hits[i].box.contains(p.x, p.y)) {
            return i;
        }
    }
    return -1;
}

int Workbench::wireAt(Point p) const {
    if (page != 0 || view != 0 || !layout || !menu.empty() || p.y <= 140 || p.y >= layout->dock) {
        return -1;
    }
    int hitIndex = findHit(p);
    if (hitIndex >= 0 && hits[hitIndex].action != SelectPort) {
        return -1;
    }
    for (auto &n : layout->nodes) {
        if (std::hypot(p.x - n.pin.x, p.y - n.pin.y) <= 11) {
            return -1;
        }
    }
    return cableAt(cables, p);
}

void Workbench::updateDraggedSlider(Point p) {
    if (draggingGain) {
        InspectorLayout dock((float)getWidth(), (float)getHeight());
        draftGain = jlimit(-96.f, 6.f, (p.x - dock.gainTrack.l) / dock.gainTrack.width() * 102 - 96);
        repaint();
    }
    if (draggedVolumeChannel >= 0) {
        float top = 310, bottom = (float)getHeight() - 270;
        volumePercent = jlimit(0.f, 100.f, (bottom - p.y) / (bottom - top) * 100);
        repaint();
    }
}

void Workbench::notify(String text, bool error) {
    message(text, error);
}

Workbench::Workbench(std::filesystem::path root) : session(std::move(root)), hostDraft(session.host) {
    setOpaque(true);
    setLookAndFeel(&look);
    setWantsKeyboardFocus(true);
    setFocusContainerType(FocusContainerType::keyboardFocusContainer);
    setTitle("DonnerLive audio routing workbench");
    setSize(1440, 900);
    look.setColour(ResizableWindow::backgroundColourId, colour(bg));

    guard([&] { reloadDevices(); });
    guard([&] { readDeviceSettings(); });
    buildPorts();

    startTimer(200);
}

Workbench::~Workbench() {
    stopTimer();
    setLookAndFeel(nullptr);
}

void Workbench::resized() {
    menu.clear();
    if (editor) {
        editor.reset();
    }
    repaint();
}

void Workbench::mouseMove(const MouseEvent &e) {
    mouse = {e.position.x, e.position.y};
    int next = findHit(mouse);
    if (hoveredHit != next) {
        hoveredHit = next;
        setMouseCursor(next >= 0 && hits[next].enabled ? MouseCursor::PointingHandCursor
                                                       : MouseCursor::NormalCursor);
        repaint();
    }
}

void Workbench::mouseExit(const MouseEvent &) {
    hoveredHit = -1;
    repaint();
}

void Workbench::mouseDown(const MouseEvent &e) {
    grabKeyboardFocus();
    mouse = {e.position.x, e.position.y};
    if (!menu.empty()) {
        for (auto h : menuHits) {
            if (h.box.contains(e.position)) {
                if (h.action == DeviceToggle && h.b == 1) {
                    deviceIndex = h.a;
                }
                execute(h);
                return;
            }
        }
        menu.clear();
        repaint();
        return;
    }
    int i = findHit(mouse);
    int wire = wireAt(mouse);
    if (wire >= 0) {
        auto c = cables[wire];
        execute({{}, SelectWire, c.s, c.d});
        return;
    }
    if (i >= 0) {
        auto h = hits[i];
        if (!h.enabled) {
            return;
        }
        if (h.action == Gain) {
            draggingGain = true;
            draftGain = gain() > 0 ? 20 * std::log10(gain()) : -96;
            updateDraggedSlider(mouse);
        } else if (h.action == Volume) {
            draggedVolumeChannel = h.a;
            updateDraggedSlider(mouse);
        } else {
            execute(h);
        }
        return;
    }
}

void Workbench::mouseDrag(const MouseEvent &e) {
    mouse = {e.position.x, e.position.y};
    if (draggingGain || draggedVolumeChannel >= 0) {
        updateDraggedSlider(mouse);
    } else if (wireAnchor >= 0) {
        draggingWire = e.getDistanceFromDragStart() > 5;
        repaint();
    }
}

void Workbench::mouseUp(const MouseEvent &e) {
    if (draggingGain) {
        draggingGain = false;
        guard([&] { setGain(draftGain); });
    }
    if (draggedVolumeChannel >= 0) {
        int i = draggedVolumeChannel;
        draggedVolumeChannel = -1;
        guard([&] { writeVolume(i, (int)std::round(volumePercent)); });
    }
    if (draggingWire && layout) {
        for (auto &n : layout->nodes) {
            if (n.port.source != anchorSource && box(n.rect).expanded(5, 12).contains(e.position)) {
                guard([&] {
                    connectPorts(anchorSource ? wireAnchor : n.port.id,
                                 anchorSource ? n.port.id : wireAnchor);
                });
                break;
            }
        }
        draggingWire = false;
    }
    repaint();
}

void Workbench::mouseDoubleClick(const MouseEvent &e) {
    if (!e.mods.isLeftButtonDown()) {
        return;
    }
    Point p{e.position.x, e.position.y};
    int wire = wireAt(p);
    if (wire >= 0) {
        auto c = cables[wire];
        execute({{}, SelectWire, c.s, c.d});
        execute({{}, Disconnect});
        return;
    }
    if (!menu.empty() || page != 0 || view == 0) {
        return;
    }
    int i = findHit(p);
    if (i >= 0 && hits[i].action == SelectWire) {
        execute({box(InspectorLayout((float)getWidth(), (float)getHeight()).gainEditor), GainEdit});
    }
}

void Workbench::mouseWheelMove(const MouseEvent &, const MouseWheelDetails &wheel) {
    int step = wheel.deltaY < 0 || wheel.deltaX < 0 ? 1 : -1;
    if (page == 0 && view == 0) {
        scroll = jmax(0, scroll + step);
    } else {
        tableScroll = jmax(0, tableScroll + step * 2);
    }
    repaint();
}

bool Workbench::keyPressed(const KeyPress &key) {
    if (key == KeyPress::escapeKey) {
        wireAnchor = -1;
        draggingWire = false;
        menu.clear();
        editor.reset();
        repaint();
        return true;
    }
    if (!menu.empty()) {
        if (key == KeyPress::downKey || key == KeyPress::upKey) {
            focusedHit = jlimit(0, (int)menu.size() - 1, focusedHit + (key == KeyPress::downKey ? 1 : -1));
            mouse = {menuBounds.getX() + 10, menuBounds.getY() + 6 + focusedHit * 36 + 17};
            repaint();
            return true;
        }
        if (key == KeyPress::returnKey && focusedHit >= 0 && focusedHit < (int)menu.size()) {
            auto h = menu[focusedHit].second;
            if (h.action == DeviceToggle && h.b == 1) {
                deviceIndex = h.a;
            }
            execute(h);
            focusedHit = -1;
            return true;
        }
    }
    // Some Windows input methods send the Ctrl+Z control character without a scan code.
    const int code = key.getKeyCode();
    if ((key.getModifiers().isCtrlDown() && (code == 'Z' || code == 'z')) || code == 26) {
        guard([&] { undo(); });
        return true;
    }
    if (key == KeyPress::deleteKey && page == 0 && selectedSource >= 0) {
        execute({{}, Disconnect});
        return true;
    }
    if (key.getKeyCode() == KeyPress::tabKey) {
        int n = (int)hits.size();
        if (!n) {
            return true;
        }
        int step = key.getModifiers().isShiftDown() ? -1 : 1;
        for (int tries = 0; tries < n; tries++) {
            focusedHit = (focusedHit + step + n) % n;
            if (hits[focusedHit].enabled) {
                break;
            }
        }
        repaint();
        return true;
    }
    if ((key == KeyPress::returnKey || key == KeyPress::spaceKey) && focusedHit >= 0 &&
        focusedHit < (int)hits.size()) {
        auto h = hits[focusedHit];
        if (h.action == Gain) {
            h.action = GainEdit;
        }
        execute(h);
        return true;
    }
    if (focusedHit >= 0 && focusedHit < (int)hits.size() &&
        (key == KeyPress::upKey || key == KeyPress::downKey)) {
        auto h = hits[focusedHit];
        int step = key == KeyPress::upKey ? 1 : -1;
        if (h.enabled && h.action == Volume) {
            auto &c = device.channels[h.a];
            guard([&] { writeVolume(h.a, jlimit(0, 100, ui::volumePercent(*c.volume) + step)); });
            repaint();
            return true;
        }
    }
    if (page == 0 && (key == KeyPress::leftKey || key == KeyPress::rightKey)) {
        scroll = jmax(0, scroll + (key == KeyPress::rightKey ? 1 : -1));
        repaint();
        return true;
    }
    return false;
}
} // namespace studio
