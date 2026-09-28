#pragma once

#include <string>

namespace kke {

// Which kind of screen and hands the game is played with. Menus, hints
// and margins differ per kind, so a phone's screens are the phone's own
// rather than a PC menu squeezed onto a phone.
//   Desktop: a PC with a mouse and keyboard (a pad may join).
//   Phone:   a phone or tablet, fingers first; menus fit the screen's
//            safe area and hints are tappable buttons.
//   Console: a TV or a handheld played with a pad (Steam Deck, PC
//            handhelds); menus keep the TV's title-safe margin.
enum class UiProfile { Desktop, Phone, Console };

const char* uiProfileName(UiProfile profile); // "desktop", "phone", "console"

// From the hardware target (kke/HardwareTarget.h): android and ios are
// phones, steam-deck and handheld-pc consoles, the rest desktops.
// `override` (KKE_UI_PROFILE: desktop|phone|console) wins when it names one.
UiProfile uiProfileForTarget(const std::string& targetName, const std::string& override = {});

// The margin every screen keeps free on each side, as a fraction of the
// screen's short side, besides what the system itself covers (a notch,
// rounded corners, the status and navigation bars): phones 2.5%, consoles
// 5% (a TV's title-safe area), desktops none.
float uiMarginFraction(UiProfile profile);

// A rectangle of the window, in pixels.
struct ScreenRect {
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
    bool operator==(const ScreenRect&) const = default;
};

// The part of a `width` x `height` screen that menus may use: the system's
// safe area `system` (empty = all of it) shrunk further to keep `margin`
// pixels from every edge. Pure, for the tests.
ScreenRect safeScreenRect(float width, float height, const ScreenRect& system, float margin);

} // namespace kke
