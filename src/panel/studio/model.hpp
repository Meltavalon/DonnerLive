// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
#include <cstdint>

namespace studio {
struct Point {
    float x = 0, y = 0;
};

struct Rect {
    float l = 0, t = 0, r = 0, b = 0;

    bool contains(Point p) const {
        return p.x >= l && p.x <= r && p.y >= t && p.y <= b;
    }

    float width() const {
        return r - l;
    }
};

struct Port {
    int id = 0, row = 0;
    bool source = false;
    std::wstring name, group;
    unsigned tint = 0;
};

inline std::vector<Port> hostUsagePorts(const std::vector<Port> &ports, uint32_t inputs, uint32_t outputs) {
    std::vector<Port> visible;
    visible.reserve(ports.size());
    for (auto &port : ports) {
        int channel = port.row == 1 ? port.id : port.id - 26;
        if ((port.row != 1 && port.row != 2) ||
            (channel >= 0 && channel < 32 &&
             ((port.row == 1 ? inputs : outputs) & (uint32_t(1) << channel)))) {
            visible.push_back(port);
        }
    }
    return visible;
}

// Keep the complete registry for matrix editing; compact only the DAW cards in the patch view.
template <class Connected>
std::vector<Port> connectedHostPorts(const std::vector<Port> &ports, Connected connected) {
    std::vector<Port> visible;
    visible.reserve(ports.size());
    for (auto &port : ports) {
        bool show = port.row != 1 && port.row != 2;
        if (!show) {
            for (auto &other : ports) {
                if (port.source != other.source &&
                    connected(port.source ? port.id : other.id, port.source ? other.id : port.id)) {
                    show = true;
                    break;
                }
            }
        }
        if (show) {
            visible.push_back(port);
        }
    }
    return visible;
}

struct Node {
    Port port;
    Rect rect;
    Point pin;
};

struct Cable {
    int s = 0, d = 0;
    Point start, control1, control2, end;
    std::vector<Point> points;

    Point at(float t) const {
        float u = 1 - t;
        return {u * u * u * start.x + 3 * u * u * t * control1.x + 3 * u * t * t * control2.x +
                    t * t * t * end.x,
                u * u * u * start.y + 3 * u * u * t * control1.y + 3 * u * t * t * control2.y +
                    t * t * t * end.y};
    }
};

inline float distance(Point p, Point a, Point b) {
    float dx = b.x - a.x, dy = b.y - a.y, q = dx * dx + dy * dy;
    float t = q ? std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / q, 0.f, 1.f) : 0;
    return std::hypot(p.x - a.x - t * dx, p.y - a.y - t * dy);
}

inline void raiseSelectedCable(std::vector<Cable> &cables, int s, int d) {
    std::stable_partition(cables.begin(), cables.end(), [=](const Cable &c) { return c.s != s || c.d != d; });
}

inline int cableAt(const std::vector<Cable> &cables, Point point) {
    for (int i = (int)cables.size() - 1; i >= 0; i--) {
        for (size_t j = 1; j < cables[i].points.size(); j++) {
            if (distance(point, cables[i].points[j - 1], cables[i].points[j]) < 7) {
                return i;
            }
        }
    }
    return -1;
}

constexpr float DockHeight = 112, StatusHeight = 28;

struct InspectorLayout {
    float top, gainLeft, effectsLeft;
    Rect gainEditor, gainTrack, mute, disconnect;

    InspectorLayout(float w, float h)
        : top(h - DockHeight), gainLeft(std::clamp(w * .25f, 280.f, 360.f) + 48), effectsLeft(gainLeft + 388),
          gainEditor{gainLeft + 98, top + 5, gainLeft + 194, top + 37},
          gainTrack{gainLeft, top + 46, gainLeft + 180, top + 66},
          mute{gainLeft + 206, top + 28, gainLeft + 270, top + 60},
          disconnect{gainLeft + 278, top + 28, gainLeft + 342, top + 60} {}
};

struct Layout {
    float w, h, dock, rowY[4]{}, cardWidth = 144, cardHeight = 88, left = 166, right = 0;
    int offset = 0, visible = 0;
    std::vector<Node> nodes;

    Layout(float width, float height, const std::vector<Port> &ports, int scroll = 0)
        : w(width), h(height), dock(height - DockHeight), right(width - 26) {
        int largest = 1;
        int counts[4]{};
        for (auto &p : ports) {
            largest = std::max(largest, ++counts[p.row]);
        }
        float step = std::clamp((right - left) / largest, 144.f, 196.f);
        visible = std::max(1, (int)((right - left) / step));
        if (visible > 1) {
            visible -= visible % 2;
        }
        step = std::min(196.f, (right - left) / visible);
        cardWidth = step - 12;
        offset = std::clamp(scroll, 0, std::max(0, largest - visible));
        float first = 160, last = dock - cardHeight - 26;
        for (int i = 0; i < 4; i++) {
            rowY[i] = first + (last - first) * i / 3;
        }
        int indices[4]{};
        for (auto &p : ports) {
            int i = indices[p.row]++;
            if (i < offset || i >= offset + visible) {
                continue;
            }
            float x = left + (i - offset) * step;
            Rect r{x, rowY[p.row], x + cardWidth, rowY[p.row] + cardHeight};
            nodes.push_back({p, r, {x + cardWidth / 2, p.source ? r.b + 5 : r.t - 5}});
        }
    }

    const Node *find(bool source, int id) const {
        for (auto &n : nodes) {
            if (n.port.source == source && n.port.id == id) {
                return &n;
            }
        }
        return nullptr;
    }

    Cable cable(const Node &a, const Node &b, int variation = 0) const {
        Cable c;
        c.s = a.port.id;
        c.d = b.port.id;
        c.start = a.pin;
        c.end = b.pin;
        bool bypass = b.port.row != a.port.row + 1;
        float bow = bypass ? 32.f + (variation % 5) * 5.f : std::abs(a.pin.x - b.pin.x) < 1 ? 10.f : 0.f;
        if (bypass && (variation & 1)) {
            bow = -bow;
        }
        float pull = std::clamp(std::abs(b.pin.y - a.pin.y) * .38f, 24.f, 180.f);
        c.control1 = {std::clamp(a.pin.x + bow, left + 8, right - 8),
                      std::clamp(a.pin.y + pull, 140.f, dock - 12)};
        c.control2 = {std::clamp(b.pin.x + bow, left + 8, right - 8),
                      std::clamp(b.pin.y - pull, 140.f, dock - 12)};
        c.points.reserve(65);
        for (int i = 0; i <= 64; i++) {
            c.points.push_back(c.at(i / 64.f));
        }
        return c;
    }
};

struct Meter {
    float db = -96, hold = -96;
    int holdTicks = 0;
    bool clip = false;

    void tick(float peak) {
        float v = peak > 0 ? std::max(-96.f, 20 * std::log10(peak)) : -96;
        db = std::max(v, db - 2.4f);
        if (v >= hold) {
            hold = v;
            holdTicks = 24;
        } else if (holdTicks > 0) {
            --holdTicks;
        } else {
            hold = std::max(db, hold - 1.2f);
        }
        clip = clip || peak >= 1;
    }

    void reset() {
        hold = db;
        clip = false;
    }
};
} // namespace studio
