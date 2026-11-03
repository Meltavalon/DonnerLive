// SPDX-License-Identifier: AGPL-3.0-only
#include "workbench.hpp"
#include "../core/volume.hpp"

namespace studio {
void Workbench::text(Graphics &g, String s, Box b, float size, unsigned c, bool bold, int align) {
    auto key = std::make_pair((int)size, bold);
    auto it = fonts.find(key);
    if (it == fonts.end()) {
        it =
            fonts.emplace(key, Font(FontOptions("Microsoft YaHei UI", size, bold ? Font::bold : Font::plain)))
                .first;
    }
    g.setColour(colour(c));
    g.setFont(it->second);
    g.drawText(s, b, align, true);
}

void Workbench::line(Graphics &g, float x, float y, float xx, float yy, unsigned c, float width) {
    g.setColour(colour(c));
    g.drawLine(x, y, xx, yy, width);
}

void Workbench::fill(Graphics &g, Box b, unsigned c, float radius) {
    g.setColour(colour(c));
    if (radius) {
        g.fillRoundedRectangle(b, radius);
    } else {
        g.fillRect(b);
    }
}

void Workbench::outline(Graphics &g, Box b, unsigned c, float radius, float width) {
    g.setColour(colour(c));
    g.drawRoundedRectangle(b.reduced(.5f), radius, width);
}

void Workbench::icon(Graphics &g, int kind, Box b, unsigned c) {
    auto p = b.getCentre();
    float x = p.x, y = p.y;
    Path path;
    if (kind == 0) {
        path.startNewSubPath(x - 7, y - 6);
        path.lineTo(x - 7, y + 6);
        path.startNewSubPath(x, y - 9);
        path.lineTo(x, y + 9);
        path.startNewSubPath(x + 7, y - 4);
        path.lineTo(x + 7, y + 4);
    }
    if (kind == 1) {
        path.addRectangle(x - 7, y - 7, 14, 14);
        path.startNewSubPath(x - 3, y - 7);
        path.lineTo(x - 3, y + 7);
        path.startNewSubPath(x + 3, y - 7);
        path.lineTo(x + 3, y + 7);
    }
    if (kind == 2) {
        path.startNewSubPath(x - 6, y);
        path.lineTo(x + 6, y);
        path.startNewSubPath(x, y - 6);
        path.lineTo(x, y + 6);
    }
    if (kind == 3) {
        path.startNewSubPath(x - 4, y - 4);
        path.lineTo(x + 1, y);
        path.lineTo(x - 4, y + 4);
    }
    if (kind == 4) {
        path.startNewSubPath(x + 4, y - 4);
        path.lineTo(x - 1, y);
        path.lineTo(x + 4, y + 4);
    }
    if (kind == 5) {
        path.startNewSubPath(x - 6, y - 3);
        path.lineTo(x, y + 3);
        path.lineTo(x + 6, y - 3);
    }
    if (kind == 6) {
        path.startNewSubPath(x, y - 7);
        path.lineTo(x, y + 1);
        path.addCentredArc(x, y, 7, 7, 0, .65f, MathConstants<float>::twoPi - .65f, true);
    }
    g.setColour(colour(c));
    g.strokePath(path, PathStrokeType(1.5f, PathStrokeType::curved, PathStrokeType::rounded));
}

void Workbench::button(
    Graphics &g, String label, Box b, Action a, int x, int y, bool active, bool enabled, int glyph) {
    int index = (int)hits.size();
    bool hot = index == hoveredHit && enabled;
    fill(g, b, !enabled ? 0x20242a : active ? 0x304339 : hot ? 0x353d46 : 0x282e35, 6);
    if (active) {
        outline(g, b, 0x527f69);
    }
    if (focusedHit == index) {
        outline(g, b, mint, 6, 1.5f);
    }
    if (glyph >= 0) {
        icon(g,
             glyph,
             {b.getX() + 9, b.getY(), 20, b.getHeight()},
             enabled ? (active ? mint : muted) : 0x78818d);
    }
    text(g,
         label,
         b.reduced(10, 0).withTrimmedLeft(glyph >= 0 ? 22.f : 0.f),
         12,
         enabled ? (active ? mint : ink) : 0x78818d,
         false,
         glyph >= 0 ? Justification::centredLeft : Justification::centred);
    hit(b, a, x, y, enabled, label);
}

void Workbench::drawMenu(Graphics &g) {
    if (menu.empty()) {
        return;
    }
    fill(g, menuBounds, 0x303740, 8);
    outline(g, menuBounds, 0x5b6775, 8);
    menuHits.clear();
    for (size_t i = 0; i < menu.size(); i++) {
        auto b = Box(menuBounds.getX() + 6, menuBounds.getY() + 6 + i * 36, menuBounds.getWidth() - 12, 34);
        if (b.contains(mouse.x, mouse.y)) {
            fill(g, b, 0x45534e, 4);
        }
        text(g, menu[i].first, b.reduced(10, 0), 13);
        auto hit = menu[i].second;
        hit.box = b;
        menuHits.push_back(hit);
    }
}

void Workbench::header(Graphics &g) {
    fill(g, {0, 0, (float)getWidth(), 64}, surface);
    line(g, 0, 63, (float)getWidth(), 63);
    icon(g, 0, {24, 20, 24, 24}, mint);
    text(g, "DonnerLive", {62, 14, 162, 34}, 22, ink, true);
    String tabs[] = {String::fromUTF8("路由工作台"),
                     String::fromUTF8("宿主通道"),
                     String::fromUTF8("虚拟设备"),
                     String::fromUTF8("声卡控制"),
                     String::fromUTF8("效果器")};
    float start = 252;
    for (int i = 0; i < 5; i++) {
        Box b{start + i * 108, 15, 100, 34};
        if (page == i) {
            fill(g, b, 0x303740, 6);
            fill(g, {b.getCentreX() - 12, 59, 24, 2}, mint);
        }
        text(g, tabs[i], b, 13, page == i ? ink : muted, page == i, Justification::centred);
        hit(b, Nav, i);
    }
    float right = (float)getWidth() - 24;
    auto state = engineState ? engineState() : dl::EngineService::State::stopped;
    bool changing =
        state == dl::EngineService::State::starting || state == dl::EngineService::State::stopping;
    String engineLabel = state == dl::EngineService::State::starting   ? String::fromUTF8("正在启动…")
                         : state == dl::EngineService::State::stopping ? String::fromUTF8("正在停止…")
                         : session.online                              ? String::fromUTF8("停止引擎")
                                                                       : String::fromUTF8("启动引擎");
    button(g, engineLabel, {right - 118, 15, 118, 34}, Engine, 0, 0, session.online, !changing, 6);
    text(g,
         session.online ? String::fromUTF8("引擎运行中") : String::fromUTF8("离线编辑"),
         {right - 238, 19, 108, 26},
         12,
         session.online ? mint : muted,
         false,
         Justification::centredRight);
}

void Workbench::toolbar(Graphics &g) {
    text(g, String::fromUTF8("信号路由"), {24, 81, 160, 34}, 23, ink, true);
    const wchar_t *names[] = {L"跳线", L"矩阵", L"清单"};
    for (int i = 0; i < 3; i++) {
        button(g, str(names[i]), {252.f + i * 66, 84, 60, 30}, View, i, 0, view == i);
    }
    float x = (float)getWidth() - 550;
    button(g, String::fromUTF8("立体声联动"), {x, 84, 106, 30}, Pair, 0, 0, stereoLinked);
    button(g, String::fromUTF8("聚焦所选"), {x + 114, 84, 94, 30}, Focus, 0, 0, focusSelectedRoute);
    button(g, String::fromUTF8("预设"), {x + 216, 84, 70, 30}, Presets);
    button(g, String::fromUTF8("撤销"), {x + 294, 84, 70, 30}, Undo, 0, 0, false, !history.empty());
    button(g, String::fromUTF8("复位峰值"), {x + 372, 84, 94, 30}, Reset);
}

void Workbench::meter(Graphics &g, const Node &n) {
    auto &m = n.port.source ? sources[n.port.id] : targets[n.port.id];
    auto b = box(n.rect);
    float x = b.getX() + 14, y = b.getBottom() - 16, w = b.getWidth() - 28;
    text(g, "dBFS", {x, b.getBottom() - 40, 44, 20}, 11, muted);
    text(g,
         m.db <= -95 ? String(CharPointer_UTF8("−∞")) : number(m.db, 0),
         {b.getRight() - 74, b.getBottom() - 43, 60, 24},
         14,
         m.clip ? 0xf09a99 : ink,
         true,
         Justification::centredRight);
    fill(g, {x, y, w, 8}, 0x11161b, 3);
    float fraction = jlimit(0.f, 1.f, (m.db + 72) / 72);
    if (fraction > 0) {
        ColourGradient gradient(colour(0x55ae88), x, y, colour(0xf09a99), x + w, y, false);
        gradient.addColour(.72, colour(mint));
        gradient.addColour(.88, colour(amber));
        g.setGradientFill(gradient);
        g.fillRoundedRectangle({x, y, w * fraction, 8}, 3);
    }
    for (float db : {-48.f, -24.f, -12.f, -6.f}) {
        float tx = x + w * (db + 72) / 72;
        line(g, tx, y + 10, tx, y + 12, 0x56616b);
    }
    if (m.hold > -72) {
        float hx = x + w * jlimit(0.f, 1.f, (m.hold + 72) / 72);
        line(g, hx, y - 2, hx, y + 10, m.clip ? 0xf09a99 : 0xe6f4ec, 1.6f);
    }
}

void Workbench::drawCable(Graphics &g, const Cable &c, bool active) {
    Path p;
    p.startNewSubPath(c.start.x, c.start.y);
    p.cubicTo(c.control1.x, c.control1.y, c.control2.x, c.control2.y, c.end.x, c.end.y);
    unsigned tint = c.s >= r::HostSource ? blue : mint;
    bool dim = (selectedSource >= 0 || wireAnchor >= 0) && !active;
    g.setColour(colour(0x0b1015).withAlpha(active ? .95f : .72f));
    g.strokePath(p, PathStrokeType(active ? 7.f : 4.5f, PathStrokeType::curved, PathStrokeType::rounded));
    g.setColour(colour(tint).withAlpha(active ? 1.f : dim ? .30f : .72f));
    g.strokePath(p, PathStrokeType(active ? 3.1f : 2.f, PathStrokeType::curved, PathStrokeType::rounded));
    if (active) {
        g.setColour(colour(0xeaf8f0).withAlpha(.45f));
        g.strokePath(p, PathStrokeType(.8f));
    }
}

void Workbench::patch(Graphics &g) {
    auto visiblePorts = visiblePatchPorts();
    layout = std::make_unique<Layout>((float)getWidth(), (float)getHeight(), visiblePorts, scroll);
    scroll = layout->offset;
    cables.clear();
    auto &l = *layout;
    int count[4]{};
    for (auto &p : visiblePorts) {
        count[p.row]++;
    }
    // One central enclosure makes the DAW round trip visually explicit.
    Box daw{16, l.rowY[1] - 28, l.w - 32, l.rowY[2] - l.rowY[1] + l.cardHeight + 38};
    fill(g, daw, 0x1b232e, 10);
    outline(g, daw, 0x384b63, 10);
    text(g, String::fromUTF8("DAW / ASIO 宿主"), {30, daw.getY() + 5, 210, 20}, 12, blue, true);
    String hostSummary = (session.online ? (session.hostUsageMode ? String::fromUTF8("宿主已接入")
                                                                  : String::fromUTF8("等待宿主"))
                                         : String::fromUTF8("离线跳线")) +
                         String::fromUTF8(" · 输入 ") + String(count[1]) + " / 32" +
                         String::fromUTF8(" · 返回 ") + String(count[2]) + " / 32";
    text(g,
         hostSummary,
         {l.w - 520, daw.getY() + 5, 485, 20},
         11,
         session.hostActive ? mint : muted,
         false,
         Justification::centredRight);
    const wchar_t *titles[] = {L"硬件 / 应用", L"送入 DAW", L"DAW 返回", L"监听 / 录音"};
    const wchar_t *subs[] = {L"音频来源", L"ASIO INPUT", L"ASIO OUTPUT", L"声音去向"};
    for (int row = 0; row < 4; row++) {
        text(g,
             str(titles[row]),
             {30, l.rowY[row] + 14, 128, 26},
             16,
             row == 1 || row == 2 ? blue : ink,
             true);
        text(g, str(subs[row]), {30, l.rowY[row] + 45, 128, 20}, 11, muted);
    }
    for (int row : {1, 2}) {
        if (count[row] == 0) {
            text(g,
                 session.online ? (String::fromUTF8("等待宿主连接通道"))
                                : (row == 1 ? String::fromUTF8("暂无已连接的宿主输入")
                                            : String::fromUTF8("暂无已连接的宿主返回")),
                 {l.left + 14, l.rowY[row] + 12, 330, 25},
                 14,
                 muted);
            text(g,
                 session.online ? String::fromUTF8("在 DAW 中启用输入或输出后，通道会自动出现")
                                : String::fromUTF8("在矩阵中选择端口并设置发送增益"),
                 {l.left + 14, l.rowY[row] + 43, 400, 23},
                 12,
                 muted);
            if (!session.online) {
                button(
                    g, String::fromUTF8("在矩阵中接线"), {l.left + 430, l.rowY[row] + 28, 136, 32}, View, 1);
            }
        }
    }
    float mid = (l.rowY[1] + l.cardHeight + l.rowY[2]) / 2;
    line(g, 181, mid, l.w - 75, mid, 0x354458);
    fill(g, {l.w / 2 - 144, mid - 12, 288, 24}, 0x1b232e);
    text(g,
         String::fromUTF8("在 DAW 中处理音频，再从下方返回"),
         {l.w / 2 - 142, mid - 12, 284, 24},
         11,
         muted,
         false,
         Justification::centred);
    for (int s = 0; s < r::Ports; s++) {
        for (int d = 0; d < r::Ports; d++) {
            if (session.routes.gain[s][d] > 0) {
                auto *a = l.find(true, s);
                auto *b = l.find(false, d);
                if (!a || !b) {
                    continue;
                }
                bool active = s == selectedSource && d == selectedDestination;
                if (focusSelectedRoute && selectedSource >= 0 && !active) {
                    continue;
                }
                cables.push_back(l.cable(*a, *b, s + d));
            }
        }
    }
    for (auto &n : l.nodes) {
        auto b = box(n.rect);
        bool selected = n.port.source
                            ? (n.port.id == selectedSource || wireAnchor == n.port.id && anchorSource)
                            : (n.port.id == selectedDestination || wireAnchor == n.port.id && !anchorSource);
        bool hot = b.expanded(5).contains(mouse.x, mouse.y);
        fill(g, b, selected ? 0x303d43 : hot ? 0x303740 : card, 8);
        if (selected) {
            outline(g, b, n.port.tint, 8, 1.3f);
        }
        text(g, str(n.port.group), {b.getX() + 14, b.getY() + 5, b.getWidth() - 28, 19}, 11, n.port.tint);
        text(g, str(n.port.name), {b.getX() + 14, b.getY() + 24, b.getWidth() - 28, 26}, 15, ink, true);
        meter(g, n);
        hit(b.expanded(3, 9),
            SelectPort,
            n.port.id,
            n.port.source ? 1 : 0,
            true,
            str(n.port.group) + " · " + str(n.port.name));
    }
    // Wires are physical overlays. The same ordered list drives rendering and hit testing.
    raiseSelectedCable(cables, selectedSource, selectedDestination);
    for (auto &c : cables) {
        drawCable(g, c, c.s == selectedSource && c.d == selectedDestination);
    }
    for (auto &n : l.nodes) {
        g.setColour(colour(bg));
        g.fillEllipse(n.pin.x - 5, n.pin.y - 5, 10, 10);
        g.setColour(colour(n.port.tint));
        g.drawEllipse(n.pin.x - 4, n.pin.y - 4, 8, 8, 1.3f);
    }
    if (wireAnchor >= 0 && draggingWire) {
        if (auto *a = l.find(anchorSource, wireAnchor)) {
            Path path;
            path.startNewSubPath(a->pin.x, a->pin.y);
            path.cubicTo(a->pin.x, mouse.y, mouse.x, a->pin.y, mouse.x, mouse.y);
            g.setColour(colour(mint));
            g.strokePath(path, PathStrokeType(2));
        }
    }
    int largest = *std::max_element(count, count + 4);
    if (largest > l.visible) {
        float y = l.dock - 22;
        text(g,
             String(scroll + 1) + " – " + String(jmin(largest, scroll + l.visible)) + " / " + String(largest),
             {l.w - 248, y - 1, 116, 20},
             10,
             muted,
             false,
             Justification::centredRight);
        button(g, "", {l.w - 118, y - 4, 32, 24}, Scroll, -l.visible, 0, false, scroll > 0, 4);
        button(
            g, "", {l.w - 78, y - 4, 32, 24}, Scroll, l.visible, 0, false, scroll + l.visible < largest, 3);
    }
}

void Workbench::table(Graphics &g) {
    layout = std::make_unique<Layout>((float)getWidth(), (float)getHeight(), ports, scroll);
    auto &l = *layout;
    float bottom = l.dock - 20;
    listRows.clear();
    if (view == 2) {
        text(g, String::fromUTF8("来源"), {40, 140, 340, 24}, 12, muted);
        text(g, String::fromUTF8("去向"), {430, 140, 340, 24}, 12, muted);
        text(g, String::fromUTF8("发送增益"), {l.w - 210, 140, 160, 24}, 12, muted);
        for (int s = 0; s < r::Ports; s++) {
            for (int d = 0; d < r::Ports; d++) {
                if (session.routes.gain[s][d] > 0) {
                    listRows.push_back({s, d});
                }
            }
        }
        int visible = (int)((bottom - 180) / 38);
        tableScroll = jlimit(0, jmax(0, (int)listRows.size() - visible), tableScroll);
        for (int i = tableScroll; i < (int)listRows.size() && i < tableScroll + visible; i++) {
            auto [s, d] = listRows[i];
            float y = 178.f + (i - tableScroll) * 38.f;
            bool active = s == selectedSource && d == selectedDestination;
            Box b{24, y, l.w - 48, 36};
            fill(g, b, active ? 0x2e4042 : i % 2 ? surface : bg, 4);
            text(g, portLabel(true, s), {40, y, 340, 36});
            icon(g, 3, {390, y, 24, 36});
            text(g, portLabel(false, d), {430, y, 340, 36});
            text(g,
                 number(20 * std::log10(session.routes.gain[s][d])) + " dB",
                 {l.w - 210, y, 160, 36},
                 13,
                 active ? mint : ink);
            hit(b, SelectWire, s, d);
        }
        if (listRows.empty()) {
            text(g,
                 String::fromUTF8("尚无连接。切换到跳线视图，依次点击来源和去向建立连接。"),
                 {40, 200, l.w - 80, 60},
                 15,
                 muted);
        }
    } else {
        std::vector<Port> ss, dd;
        for (auto &p : ports) {
            (p.source ? ss : dd).push_back(p);
        }
        int cols = jmax(1, (int)((l.w - 220) / 108)), rows = jmax(1, (int)((bottom - 211) / 29));
        tableScroll = jlimit(0, jmax(0, (int)ss.size() - rows), tableScroll);
        scroll = jlimit(0, jmax(0, (int)dd.size() - cols), scroll);
        for (int j = scroll; j < (int)dd.size() && j < scroll + cols; j++) {
            float x = 210.f + (j - scroll) * 108.f;
            fill(g, {x, 143, 106, 54}, 0x253040, 4);
            text(g, str(dd[j].name), {x + 6, 146, 94, 48}, 11, blue, false, Justification::centred);
        }
        for (int i = tableScroll; i < (int)ss.size() && i < tableScroll + rows; i++) {
            float y = 203.f + (i - tableScroll) * 29.f;
            text(g, str(ss[i].name), {30, y, 174, 27}, 12, ss[i].tint);
            for (int j = scroll; j < (int)dd.size() && j < scroll + cols; j++) {
                float x = 210.f + (j - scroll) * 108.f;
                float gain = session.routes.gain[ss[i].id][dd[j].id];
                Box b{x, y, 106, 27};
                fill(g, b, gain > 0 ? 0x304b43 : surface, 3);
                if (ss[i].id == selectedSource && dd[j].id == selectedDestination) {
                    outline(g, b, mint, 3);
                }
                text(g,
                     gain > 0 ? number(20 * std::log10(gain)) : String(CharPointer_UTF8("·")),
                     b,
                     12,
                     gain > 0 ? mint : 0x79838f,
                     false,
                     Justification::centred);
                hit(b, SelectWire, ss[i].id, dd[j].id);
            }
        }
        button(g,
               String::fromUTF8("前一组"),
               {l.w - 210, bottom - 28, 84, 26},
               Scroll,
               -cols,
               0,
               false,
               scroll > 0);
        button(g,
               String::fromUTF8("后一组"),
               {l.w - 118, bottom - 28, 84, 26},
               Scroll,
               cols,
               0,
               false,
               scroll + cols < (int)dd.size());
    }
}

void Workbench::inspector(Graphics &g) {
    float w = (float)getWidth();
    InspectorLayout dock(w, (float)getHeight());
    float y = dock.top, x = dock.gainLeft;
    bool selected = selectedSource >= 0 && selectedDestination >= 0;
    fill(g, {0, y, w, DockHeight - StatusHeight}, surface);
    line(g, 0, y, w, y, border);
    text(g, String::fromUTF8("发送详情"), {24, y + 9, x - 64, 20}, 12, muted);
    String route = selected ? portLabel(true, selectedSource) + String::fromUTF8(" → ") +
                                  portLabel(false, selectedDestination)
                            : String::fromUTF8("选择连接，或点击插孔接线");
    text(g, route, {24, y + 35, x - 64, 28}, 14, ink, true);
    line(g, x - 24, y + 16, x - 24, y + 68);
    text(g, String::fromUTF8("发送增益"), {x, y + 9, 96, 24}, 12, muted);
    float db = draggingGain ? draftGain : gain() > 0 ? 20 * std::log10(gain()) : -96;
    text(g,
         selected ? (gain() > 0 || draggingGain ? number(db) + " dB" : String::fromUTF8("已断开"))
                  : String(CharPointer_UTF8("—")),
         box(dock.gainEditor),
         18,
         selected ? mint : muted,
         true,
         Justification::centredRight);
    hit(box(dock.gainEditor), GainEdit, 0, 0, selected);
    auto updateDraggedSlider = box(dock.gainTrack);
    float centre = updateDraggedSlider.getCentreY();
    line(g, updateDraggedSlider.getX(), centre, updateDraggedSlider.getRight(), centre, border, 4);
    float gx =
        updateDraggedSlider.getX() + jlimit(0.f, 1.f, (db + 96) / 102) * updateDraggedSlider.getWidth();
    line(g, updateDraggedSlider.getX(), centre, gx, centre, mint, 4);
    fill(g, {gx - 4, centre - 7, 8, 14}, selected ? mint : muted, 3);
    hit(updateDraggedSlider.expanded(6), Gain, 0, 0, selected);
    button(g,
           String::fromUTF8("静音"),
           box(dock.mute),
           Mute,
           0,
           0,
           selected && mutedGains.count(selectedSource * r::Ports + selectedDestination),
           selected && (gain() > 0 || mutedGains.count(selectedSource * r::Ports + selectedDestination)));
    button(
        g, String::fromUTF8("断开"), box(dock.disconnect), Disconnect, 0, 0, false, selected && gain() > 0);
    float fx = dock.effectsLeft;
    line(g, fx - 24, y + 16, fx - 24, y + 68);
    text(g, String::fromUTF8("通道效果链"), {fx, y + 9, w - fx - 220, 20}, 12, muted);
    text(g,
         selected || wireAnchor >= 0 ? portLabel(selectedRow == 0 || selectedRow == 2, selectedPort)
                                     : String::fromUTF8("选择通道"),
         {fx, y + 35, w - fx - 220, 28},
         14,
         ink,
         true);
    Box slot{w - 194, y + 20, 170, 44};
    outline(g, slot, 0x48515b, 6);
    icon(g, 2, {slot.getX() + 10, y + 29, 24, 24}, 0x85919f);
    text(g, "VST3", {slot.getX() + 44, y + 23, 110, 20}, 13, muted, true);
    text(g, String::fromUTF8("尚未接入"), {slot.getX() + 44, y + 42, 110, 17}, 11, muted);
}

void Workbench::hostPage(Graphics &g) {
    float w = (float)getWidth(), bottom = (float)getHeight() - 80;
    text(g, String::fromUTF8("宿主通道"), {32, 88, 300, 34}, 23, ink, true);
    text(g,
         String::fromUTF8("固定提供 32 入 / 32 "
                          "出，跳线视图按宿主使用情况显示。修改名称需停止引擎，并重开宿主音频设备。"),
         {32, 130, w - 64, 26},
         13,
         muted);
    text(g, String::fromUTF8("送入 DAW"), {32, 180, 140, 30}, 14, blue, true);
    text(g, String::fromUTF8("32 路 · 固定"), {182, 180, 220, 32}, 18, ink, true);
    text(g, String::fromUTF8("DAW 返回"), {w / 2, 180, 140, 30}, 14, blue, true);
    text(g, String::fromUTF8("32 路 · 固定"), {w / 2 + 150, 180, 220, 32}, 18, ink, true);
    text(g, String::fromUTF8("端口"), {40, 238, 100, 24}, 11, muted);
    text(g, String::fromUTF8("宿主输入名称 · 点击编辑"), {160, 238, w / 2 - 184, 24}, 12, muted);
    text(g, String::fromUTF8("宿主返回名称 · 点击编辑"), {w / 2 + 60, 238, w / 2 - 92, 24}, 12, muted);
    int rows = jmax(1, (int)((bottom - 280) / 38)), count = jmax(hostDraft.inputs, hostDraft.outputs);
    tableScroll = jlimit(0, jmax(0, count - rows), tableScroll);
    for (int i = tableScroll; i < count && i < tableScroll + rows; i++) {
        float y = 277.f + (i - tableScroll) * 38.f;
        fill(g, {32, y, w - 64, 36}, i % 2 ? surface : bg, 3);
        text(g, String(i + 1).paddedLeft('0', 2), {40, y, 100, 36}, 12, muted);
        Box a{150, y, w / 2 - 174, 36}, b{w / 2 + 50, y, w / 2 - 82, 36};
        text(g,
             String::fromUTF8(hostDraft.inputNames[i].c_str()),
             a.reduced(10, 0),
             13,
             i < hostDraft.inputs ? ink : 0x77808b);
        text(g,
             String::fromUTF8(hostDraft.outputNames[i].c_str()),
             b.reduced(10, 0),
             13,
             i < hostDraft.outputs ? ink : 0x77808b);
        hit(a, HostName, i, 0, !audioBusy() && i < hostDraft.inputs);
        hit(b, HostName, i, 1, !audioBusy() && i < hostDraft.outputs);
    }
    button(g,
           String::fromUTF8("保存宿主设置"),
           {w - 186, (float)getHeight() - 68, 154, 34},
           HostSave,
           0,
           0,
           true,
           !audioBusy());
    text(g,
         String::fromUTF8("名称最多 31 个 UTF-8 字节；滚轮查看更多端口。"),
         {32, (float)getHeight() - 68, w - 240, 34},
         12,
         muted);
}

void Workbench::devicesPage(Graphics &g) {
    float w = (float)getWidth();
    text(g, String::fromUTF8("虚拟设备"), {32, 88, 300, 34}, 23, ink, true);
    text(g,
         String::fromUTF8("应用从播放设备进入路由，从录音设备接收混音。"),
         {32, 130, w - 64, 26},
         13,
         muted);
    button(g, String::fromUTF8("添加设备"), {w - 250, 90, 124, 34}, DeviceAdd, 0, 0, true, true, 2);
    button(g, String::fromUTF8("刷新"), {w - 114, 90, 82, 34}, Refresh);
    int playback = 0, capture = 0;
    for (auto &e : devices) {
        if (e.active) {
            (e.playback ? playback : capture)++;
        }
    }
    text(g,
         String::fromUTF8("播放 ") + String(playback) + " / 8     " + String::fromUTF8("录音 ") +
             String(capture) + " / 5",
         {32, 179, 400, 24},
         13,
         mint);
    text(g, String::fromUTF8("Windows 设备名称"), {48, 224, 320, 24}, 12, muted);
    text(g, String::fromUTF8("方向"), {w * .40f, 224, 250, 24}, 12, muted);
    text(g, String::fromUTF8("底层通道"), {w * .68f, 224, 130, 24}, 12, muted);
    text(g, String::fromUTF8("状态"), {w - 180, 224, 140, 24}, 12, muted);
    int visible = jmax(1, (getHeight() - 370) / 40);
    tableScroll = jlimit(0, jmax(0, (int)devices.size() - visible), tableScroll);
    for (int i = tableScroll; i < (int)devices.size() && i < tableScroll + visible; i++) {
        auto &e = devices[i];
        float y = 262.f + (i - tableScroll) * 40.f;
        Box b{32, y, w - 64, 38};
        fill(g, b, i == deviceIndex ? 0x2e4042 : i % 2 ? surface : bg, 4);
        text(g, str(e.name), {48, y, w * .36f - 48, 38}, 14, e.active ? ink : muted);
        text(g,
             e.playback ? String::fromUTF8("播放 · 应用 → 路由") : String::fromUTF8("录音 · 路由 → 应用"),
             {w * .40f, y, 270, 38},
             12,
             muted);
        text(g, String(e.channel + 1) + " / " + String(e.channel + 2), {w * .68f, y, 130, 38}, 13, muted);
        text(g,
             e.active ? String::fromUTF8("已启用") : String::fromUTF8("未启用"),
             {w - 180, y, 140, 38},
             12,
             e.active ? mint : muted);
        hit(b, DeviceSelect, i);
    }
    if (devices.empty()) {
        text(g,
             String::fromUTF8("没有找到支持的虚拟端点。确认 Wave Link 驱动已安装后刷新。"),
             {48, 280, w - 96, 60},
             14,
             muted);
    }
    float y = (float)getHeight() - 92;
    button(g, String::fromUTF8("重命名"), {32, y, 108, 34}, DeviceRename, 0, 0, false, deviceIndex >= 0);
    button(g,
           deviceIndex >= 0 && devices[deviceIndex].active ? String::fromUTF8("移除设备")
                                                           : String::fromUTF8("启用设备"),
           {152, y, 116, 34},
           DeviceToggle,
           0,
           0,
           false,
           deviceIndex >= 0);
    text(g, String::fromUTF8("移除将断开关联发送并隐藏端点，可再次启用。"), {292, y, w - 324, 34}, 12, muted);
}

void Workbench::audioPage(Graphics &g) {
    float w = (float)getWidth(), h = (float)getHeight();
    text(g, String::fromUTF8("声卡控制"), {32, 88, 300, 34}, 23, ink, true);
    text(g, "Donner Livejack USB Audio", {32, 130, 500, 24}, 13, muted);
    button(g, String::fromUTF8("重新读取"), {w - 156, 90, 124, 34}, Refresh);
    float right = 356;
    fill(g, {32, 184, 300, h - 324}, surface, 9);
    text(g, String::fromUTF8("音频参数"), {52, 204, 260, 28}, 17, ink, true);
    text(g, String::fromUTF8("采样率"), {52, 254, 260, 24}, 12, muted);
    button(g,
           deviceAvailable ? String(draftRate) + " Hz" : String::fromUTF8("未读取"),
           {52, 284, 260, 36},
           Rate,
           0,
           0,
           false,
           deviceAvailable && !audioBusy());
    text(g, String::fromUTF8("缓冲区"), {52, 340, 260, 24}, 12, muted);
    button(g,
           deviceAvailable ? String(draftFrames) + String::fromUTF8(" 采样帧") : String::fromUTF8("未读取"),
           {52, 370, 260, 36},
           Frames,
           0,
           0,
           false,
           deviceAvailable && !audioBusy());
    text(g, String::fromUTF8("ASIO 安全模式"), {52, 426, 260, 24}, 12, muted);
    button(g,
           draftSafe ? String::fromUTF8("已开启") : String::fromUTF8("已关闭"),
           {52, 456, 260, 36},
           Safety,
           0,
           0,
           draftSafe,
           deviceAvailable && !audioBusy());
    text(g, String::fromUTF8("增加缓冲余量，可能增加延迟。"), {52, 506, 260, 24}, 11, muted);
    button(g,
           String::fromUTF8("应用音频参数"),
           {52, 552, 260, 36},
           Apply,
           0,
           0,
           true,
           deviceAvailable && !audioBusy());
    text(g,
         session.online ? String::fromUTF8("停止引擎后可修改音频参数。")
                        : String::fromUTF8("提交后读取驱动确认。"),
         {52, 600, 260, 24},
         11,
         muted);
    fill(g, {right, 184, w - right - 32, h - 324}, surface, 9);
    text(g, String::fromUTF8("数字音量"), {right + 24, 204, 200, 28}, 17, ink, true);
    button(g, String::fromUTF8("输出 L/R 联动"), {w - 212, 202, 156, 32}, Link, 0, 0, outputVolumesLinked);
    float spacing = (w - right - 64) / 4, top = 310, bottom = h - 270;
    const wchar_t *names[] = {L"输入 1", L"输入 2", L"输出 L", L"输出 R"};
    for (int i = 0; i < 4; i++) {
        float x = right + 16 + (i + .5f) * spacing;
        auto &c = device.channels[i];
        bool available = deviceAvailable && c.canVolume && c.volume && c.step > 0;
        int pc = draggedVolumeChannel == i ? (int)volumePercent : c.volume ? ui::volumePercent(*c.volume) : 0;
        text(g,
             available ? String(pc) + " %" : String(CharPointer_UTF8("—")),
             {x - 58, 252, 116, 40},
             26,
             available ? ink : muted,
             true,
             Justification::centred);
        float knob = bottom - (bottom - top) * pc / 100;
        line(g, x, top, x, bottom, 0x101318, 6);
        if (available) {
            line(g, x, knob, x, bottom, mint, 4);
        }
        for (int k = 0; k <= 4; k++) {
            line(g, x + 17, top + (bottom - top) * k / 4, x + 23, top + (bottom - top) * k / 4, border);
        }
        fill(g, {x - 14, knob - 6, 28, 12}, available ? 0xc9d8d0 : 0x66717e, 4);
        line(g, x - 8, knob, x + 8, knob, 0x40504c);
        hit({x - 25, top - 12, 50, bottom - top + 24}, Volume, i, 0, available);
        text(g, str(names[i]), {x - 58, bottom + 22, 116, 26}, 14, ink, true, Justification::centred);
        button(g,
               c.muted && *c.muted ? String::fromUTF8("已静音") : String::fromUTF8("静音"),
               {x - 43, bottom + 62, 86, 32},
               ChannelMute,
               i,
               0,
               c.muted && *c.muted,
               deviceAvailable && c.canMute && c.muted.has_value());
    }
    line(g, 32, h - 112, w - 32, h - 112);
    String input = deviceAvailable && device.bufferRate
                       ? number(device.inputLatency * 1000.f / device.bufferRate, 2) + " ms"
                       : String(CharPointer_UTF8("—")),
           output = deviceAvailable && device.bufferRate
                        ? number(device.outputLatency * 1000.f / device.bufferRate, 2) + " ms"
                        : String(CharPointer_UTF8("—"));
    text(g,
         String::fromUTF8("时钟  ") +
             (device.clockAvailable ? str(device.clockName) : String::fromUTF8("未读取")) + "   " +
             (device.clockLocked ? String::fromUTF8("已同步") : String::fromUTF8("未锁定")),
         {32, h - 100, w * .42f, 26},
         12,
         muted);
    text(g,
         String::fromUTF8("输入延迟  ") + input + String::fromUTF8("     输出延迟  ") + output,
         {w * .44f, h - 100, w * .52f, 26},
         12,
         muted);
    text(g, String::fromUTF8("驱动版本  ") + str(device.driverVersion), {32, h - 70, w - 64, 24}, 11, muted);
}

void Workbench::effectsPage(Graphics &g) {
    float w = (float)getWidth();
    text(g, String::fromUTF8("效果器"), {32, 88, 300, 34}, 23, ink, true);
    text(g,
         String::fromUTF8("为每个通道安排独立的插入效果链，处理后再发送到不同去向。"),
         {32, 130, w - 64, 26},
         13,
         muted);
    fill(g, {32, 198, w - 64, 236}, surface, 10);
    icon(g, 1, {60, 224, 36, 36}, blue);
    text(g, "VST3", {112, 220, 300, 40}, 26, ink, true);
    text(g, String::fromUTF8("JUCE 插件宿主扩展位"), {112, 270, w - 160, 32}, 17, ink, true);
    text(g,
         String::fromUTF8("界面与通道上下文已预留。插件扫描、实例加载、实时音频处理和延迟补偿尚未接入。"),
         {112, 314, w - 160, 30},
         13,
         muted);
    text(g,
         String::fromUTF8("当前没有运行中的插件，音频仍由现有路由引擎处理。"),
         {112, 357, w - 160, 30},
         13,
         muted);
    const wchar_t *steps[] = {L"通道输入", L"插入效果链", L"发送 / 路由", L"目标通道"};
    float cw = (w - 112) / 4;
    for (int i = 0; i < 4; i++) {
        float x = 40 + i * cw;
        outline(g, {x, 488, cw - 36, 80}, i == 1 ? 0x527f69 : border, 7);
        text(g,
             str(steps[i]),
             {x + 12, 506, cw - 60, 40},
             16,
             i == 1 ? mint : ink,
             true,
             Justification::centred);
        if (i < 3) {
            icon(g, 3, {x + cw - 31, 512, 22, 32});
        }
    }
    text(g,
         String::fromUTF8("每条效果链预留：插件顺序、旁路、状态恢复、延迟与独立编辑器窗口。"),
         {40, 616, w - 80, 28},
         13,
         muted);
}

void Workbench::footer(Graphics &g) {
    float w = (float)getWidth(), y = (float)getHeight() - 28;
    fill(g, {0, y, w, 28}, 0x111418);
    line(g, 0, y, w, y, 0x2d333b);
    String s;
    if (Time::getMillisecondCounterHiRes() < noticeUntil) {
        s = notice;
    } else if (hoveredHit >= 0 && hoveredHit < (int)hits.size() && hits[hoveredHit].tip.isNotEmpty()) {
        s = hits[hoveredHit].tip;
    } else {
        s = String::fromUTF8("双击连线断开    Ctrl+Z 撤销    Delete 断开    Esc 取消");
    }
    text(g, s, {24, y + 1, w - 460, 26}, 11, muted);
    String status = session.online ? number((float)session.rate / 1000) + " kHz  /  " +
                                         String(session.frames) + String::fromUTF8(" 帧")
                    : deviceAvailable ? number(device.rate / 1000.f) + " kHz  /  " +
                                            String(device.preferred) + String::fromUTF8(" 帧")
                                      : String::fromUTF8("引擎离线");
    text(g,
         status + "     " + String(session.inputs) + " IN / " + String(session.outputs) + " OUT",
         {w - 422, y + 1, 398, 26},
         11,
         muted,
         false,
         Justification::centredRight);
}

void Workbench::paint(Graphics &g) {
    g.fillAll(colour(bg));
    hits.clear();
    header(g);
    if (page == 0) {
        toolbar(g);
        if (view == 0) {
            patch(g);
        } else {
            table(g);
        }
        inspector(g);
    }
    if (page == 1) {
        hostPage(g);
    }
    if (page == 2) {
        devicesPage(g);
    }
    if (page == 3) {
        audioPage(g);
    }
    if (page == 4) {
        effectsPage(g);
    }
    footer(g);
    if (menu.empty() && focusedHit >= 0 && focusedHit < (int)hits.size()) {
        outline(g, hits[focusedHit].box, mint, 5, 1.5f);
    }
    drawMenu(g);
}
} // namespace studio
