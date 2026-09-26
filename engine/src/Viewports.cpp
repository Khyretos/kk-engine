#include "kke/Viewports.h"

#include <algorithm>
#include <cmath>

namespace kke {

std::vector<ViewRect> splitScreen(int count, bool sideBySide) {
    switch (std::clamp(count, 1, 4)) {
    case 1: return { { 0.0f, 0.0f, 1.0f, 1.0f } };
    case 2:
        if (sideBySide) return { { 0.0f, 0.0f, 0.5f, 1.0f }, { 0.5f, 0.0f, 0.5f, 1.0f } };
        return { { 0.0f, 0.0f, 1.0f, 0.5f }, { 0.0f, 0.5f, 1.0f, 0.5f } };
    case 3: return { { 0.0f, 0.0f, 0.5f, 0.5f }, { 0.5f, 0.0f, 0.5f, 0.5f }, { 0.0f, 0.5f, 0.5f, 0.5f } };
    default:
        return { { 0.0f, 0.0f, 0.5f, 0.5f }, { 0.5f, 0.0f, 0.5f, 0.5f }, { 0.0f, 0.5f, 0.5f, 0.5f }, { 0.5f, 0.5f, 0.5f, 0.5f } };
    }
}

ViewRect pictureInPicture(int corner, float size, float margin) {
    size = std::clamp(size, 0.05f, 1.0f);
    margin = std::clamp(margin, 0.0f, (1.0f - size) * 0.5f);
    ViewRect r{ margin, margin, size, size };
    if (corner == 1 || corner == 3) r.x = 1.0f - margin - size;
    if (corner == 2 || corner == 3) r.y = 1.0f - margin - size;
    return r;
}

VkRect2D viewPixels(const ViewRect& r, VkExtent2D extent) {
    auto px = [](float f, uint32_t n) {
        return static_cast<int32_t>(std::lround(std::clamp(f, 0.0f, 1.0f) * static_cast<float>(n)));
    };
    const int32_t x0 = px(r.x, extent.width), x1 = px(r.x + r.w, extent.width);
    const int32_t y0 = px(r.y, extent.height), y1 = px(r.y + r.h, extent.height);
    return { { x0, y0 }, { static_cast<uint32_t>(std::max(0, x1 - x0)), static_cast<uint32_t>(std::max(0, y1 - y0)) } };
}

float viewAspect(const ViewRect& r, VkExtent2D extent) {
    const VkRect2D p = viewPixels(r, extent);
    return p.extent.height ? static_cast<float>(p.extent.width) / static_cast<float>(p.extent.height) : 1.0f;
}

} // namespace kke
