#include "CommandHud.h"

#include "kke/Application.h"
#include "kke/Log.h"
#include "kke/modules/UiModule.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <SDL3/SDL.h>

#include <cstdio>

namespace command_kit {

namespace {

std::string px(float v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.0fpx", static_cast<double>(v));
    return buf;
}

} // namespace

CommandHud::CommandHud(kke::Application& app) : m_app(app) {}

CommandHud::~CommandHud() {
    if (m_doc) m_doc->Close();
}

bool CommandHud::build(const std::string& title) {
    m_title = title;
    auto* ui = m_app.getModule<kke::UiModule>();
    if (!ui || !ui->context()) return false;
    Rml::Context* ctx = ui->context();
    Rml::DataModelConstructor c = ctx->CreateDataModel("cmd");
    if (!c) return false;
    if (auto s = c.RegisterStruct<ButtonView>()) {
        s.RegisterMember("icon", &ButtonView::icon);
        s.RegisterMember("label", &ButtonView::label);
        s.RegisterMember("key", &ButtonView::key);
        s.RegisterMember("on", &ButtonView::on);
    }
    c.RegisterArray<std::vector<ButtonView>>();
    if (auto s = c.RegisterStruct<LineView>()) {
        s.RegisterMember("text", &LineView::text);
        s.RegisterMember("color", &LineView::color);
    }
    c.RegisterArray<std::vector<LineView>>();
    if (auto s = c.RegisterStruct<Item>()) {
        s.RegisterMember("icon", &Item::icon);
        s.RegisterMember("label", &Item::label);
        s.RegisterMember("left", &Item::left);
        s.RegisterMember("top", &Item::top);
        s.RegisterMember("picked", &Item::picked);
    }
    c.RegisterArray<std::vector<Item>>();
    c.Bind("title", &m_title);
    c.Bind("hint", &m_hint);
    c.Bind("toast", &m_toast);
    c.Bind("buttons", &m_buttons);
    c.Bind("lines", &m_lines);
    c.Bind("items", &m_items);
    c.Bind("reticle", &m_reticle);
    c.Bind("box", &m_box);
    c.Bind("box_left", &m_boxLeft);
    c.Bind("box_top", &m_boxTop);
    c.Bind("box_width", &m_boxWidth);
    c.Bind("box_height", &m_boxHeight);
    c.Bind("wheel_open", &m_wheelOpen);
    c.Bind("wheel_left", &m_wheelLeft);
    c.Bind("wheel_top", &m_wheelTop);
    c.BindEventCallback("press", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args) {
        if (!args.empty() && onButton) onButton(args[0].Get<int>());
    });
    m_model = c.GetModelHandle();

    const char* base = SDL_GetBasePath();
    const std::string path = std::string(base ? base : "") + "ui/command_hud.rml";
    m_doc = ctx->LoadDocument(path);
    if (!m_doc) {
        kke::log::get("CommandHud")->warn("could not load {}", path);
        return false;
    }
    m_doc->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
    return true;
}

void CommandHud::setButtons(std::vector<Button> buttons) {
    std::vector<ButtonView> next;
    for (Button& b : buttons) next.push_back({ std::move(b.icon), std::move(b.label), std::move(b.key), b.on });
    bool same = next.size() == m_buttons.size();
    for (size_t i = 0; same && i < next.size(); ++i)
        same = next[i].icon == m_buttons[i].icon && next[i].label == m_buttons[i].label && next[i].key == m_buttons[i].key &&
               next[i].on == m_buttons[i].on;
    if (same) return;
    m_buttons = std::move(next);
    if (m_model) m_model.DirtyVariable("buttons");
}

void CommandHud::setWheel(std::vector<WheelItem> items) { m_wheel = std::move(items); }

void CommandHud::setLines(std::vector<Line> lines) {
    std::vector<LineView> next;
    for (Line& l : lines) next.push_back({ std::move(l.text), std::move(l.color) });
    bool same = next.size() == m_lines.size();
    for (size_t i = 0; same && i < next.size(); ++i) same = next[i].text == m_lines[i].text && next[i].color == m_lines[i].color;
    if (same) return;
    m_lines = std::move(next);
    if (m_model) m_model.DirtyVariable("lines");
}

void CommandHud::setHint(const std::string& hint) {
    if (hint == m_hint) return;
    m_hint = hint;
    if (m_model) m_model.DirtyVariable("hint");
}

void CommandHud::toast(const std::string& text, float seconds) {
    m_toastLeft = seconds;
    if (text == m_toast) return;
    m_toast = text;
    if (m_model) m_model.DirtyVariable("toast");
}

void CommandHud::update(const CommandInput::Frame& in, float dt) {
    if (!m_model) return;
    const float scale = m_app.window().pixelsPerPoint();
    auto set = [this](auto& field, const auto& value, const char* var) {
        if (field == value) return;
        field = value;
        m_model.DirtyVariable(var);
    };
    if (m_toastLeft > 0.0f) {
        m_toastLeft -= dt;
        if (m_toastLeft <= 0.0f) set(m_toast, std::string(), "toast");
    }
    set(m_reticle, in.reticle && !in.wheelOpen, "reticle");
    set(m_box, in.dragging, "box");
    if (in.dragging) {
        const glm::vec2 mn = glm::min(in.boxA, in.boxB) * scale, mx = glm::max(in.boxA, in.boxB) * scale;
        set(m_boxLeft, px(mn.x), "box_left");
        set(m_boxTop, px(mn.y), "box_top");
        set(m_boxWidth, px(mx.x - mn.x), "box_width");
        set(m_boxHeight, px(mx.y - mn.y), "box_height");
    }
    set(m_wheelOpen, in.wheelOpen, "wheel_open");
    if (in.wheelOpen) {
        const glm::vec2 c = in.wheelCenter * scale;
        set(m_wheelLeft, px(c.x), "wheel_left");
        set(m_wheelTop, px(c.y), "wheel_top");
        // Items on a ring around the centre (the wheel's own directions).
        kke::RadialMenu ring(static_cast<int>(m_wheel.size()));
        const float radius = 120.0f * scale;
        std::vector<Item> items;
        for (size_t i = 0; i < m_wheel.size(); ++i) {
            const glm::vec2 d = ring.direction(static_cast<int>(i)) * radius;
            items.push_back({ m_wheel[i].icon, m_wheel[i].label, px(d.x), px(d.y), static_cast<int>(i) == in.wheelPicked });
        }
        bool same = items.size() == m_items.size();
        for (size_t i = 0; same && i < items.size(); ++i)
            same = items[i].left == m_items[i].left && items[i].top == m_items[i].top && items[i].picked == m_items[i].picked &&
                   items[i].label == m_items[i].label;
        if (!same) {
            m_items = std::move(items);
            m_model.DirtyVariable("items");
        }
    }
}

bool CommandHud::overButtons(const glm::vec2& points) {
    Rml::Element* bar = m_doc ? m_doc->GetElementById("bar") : nullptr;
    if (!bar) return false;
    const float scale = m_app.window().pixelsPerPoint();
    return bar->IsPointWithinElement(Rml::Vector2f(points.x * scale, points.y * scale));
}

} // namespace command_kit
