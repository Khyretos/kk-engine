#include "kke/Upscale.h"

#include <algorithm>

namespace kke {

float fsrEasuWeight(float d2, float lob) {
    const float clp = 1.0f / lob;
    d2 = std::min(d2, clp);
    float wB = 0.4f * d2 - 1.0f, wA = lob * d2 - 1.0f;
    wB *= wB;
    wA *= wA;
    wB = 1.5625f * wB - 0.5625f;
    return wB * wA;
}

void fsrToFilterSpace(const float in[3], float out[3]) {
    const float m = std::max(in[0], std::max(in[1], in[2]));
    for (int i = 0; i < 3; ++i) out[i] = in[i] / (1.0f + m);
}

void fsrFromFilterSpace(const float in[3], float out[3]) {
    const float m = std::max(in[0], std::max(in[1], in[2]));
    const float d = std::max(1.0f - m, 1.0f / 4096.0f);
    for (int i = 0; i < 3; ++i) out[i] = in[i] / d;
}

} // namespace kke
