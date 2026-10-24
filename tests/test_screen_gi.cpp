#include "kke/ScreenGi.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <vector>

// ENG-F01: screen-space GI with a visibility bitmask, on scenes ray-cast
// into a depth and light image (view space, camera at the origin looking down -z).

namespace {

struct Scene {
    bool redWall = false; // the plane x = 1 (z from -8 to -2), red
    bool pole = false;    // a thin blue pole, radius 3 cm, 0.4 m in front of the back wall at x = -0.5
};

struct Hit {
    float depth = 0.0f; // along -z; 0 = nothing
    glm::vec3 light{0.0f};
};

Hit cast(const Scene& s, const glm::vec3& dir) {
    Hit best;
    float bestT = std::numeric_limits<float>::max();
    auto take = [&](float t, const glm::vec3& light) {
        if (t > 0.0f && t < bestT) {
            bestT = t;
            best.depth = -(dir * t).z;
            best.light = light;
        }
    };
    // The back wall, grey: z = -5.
    if (dir.z < 0.0f) take(-5.0f / dir.z, glm::vec3(0.2f));
    if (s.redWall && dir.x > 0.0f) {
        const float t = 1.0f / dir.x;
        const glm::vec3 p = dir * t;
        if (p.z < -2.0f && p.z > -8.0f) take(t, glm::vec3(1.0f, 0.0f, 0.0f));
    }
    if (s.pole) {
        // Vertical cylinder round (x, z) = (-0.5, -4.6).
        const glm::vec2 o(-0.5f, -4.6f), d(dir.x, dir.z);
        const float a = glm::dot(d, d), b = -2.0f * glm::dot(d, o), c = glm::dot(o, o) - 0.03f * 0.03f;
        const float disc = b * b - 4.0f * a * c;
        if (disc >= 0.0f) take((-b - std::sqrt(disc)) / (2.0f * a), glm::vec3(0.0f, 0.0f, 1.0f));
    }
    return best;
}

struct Frame {
    kke::ScreenGiView view;
    std::vector<Hit> pixels;
};

Frame render(const Scene& s, int w = 320, int h = 180) {
    Frame f;
    f.view.width = w;
    f.view.height = h;
    f.view.tanHalfFovY = 0.414f;
    f.pixels.resize(static_cast<size_t>(w * h));
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            f.pixels[static_cast<size_t>(y * w + x)] = cast(s, glm::normalize(kke::screenGiViewPos(f.view, float(x), float(y), 1.0f)));
    return f;
}

// The method's view of a frame (the frame must outlive the returned view).
kke::ScreenGiView viewOf(const Frame& f) {
    kke::ScreenGiView v = f.view;
    const Frame* fp = &f;
    v.depth = [fp](int x, int y) { return fp->pixels[static_cast<size_t>(y * fp->view.width + x)].depth; };
    v.light = [fp](int x, int y) { return fp->pixels[static_cast<size_t>(y * fp->view.width + x)].light; };
    return v;
}

// The pixel showing view-space point p.
void pixelOf(const kke::ScreenGiView& v, const glm::vec3& p, int& x, int& y) {
    const float aspect = static_cast<float>(v.width) / static_cast<float>(v.height);
    const float ndcX = p.x / (-p.z * aspect * v.tanHalfFovY), ndcY = p.y / (-p.z * v.tanHalfFovY);
    x = static_cast<int>(std::floor((ndcX + 1.0f) * 0.5f * static_cast<float>(v.width)));
    y = static_cast<int>(std::floor((1.0f - ndcY) * 0.5f * static_cast<float>(v.height)));
}

} // namespace

TEST(ScreenGi, SectorsCoverTheirShareOfTheHemisphere) {
    EXPECT_EQ(kke::screenGiSectors(0.0f, 1.0f), 0xffffffffu);
    EXPECT_EQ(kke::screenGiSectors(0.0f, 0.5f), 0x0000ffffu);
    EXPECT_EQ(kke::screenGiSectors(0.5f, 1.0f), 0xffff0000u);
    EXPECT_EQ(kke::screenGiSectors(0.4f, 0.4f), 0u); // nothing covered
    EXPECT_EQ(kke::screenGiSectors(1.0f, 1.0f), 0u);
    // Cosine-weighted: the normal is the middle, the surface's tangent the ends.
    EXPECT_NEAR(kke::screenGiSector(0.3f, 0.3f), 0.5f, 1e-6f);
    EXPECT_NEAR(kke::screenGiSector(0.3f + 1.5707963f, 0.3f), 1.0f, 1e-6f);
    EXPECT_NEAR(kke::screenGiSector(-3.0f, 0.0f), 0.0f, 1e-6f);
    // 30 degrees from the normal holds a quarter of the slice's weight (sin 30 = 0.5).
    EXPECT_NEAR(kke::screenGiSector(0.5235988f, 0.0f), 0.75f, 1e-5f);
}

