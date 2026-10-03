#pragma once

// The cookbook's own controls, as a C++ example of binding input
// (docs/cookbook/input.md). tests/test_cookbook.cpp drives this exact
// function with a fake keyboard and gamepad.

#include "kke/InputMap.h"
#include "kke/modules/InputModule.h"

namespace cookbook {

// --8<-- [start:bindings]
inline void addCookbookBindings(kke::InputMap& in) {
    using kke::InputModule;
    using kke::Trigger;

    // A button action: Tab on the keyboard, d-pad right on a pad (View/Back
    // and Start belong to the pause menu every game shares).
    in.defineAction({ "camera.next", "Next camera", "Camera" });
    in.addBinding(InputModule::bind("camera.next", InputModule::key(SDL_SCANCODE_TAB)));
    in.addBinding(InputModule::bind("camera.next", InputModule::pad(SDL_GAMEPAD_BUTTON_DPAD_RIGHT)));

    // Hold to charge, release to throw: two actions on one key.
    in.defineAction({ "throw.charge", "Charge a throw", "Actions" });
    in.defineAction({ "throw", "Throw", "Actions" });
    in.addBinding(InputModule::bind("throw.charge", InputModule::key(SDL_SCANCODE_Q), Trigger::Hold));
    in.addBinding(InputModule::bind("throw", InputModule::key(SDL_SCANCODE_Q), Trigger::Release));

    // A chord: Ctrl+R resets the level; plain R stays free for something else.
    in.defineAction({ "level.reset", "Reset the level", "Game" });
    kke::Binding reset = InputModule::bind("level.reset", InputModule::key(SDL_SCANCODE_R));
    reset.modifiers.push_back(InputModule::key(SDL_SCANCODE_LCTRL));
    in.addBinding(reset);

    // An axis from two keys and a trigger: zoom in with X or the right
    // trigger (analog: half pulled = half speed), out with the left one.
    in.defineAction({ "zoom", "Zoom", "Camera", "game", kke::ActionType::Axis1D });
    kke::Binding in1 = InputModule::bind("zoom", InputModule::key(SDL_SCANCODE_X), Trigger::Continuous);
    in.addBinding(in1);
    kke::Binding out1 = InputModule::bind("zoom", InputModule::key(SDL_SCANCODE_Z), Trigger::Continuous);
    out1.scale = -1.0f;
    in.addBinding(out1);
    kke::Binding padIn = InputModule::bind("zoom", InputModule::padAxis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER), Trigger::Continuous);
    padIn.deadzone = 0.1f;
    in.addBinding(padIn);
    kke::Binding padOut = InputModule::bind("zoom", InputModule::padAxis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER), Trigger::Continuous);
    padOut.deadzone = 0.1f;
    padOut.scale = -1.0f;
    in.addBinding(padOut);
}
// --8<-- [end:bindings]

} // namespace cookbook
