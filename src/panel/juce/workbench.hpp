// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include "../../engine/service.hpp"
#include "../core/session.hpp"
#include "../studio/model.hpp"
#include "../../engine/endpoints.hpp"
#include "../../engine/driver_settings.hpp"
#include <map>

#include "style.hpp"

namespace studio {
enum Action {
    Nav,
    View,
    Pair,
    Focus,
    Presets,
    Undo,
    Reset,
    Engine,
    SelectPort,
    SelectWire,
    Gain,
    GainEdit,
    Mute,
    Disconnect,
    Scroll,
    HostName,
    HostSave,
    DeviceSelect,
    DeviceToggle,
    DeviceRename,
    DeviceAdd,
    Refresh,
    Rate,
    Frames,
    Safety,
    Apply,
    Link,
    Volume,
    ChannelMute,
    MenuPreset,
    MenuRate,
    MenuFrames,
    MenuSafe
};

struct Hit {
    Box box;
    Action action;
    int a = 0, b = 0;
    bool enabled = true;
    String tip;
};

class Workbench final : public Component, private Timer {
  public:
    ui::Session session;

  private:
    std::vector<dl::Endpoint> devices;
    std::unique_ptr<dl::EndpointStore> endpointStore;
    dl::DonnerSettings::State device{};
    dl::HostConfig hostDraft;
    bool deviceAvailable = false;
    bool stereoLinked = false;
    bool focusSelectedRoute = false;
    bool outputVolumesLinked = true;
    int page = 0, view = 0;
    int scroll = 0, tableScroll = 0;
    int deviceIndex = -1;
    int selectedSource = -1, selectedDestination = -1;
    int selectedPort = 0, selectedRow = 0;
    int wireAnchor = -1;
    int hoveredHit = -1, focusedHit = -1;
    int refreshTicks = 0;
    int draggedVolumeChannel = -1;
    bool draggingGain = false, draggingWire = false, anchorSource = true;
    float draftGain = 0, volumePercent = 0;
    unsigned draftRate = 48000, draftFrames = 64;
    bool draftSafe = false;
    Point mouse;
    std::vector<Port> ports;
    std::vector<Hit> hits, menuHits;
    std::vector<std::pair<String, Hit>> menu;
    Box menuBounds;
    std::vector<r::Routes> history;
    std::map<int, float> mutedGains;
    Meter sources[r::Ports], targets[r::Ports];
    std::unique_ptr<TextEditor> editor;
    std::function<void(String)> editCommit;
    String notice;
    uint64 noticeUntil = 0;
    LookAndFeel_V4 look;
    std::unique_ptr<Layout> layout;
    std::vector<Cable> cables;
    std::vector<std::pair<int, int>> listRows;
    std::map<std::pair<int, bool>, Font> fonts;

    static Box box(Rect r) {
        return {r.l, r.t, r.r - r.l, r.b - r.t};
    }

    void text(Graphics &g,
              String s,
              Box b,
              float size = 13,
              unsigned c = ink,
              bool bold = false,
              int align = Justification::centredLeft);

    void line(Graphics &g, float x, float y, float xx, float yy, unsigned c = border, float width = 1);

    void fill(Graphics &g, Box b, unsigned c, float radius = 0);

    void outline(Graphics &g, Box b, unsigned c, float radius = 6, float width = 1);

    // All icons use the same authored 1.5-DIP stroke grammar.
    void icon(Graphics &g, int kind, Box b, unsigned c = muted);

    void hit(Box b, Action a, int x = 0, int y = 0, bool enabled = true, String tip = {});

    void button(Graphics &g,
                String label,
                Box b,
                Action a,
                int x = 0,
                int y = 0,
                bool active = false,
                bool enabled = true,
                int glyph = -1);

    void message(String s, bool error = false);

    template <class F> void guard(F f) {
        try {
            f();
        } catch (const std::exception &e) {
            message(String::fromUTF8(e.what()), true);
        }
    }

    float gain() const;

    String portLabel(bool source, int id) const;

    void buildPorts();

    std::vector<Port> visiblePatchPorts() const;

    void reloadDevices();

    void commitWithUndo(r::Routes next);

    void undo();

    void connectPorts(int s, int d);

    void setGain(float db);

    void readDeviceSettings();

    bool audioBusy() const;

    void applyAudioSettings();

    void writeVolume(int i, int percent);

    void edit(String initial, Box bounds, std::function<void(String)> save);

    void openMenu(std::vector<std::pair<String, Hit>> items, Box anchorBox);

    void drawMenu(Graphics &g);

    void header(Graphics &g);

    void toolbar(Graphics &g);

    void meter(Graphics &g, const Node &n);

    void drawCable(Graphics &g, const Cable &c, bool active);

    void patch(Graphics &g);

    void table(Graphics &g);

    void inspector(Graphics &g);

    void hostPage(Graphics &g);

    void devicesPage(Graphics &g);

    void audioPage(Graphics &g);

    void effectsPage(Graphics &g);

    void footer(Graphics &g);

    void execute(Hit h);

    void toggleDevice();

    void timerCallback() override;

    int findHit(Point p) const;

    int wireAt(Point p) const;

    void updateDraggedSlider(Point p);

  public:
    std::function<void()> toggleEngine;
    std::function<dl::EngineService::State()> engineState;

    void notify(String text, bool error = false);

    explicit Workbench(std::filesystem::path root);

    ~Workbench() override;

    void paint(Graphics &g) override;

    void resized() override;

    void mouseMove(const MouseEvent &e) override;

    void mouseExit(const MouseEvent &) override;

    void mouseDown(const MouseEvent &e) override;

    void mouseDrag(const MouseEvent &e) override;

    void mouseUp(const MouseEvent &e) override;

    void mouseDoubleClick(const MouseEvent &e) override;

    void mouseWheelMove(const MouseEvent &, const MouseWheelDetails &wheel) override;

    bool keyPressed(const KeyPress &key) override;
};
} // namespace studio