TEST(ScreenGi, AnOpenWallSeesEverything) {
    const Frame f = render(Scene{});
    const kke::ScreenGiView v = viewOf(f);
    int x, y;
    pixelOf(v, glm::vec3(-0.6f, 0.2f, -5.0f), x, y);
    const kke::ScreenGiResult r = kke::screenGi(v, x, y, glm::vec3(0, 0, 1));
    EXPECT_NEAR(r.visibility, 1.0f, 1e-4f);
    EXPECT_NEAR(glm::length(r.bounce), 0.0f, 1e-6f);
}

TEST(ScreenGi, ARedWallTintsAndShadesTheWallBesideIt) {
    const Frame f = render(Scene{ true, false });
    const kke::ScreenGiView v = viewOf(f);
    int x, y;
    pixelOf(v, glm::vec3(0.85f, 0.0f, -5.0f), x, y);
    const kke::ScreenGiResult near = kke::screenGi(v, x, y, glm::vec3(0, 0, 1));
    pixelOf(v, glm::vec3(0.3f, 0.0f, -5.0f), x, y);
    const kke::ScreenGiResult far = kke::screenGi(v, x, y, glm::vec3(0, 0, 1));
    std::printf("red wall: 15 cm away visibility %.3f bounce (%.3f %.3f %.3f); 70 cm away %.3f (%.3f %.3f %.3f)\n",
                static_cast<double>(near.visibility), static_cast<double>(near.bounce.r), static_cast<double>(near.bounce.g),
                static_cast<double>(near.bounce.b), static_cast<double>(far.visibility), static_cast<double>(far.bounce.r),
                static_cast<double>(far.bounce.g), static_cast<double>(far.bounce.b));
    EXPECT_LT(near.visibility, 0.9f) << "the corner is darker";
    EXPECT_GT(near.bounce.r, 0.05f) << "and red";
    EXPECT_NEAR(near.bounce.g, 0.0f, 1e-6f);
    EXPECT_GT(far.visibility, near.visibility) << "less so further away";
    EXPECT_LT(far.bounce.r, near.bounce.r);
}

// The improvement over horizon-based methods: a thin pole 40 cm in front
// of a wall shades only the sectors it covers, not everything behind it.
TEST(ScreenGi, AThinPoleDoesNotDarkenTheWallBehindIt) {
    const Frame f = render(Scene{ false, true });
    const kke::ScreenGiView v = viewOf(f);
    int x, y;
    pixelOf(v, glm::vec3(-0.35f, 0.0f, -5.0f), x, y);
    kke::ScreenGiSettings bitmask, horizon;
    horizon.horizonOnly = true;
    const kke::ScreenGiResult a = kke::screenGi(v, x, y, glm::vec3(0, 0, 1), bitmask);
    const kke::ScreenGiResult b = kke::screenGi(v, x, y, glm::vec3(0, 0, 1), horizon);
    std::printf("thin pole: visibility with the bitmask %.3f, horizon-based %.3f\n", static_cast<double>(a.visibility),
                static_cast<double>(b.visibility));
    EXPECT_GT(a.visibility, 0.85f);
    EXPECT_LT(1.0f - a.visibility, 0.7f * (1.0f - b.visibility)) << "at least 30% less shading where nothing is";
    EXPECT_GT(a.bounce.b, 0.0f) << "the pole's blue still reaches the wall";
}

TEST(ScreenGi, SkyPixelsAndNoSlicesLeaveItAlone) {
    const Frame f = render(Scene{});
    kke::ScreenGiView v = viewOf(f);
    v.depth = [](int, int) { return 0.0f; };
    EXPECT_FLOAT_EQ(kke::screenGi(v, 10, 10, glm::vec3(0, 0, 1)).visibility, 1.0f);
    kke::ScreenGiSettings none;
    none.slices = 0;
    EXPECT_FLOAT_EQ(kke::screenGi(viewOf(f), 10, 10, glm::vec3(0, 0, 1), none).visibility, 1.0f);
}
