// ENG-F14: the FSR 1-style upscaler's maths (kke/Upscale.h, shaders/upscale_fsr1.glsl).

#include "kke/Upscale.h"

#include <gtest/gtest.h>

TEST(Upscale, EasuWeightIsALanczosLobe) {
    EXPECT_NEAR(kke::fsrEasuWeight(0.0f, 0.25f), 1.0f, 1e-6f) << "the centre tap counts fully";
    EXPECT_NEAR(kke::fsrEasuWeight(1.0f, 0.25f), 0.0f, 0.02f) << "zero at one texel, as Lanczos-2";
    EXPECT_LT(kke::fsrEasuWeight(2.25f, 0.25f), 0.0f) << "the negative lobe between 1 and 2 texels sharpens";
    EXPECT_NEAR(kke::fsrEasuWeight(4.0f, 0.25f), 0.0f, 1e-5f) << "nothing at two texels";
    EXPECT_NEAR(kke::fsrEasuWeight(100.0f, 0.25f), kke::fsrEasuWeight(4.0f, 0.25f), 1e-6f) << "clipped beyond the window";
    // A flatter lobe (lob 0.5, flat areas) cuts off sooner: no negative ring.
    EXPECT_GE(kke::fsrEasuWeight(1.5f, 0.5f), kke::fsrEasuWeight(1.5f, 0.25f));
}

TEST(Upscale, FilterSpaceRoundTrips) {
    for (float v : { 0.0f, 0.18f, 1.0f, 8.0f, 60.0f }) {
        const float c[3] = { v, v * 0.5f, v * 0.25f };
        float f[3], back[3];
        kke::fsrToFilterSpace(c, f);
        EXPECT_LT(f[0], 1.0f) << "HDR fits under 1";
        kke::fsrFromFilterSpace(f, back);
        for (int i = 0; i < 3; ++i) EXPECT_NEAR(back[i], c[i], 1e-3f * (1.0f + c[i])) << v;
    }
}
