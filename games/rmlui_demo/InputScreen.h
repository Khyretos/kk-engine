#pragma once

#include "kke/InputMap.h"
#include "kke/Module.h"

#include <RmlUi/Core/DataModelHandle.h>

#include <string>
#include <vector>

namespace kke {
class InputModule;
class UiModule;
}

namespace kke_demo {

// The "Input" screen: a live tester for every connected peripheral
// (buttons light up, axes move, hats, gyro/accel, touchpads; identical
// twins numbered and nameable, identify/rumble, swap) and the
// Tarkov-style bindings editor (any number of bindings per action, each
// with its own trigger: press/hold/tap/double tap/toggle/...; chords;
// capture by pressing the combination). Also defines this demo's game
// actions, so there is something to bind and watch.
// The data model must exist before input.rml loads: ShowcaseModule
// depends on this module for that reason.
class InputScreen : public kke::Module {
public:
    const char* name() const override { return "InputScreen"; }
    std::vector<kke::ModuleDependency> dependencies() const override;
    void init(kke::Application& app) override;
    void update(const kke::UpdateContext& ctx) override;
    void frameStart(const kke::UpdateContext& ctx) override;
    bool capturing() const { return m_capturing; }

    struct DeviceView { std::string ref, label, type, info, twin; bool active = false, selected = false, connected = true, rumble = false; };
    struct Cell { std::string name; bool on = false; };
    struct AxisView { std::string name, value, left, width; };
    struct BindView { int index = -1; std::string text, trigger; bool conflict = false; };
    struct ActionView { std::string id, label, category, value, type; bool held = false, flash = false, first = false; std::vector<BindView> bindings; };

private:
    void defineGameActions();
    void refreshDevices();
    void refreshLive();
    void refreshActions();
    void startCapture(const std::string& action);
    void finishCapture();

    kke::Application* m_app = nullptr;
    kke::InputModule* m_input = nullptr;
    kke::UiModule* m_ui = nullptr;
    Rml::DataModelHandle m_model;

    std::vector<DeviceView> m_devices;
    std::string m_selected;          // device ref (decimal) shown in the live view
    std::string m_selectedName, m_selectedKey, m_alias;
    std::vector<Cell> m_buttons, m_padButtons, m_hats;
    std::vector<AxisView> m_axes;
    std::string m_sensors, m_touch;
    std::vector<ActionView> m_actions;
    std::vector<float> m_flash;      // per action: seconds of "just fired" highlight left
    bool m_capturing = false;
    std::string m_captureAction, m_captureHint, m_lastInput, m_status;
    double m_devicesRefreshedAt = -1.0;
};

} // namespace kke_demo
