// Contact shadows (ENG-F16, kke/ContactShadows.h): the march the shader does.

#include "kke/ContactShadows.h"

#include <gtest/gtest.h>

#include <cmath>

using kke::ContactShadowSettings;

namespace {

// A flat floor 1 m below the camera's eye line, seen from the side, with an
// optional box (a step) on it: the view depth the frame drew where a
// view-space point's pixel lands is the depth of whatever the camera ray
// through that point hits first. Kept simple: the "frame" is the floor
// plane y = -1 plus a box occupying x in [bx0, bx1], y up to boxTop, any z.
struct Scene {
    bool box = false;
    float bx0 = 0.5f, bx1 = 0.8f, boxTop = -0.7f;
    float depthAt(const float v[3]) const {
        // Camera ray direction through v (camera at origin).
        const float dx = v[0] / -v[2], dy = v[1] / -v[2];
        float best = 0.0f;
        // Floor: y = -1 at depth d where dy * d = -1.
        if (dy < 0.0f) best = -1.0f / dy;
        if (box) {
            // Box top face y = boxTop over x in [bx0, bx1].
            if (dy < 0.0f) {
                const float d = boxTop / dy;
                const float x = dx * d;
                if (x >= bx0 && x <= bx1 && (best == 0.0f || d < best)) best = d;
            }
            // Box's near side face x = bx0 (seen from x < bx0), y in [-1, boxTop].
            if (dx > 0.0f) {
                const float d = bx0 / dx;
                const float y = dy * d;
                if (y >= -1.0f && y <= boxTop && (best == 0.0f || d < best)) best = d;
            }
        }
        return best;
    }
};

float march(const Scene& scene, const float pos[3], const float toSun[3], const ContactShadowSettings& s = {}) {
    return kke::contactShadowMarch(pos, toSun, [&](const float v[3]) { return scene.depthAt(v); }, s);
}

} // namespace

TEST(ContactShadows, OpenFloorIsLit) {
    Scene scene;
    const float floorPoint[3] = { -0.2f, -1.0f, -4.0f };
    const float sun[3] = { 0.6f, 0.8f, 0.0f }; // up and to the right
    EXPECT_FLOAT_EQ(march(scene, floorPoint, sun), 1.0f);
}

TEST(ContactShadows, FloorAtTheFootOfABoxIsShadowed) {
    Scene scene;
    scene.box = true; // a 30 cm step from x = 0.5 to 0.8, 30 cm high
    // 5 cm left of the box, the sun low on the right: the march runs into the box's side.
    const float atFoot[3] = { 0.45f, -1.0f, -4.0f };
    const float lowSun[3] = { 0.94f, 0.34f, 0.0f };
    EXPECT_LT(march(scene, atFoot, lowSun), 0.3f);
    // Far from it (beyond the march) it stays lit.
    const float away[3] = { -1.0f, -1.0f, -4.0f };
    EXPECT_FLOAT_EQ(march(scene, away, lowSun), 1.0f);
    // The sun behind the point (to the left) casts no contact shadow from the box.
    const float leftSun[3] = { -0.94f, 0.34f, 0.0f };
    EXPECT_FLOAT_EQ(march(scene, atFoot, leftSun), 1.0f);
}

TEST(ContactShadows, ThickOccludersInFrontDontCount) {
    Scene scene;
    scene.box = true;
    ContactShadowSettings thin;
    thin.thickness = 0.001f; // nothing is that thin: every hit is "a separate object"
    const float atFoot[3] = { 0.45f, -1.0f, -4.0f };
    const float lowSun[3] = { 0.94f, 0.34f, 0.0f };
    EXPECT_FLOAT_EQ(march(scene, atFoot, lowSun, thin), 1.0f);
}

TEST(ContactShadows, FadeWithDistance) {
    ContactShadowSettings s;
    s.fadeFrom = 40.0f;
    s.maxDistance = 60.0f;
    EXPECT_FLOAT_EQ(kke::contactShadowFade(10.0f, s), 1.0f);
    EXPECT_FLOAT_EQ(kke::contactShadowFade(50.0f, s), 0.5f);
    EXPECT_FLOAT_EQ(kke::contactShadowFade(70.0f, s), 0.0f);
}
