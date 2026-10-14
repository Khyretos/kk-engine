#include "kke/ToneMap.h"

#include "kke/Sky.h"

#include <algorithm>
#include <cmath>

namespace kke {

namespace {

// The constants are shaders/tonemap.glsl's (Wrensch's minimal AgX, MIT,
// LICENSES/AgX-minimal-MIT.txt); glm::mat3 takes columns in the same order
// as GLSL's mat3 constructor.
const glm::mat3 kAgxInset(0.842479062253094f, 0.0423282422610123f, 0.0423756549057051f, 0.0784335999999992f, 0.878468636469772f,
                          0.0784336f, 0.0792237451477643f, 0.0791661274605434f, 0.879142973793104f);
const glm::mat3 kAgxOutset(1.19687900512017f, -0.0528968517574562f, -0.0529716355144438f, -0.0980208811401368f, 1.15190312990417f,
                           -0.0980434501171241f, -0.0990297440797205f, -0.0989611768448433f, 1.15107367264116f);
constexpr float kAgxMinEv = -12.47393f;
constexpr float kAgxMaxEv = 4.026069f;

glm::vec3 agxSigmoid(const glm::vec3& x) {
    const glm::vec3 x2 = x * x;
    const glm::vec3 x4 = x2 * x2;
    return 15.5f * x4 * x2 - 40.14f * x4 * x + 31.96f * x4 - 6.868f * x2 * x + 0.4298f * x2 + 0.1191f * x - 0.00232f;
}

glm::vec3 applyLook(glm::vec3 v, const PostParams& p) {
    v = glm::pow(glm::max(v * glm::vec3(p.lookSlope) + glm::vec3(p.lookOffset), glm::vec3(0.0f)), glm::vec3(p.lookPower));
    const float luma = glm::dot(v, glm::vec3(0.2126f, 0.7152f, 0.0722f));
    return luma + p.lookSlope.a * (v - luma);
}

glm::vec3 agx(const glm::vec3& c, const PostParams& p, bool look) {
    glm::vec3 v = kAgxInset * c;
    v = glm::clamp(glm::log2(glm::max(v, glm::vec3(1e-10f))), kAgxMinEv, kAgxMaxEv);
    v = (v - kAgxMinEv) / (kAgxMaxEv - kAgxMinEv);
    v = agxSigmoid(v);
    if (look) v = applyLook(v, p);
    v = kAgxOutset * v;
    return glm::pow(glm::max(v, glm::vec3(0.0f)), glm::vec3(2.2f));
}

glm::vec3 curve(glm::vec3 c, int op) {
    if (op == 1) {
        c *= 0.6f;
        return glm::clamp((c * (2.51f * c + 0.03f)) / (c * (2.43f * c + 0.59f) + 0.14f), 0.0f, 1.0f);
    }
    if (op == 2) return c / (c + glm::vec3(1.0f));
    return glm::clamp(c, 0.0f, 1.0f); // None
}

} // namespace

PostParams makePostParams(ToneMapper op, float exposure, const ColorGrade& grade) {
    PostParams p;
    p.tone = glm::vec4(static_cast<float>(op), exposure, 0.0f, 0.0f);
    p.lookSlope = glm::vec4(grade.slope, grade.saturation);
    p.lookOffset = glm::vec4(grade.offset, grade.isIdentity() ? 0.0f : 1.0f);
    p.lookPower = glm::vec4(grade.power, 0.0f);
    return p;
}

glm::vec3 toneMapCpu(const glm::vec3& sceneLight, const PostParams& p) {
    const glm::vec3 c = sceneLight * p.tone.y;
    const int op = static_cast<int>(p.tone.x + 0.5f);
    const bool look = p.lookOffset.a > 0.5f;
    if (op == 0) return agx(c, p, look);
    glm::vec3 v = curve(c, op);
    if (!look) return v;
    v = applyLook(glm::pow(v, glm::vec3(1.0f / 2.2f)), p);
    return glm::pow(glm::clamp(v, 0.0f, 1.0f), glm::vec3(2.2f));
}

// Every curve is increasing in each channel and close to diagonal, so a
// per-channel bisection on the log of the scene light, repeated a few
// times to settle the small cross-talk between channels, converges.
glm::vec3 inverseToneMapCpu(const glm::vec3& displayLinear, const PostParams& p) {
    const glm::vec3 want = glm::clamp(displayLinear, 0.0f, 1.0f);
    glm::vec3 x = want;
    for (int round = 0; round < 4; ++round) {
        for (int ch = 0; ch < 3; ++ch) {
            float lo = -24.0f, hi = 8.0f; // log2 of the scene light
            for (int i = 0; i < 48; ++i) {
                const float mid = 0.5f * (lo + hi);
                glm::vec3 probe = x;
                probe[ch] = std::exp2(mid);
                if (toneMapCpu(probe, p)[ch] < want[ch]) lo = mid;
                else hi = mid;
            }
            x[ch] = want[ch] <= 0.0f ? 0.0f : std::exp2(0.5f * (lo + hi));
        }
    }
    return x;
}

glm::vec3 sceneClearColor(const glm::vec3& srgb, const PostParams& p) {
    auto lin = [](float v) { return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f); };
    return inverseToneMapCpu(glm::vec3(lin(srgb.r), lin(srgb.g), lin(srgb.b)), p);
}

} // namespace kke
