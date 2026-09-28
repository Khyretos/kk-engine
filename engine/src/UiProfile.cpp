#include "kke/UiProfile.h"

#include <algorithm>

namespace kke {

const char* uiProfileName(UiProfile profile) {
    switch (profile) {
    case UiProfile::Phone: return "phone";
    case UiProfile::Console: return "console";
    case UiProfile::Desktop: break;
    }
    return "desktop";
}

UiProfile uiProfileForTarget(const std::string& targetName, const std::string& override) {
    if (override == "desktop") return UiProfile::Desktop;
    if (override == "phone") return UiProfile::Phone;
    if (override == "console") return UiProfile::Console;
    if (targetName == "android" || targetName == "ios") return UiProfile::Phone;
    if (targetName == "steam-deck" || targetName == "handheld-pc") return UiProfile::Console;
    return UiProfile::Desktop;
}

float uiMarginFraction(UiProfile profile) {
    switch (profile) {
    case UiProfile::Phone: return 0.025f;
    case UiProfile::Console: return 0.05f;
    case UiProfile::Desktop: break;
    }
    return 0.0f;
}

ScreenRect safeScreenRect(float width, float height, const ScreenRect& system, float margin) {
    const bool whole = system.w <= 0.0f || system.h <= 0.0f;
    const float left = std::max(whole ? 0.0f : system.x, margin);
    const float top = std::max(whole ? 0.0f : system.y, margin);
    const float right = std::min(whole ? width : system.x + system.w, width - margin);
    const float bottom = std::min(whole ? height : system.y + system.h, height - margin);
    if (right - left < width * 0.5f || bottom - top < height * 0.5f) return { 0.0f, 0.0f, width, height }; // nonsense insets
    return { left, top, right - left, bottom - top };
}

} // namespace kke
