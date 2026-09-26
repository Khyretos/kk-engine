#pragma once

#include <volk.h>

#include <vector>

namespace kke {

// Split screen and picture-in-picture (ACTION_PLAN.md 1.3): where each
// view goes in the window. Pure layout, unit-tested in
// tests/test_viewports.cpp; Application::views() draws them.

// A part of the window, as fractions of its size (0..1, top-left origin).
struct ViewRect {
    float x = 0.0f, y = 0.0f, w = 1.0f, h = 1.0f;
};

// The usual split screen for 1-4 players: 1 = whole window; 2 = side by
// side (or stacked, top and bottom, with sideBySide = false); 3 = two on
// top, one bottom left (the fourth quarter stays empty); 4 = quarters.
// Out-of-range counts are clamped to 1..4.
std::vector<ViewRect> splitScreen(int count, bool sideBySide = true);

// A small view in a corner over the others (a rear-view mirror, a map, a
// security camera): `size` is its width as a fraction of the window's,
// `margin` the gap to the edges (same units); the height keeps the
// window's aspect ratio. corner: 0 = top left, 1 = top right,
// 2 = bottom left, 3 = bottom right.
ViewRect pictureInPicture(int corner, float size = 0.3f, float margin = 0.02f);

// The rect in pixels of an image `extent` big. Neighbouring views share
// their edge pixel for pixel (both round the same fraction), so there is
// neither a gap nor an overlap between them.
VkRect2D viewPixels(const ViewRect& r, VkExtent2D extent);
// Width over height of the rect in pixels (the view's projection aspect).
float viewAspect(const ViewRect& r, VkExtent2D extent);

} // namespace kke
