#include "kke/AnimCompress.h"

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <utility>

namespace kke {

namespace {

constexpr float kInvSqrt2 = 0.70710678f;

glm::quat nlerp(const glm::quat& a, glm::quat b, float t) {
    if (glm::dot(a, b) < 0.0f) b = -b;
    return glm::normalize(glm::quat(a.w + (b.w - a.w) * t, a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t));
}

// The angle between two rotations, radians. Through the relative rotation's vector part: acos of a dot product
// near 1 cannot tell angles under about 0.0007 radians from each other in single precision, and the tolerance is smaller.
float angleBetween(const glm::quat& a, const glm::quat& b) {
    const glm::quat r = glm::conjugate(a) * b;
    return 2.0f * std::atan2(glm::length(glm::vec3(r.x, r.y, r.z)), std::fabs(r.w));
}

PackedAnimation::Quat48 packQuat(glm::quat q) {
    q = glm::normalize(q);
    const float c[4] = { q.x, q.y, q.z, q.w };
    int largest = 0;
    for (int i = 1; i < 4; ++i)
        if (std::fabs(c[i]) > std::fabs(c[largest])) largest = i;
    const float sign = c[largest] < 0.0f ? -1.0f : 1.0f; // the largest stays positive: q and -q are one rotation
    PackedAnimation::Quat48 out;
    out.largest = static_cast<uint8_t>(largest);
    int16_t* dst[3] = { &out.a, &out.b, &out.c };
    int k = 0;
    for (int i = 0; i < 4; ++i) {
        if (i == largest) continue;
        const float v = std::clamp(sign * c[i] / kInvSqrt2, -1.0f, 1.0f);
        *dst[k++] = static_cast<int16_t>(std::lround(v * 32767.0f));
    }
    return out;
}

glm::quat unpackQuat(const PackedAnimation::Quat48& p) {
    const float v[3] = { static_cast<float>(p.a) / 32767.0f * kInvSqrt2, static_cast<float>(p.b) / 32767.0f * kInvSqrt2,
                         static_cast<float>(p.c) / 32767.0f * kInvSqrt2 };
    const float rest = std::sqrt(std::max(0.0f, 1.0f - v[0] * v[0] - v[1] * v[1] - v[2] * v[2]));
    float c[4];
    int k = 0;
    for (int i = 0; i < 4; ++i) c[i] = i == p.largest ? rest : v[k++];
    return glm::quat(c[3], c[0], c[1], c[2]);
}

// Which samples of a channel to keep: both ends, and recursively the one furthest from the line between the
// kept neighbours until nothing is further than `tolerance` (Ramer-Douglas-Peucker over time).
template <typename ErrorFn>
std::vector<uint32_t> reduceKeys(size_t count, float tolerance, ErrorFn error) {
    std::vector<uint32_t> keep;
    if (count == 0) return keep;
    keep.push_back(0);
    if (count == 1) return keep;
    std::vector<std::pair<uint32_t, uint32_t>> stack{ { 0u, static_cast<uint32_t>(count - 1) } };
    std::vector<bool> kept(count, false);
    kept[0] = kept[count - 1] = true;
    while (!stack.empty()) {
        const auto [a, b] = stack.back();
        stack.pop_back();
        if (b <= a + 1) continue;
        float worst = 0.0f;
        uint32_t at = 0;
        for (uint32_t i = a + 1; i < b; ++i) {
            const float e = error(a, b, i);
            if (e > worst) {
                worst = e;
                at = i;
            }
        }
        if (worst > tolerance) {
            kept[at] = true;
            stack.emplace_back(a, at);
            stack.emplace_back(at, b);
        }
    }
    keep.clear();
    for (uint32_t i = 0; i < count; ++i)
        if (kept[i]) keep.push_back(i);
    return keep;
}

// The key interval of `frame`: i such that keys[i] <= frame <= keys[i + 1] (clamped), and the blend between them.
template <typename Keys>
void locate(const Keys& frames, uint32_t frame, size_t& i, float& t) {
    if (frames.size() <= 1 || frame <= frames.front()) {
        i = 0;
        t = 0.0f;
        return;
    }
    if (frame >= frames.back()) {
        i = frames.size() - 2;
        t = 1.0f;
        return;
    }
    i = static_cast<size_t>(std::upper_bound(frames.begin(), frames.end(), static_cast<uint16_t>(frame)) - frames.begin()) - 1;
    t = static_cast<float>(frame - frames[i]) / static_cast<float>(frames[i + 1] - frames[i]);
}

} // namespace

size_t PackedAnimation::bytes() const {
    size_t n = sizeof(*this);
    for (const Track& t : tracks)
        n += sizeof(Track) + t.rotFrames.size() * sizeof(uint16_t) + t.rot.size() * sizeof(Quat48) + t.posFrames.size() * sizeof(uint16_t) +
             t.pos.size() * sizeof(Vec3u16) + t.posFull.size() * sizeof(glm::vec3) + t.sclFrames.size() * sizeof(uint16_t) + t.scl.size() * sizeof(glm::vec3);
    return n;
}

glm::mat4 PackedAnimation::local(size_t bone, uint32_t frame) const {
    const Track& t = tracks[bone];
    size_t i = 0;
    float k = 0.0f;
    glm::quat q(1, 0, 0, 0);
    if (!t.rot.empty()) {
        locate(t.rotFrames, frame, i, k);
        q = t.rot.size() == 1 ? unpackQuat(t.rot[0]) : nlerp(unpackQuat(t.rot[i]), unpackQuat(t.rot[i + 1]), k);
    }
    glm::vec3 p(0.0f);
    const size_t keys = t.posFull.empty() ? t.pos.size() : t.posFull.size();
    if (keys) {
        locate(t.posFrames, frame, i, k);
        auto at = [&](size_t n) {
            if (!t.posFull.empty()) return t.posFull[n];
            const Vec3u16& v = t.pos[n];
            return t.posMin + t.posRange * glm::vec3(static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)) / 65535.0f;
        };
        p = keys == 1 ? at(0) : glm::mix(at(i), at(i + 1), k);
    }
    glm::vec3 s(1.0f);
    if (!t.scl.empty()) {
        locate(t.sclFrames, frame, i, k);
        s = t.scl.size() == 1 ? t.scl[0] : glm::mix(t.scl[i], t.scl[i + 1], k);
    }
    glm::mat4 m = glm::mat4_cast(q);
    m[0] *= s.x;
    m[1] *= s.y;
    m[2] *= s.z;
    m[3] = glm::vec4(p, 1.0f);
    return m;
}

PackedAnimation packFrames(const std::vector<std::vector<glm::mat4>>& frames, const AnimCompressOptions& o) {
    PackedAnimation out;
    out.frameCount = static_cast<uint32_t>(frames.size());
    if (frames.empty()) return out;
    const size_t n = frames.size(), bones = frames[0].size();
    out.tracks.resize(bones);
    std::vector<glm::vec3> pos(n), scl(n);
    std::vector<glm::quat> rot(n);
    for (size_t b = 0; b < bones; ++b) {
        for (size_t f = 0; f < n; ++f) {
            const glm::mat4& m = frames[f][b];
            pos[f] = glm::vec3(m[3]);
            glm::vec3 s(glm::length(glm::vec3(m[0])), glm::length(glm::vec3(m[1])), glm::length(glm::vec3(m[2])));
            if (glm::determinant(glm::mat3(m)) < 0.0f) s.x = -s.x; // a mirror: carried in x
            scl[f] = s;
            const glm::vec3 c0 = s.x != 0.0f ? glm::vec3(m[0]) / s.x : glm::vec3(1, 0, 0), c1 = s.y != 0.0f ? glm::vec3(m[1]) / s.y : glm::vec3(0, 1, 0),
                            c2 = s.z != 0.0f ? glm::vec3(m[2]) / s.z : glm::vec3(0, 0, 1);
            rot[f] = glm::normalize(glm::quat_cast(glm::mat3(c0, c1, c2)));
            if (f > 0 && glm::dot(rot[f], rot[f - 1]) < 0.0f) rot[f] = -rot[f]; // the short way round between neighbours
        }
        PackedAnimation::Track& t = out.tracks[b];
        // Rotation. What is quantised is what is measured, so the stored key is what the error is worked out against.
        std::vector<glm::quat> qr(n);
        for (size_t f = 0; f < n; ++f) qr[f] = unpackQuat(packQuat(rot[f]));
        for (uint32_t k : reduceKeys(n, o.rotationError, [&](uint32_t a, uint32_t c, uint32_t i) {
                 return angleBetween(rot[i], nlerp(qr[a], qr[c], static_cast<float>(i - a) / static_cast<float>(c - a)));
             })) {
            t.rotFrames.push_back(static_cast<uint16_t>(k));
            t.rot.push_back(packQuat(rot[k]));
        }
        // Translation.
        const std::vector<uint32_t> pk = reduceKeys(n, o.translationError * 0.5f, [&](uint32_t a, uint32_t c, uint32_t i) {
            return glm::distance(pos[i], glm::mix(pos[a], pos[c], static_cast<float>(i - a) / static_cast<float>(c - a)));
        });
        glm::vec3 lo(1e30f), hi(-1e30f);
        for (uint32_t k : pk) {
            lo = glm::min(lo, pos[k]);
            hi = glm::max(hi, pos[k]);
        }
        t.posMin = lo;
        t.posRange = hi - lo;
        const bool fits = glm::max(glm::max(t.posRange.x, t.posRange.y), t.posRange.z) / 65535.0f <= o.translationError * 0.25f;
        for (uint32_t k : pk) {
            t.posFrames.push_back(static_cast<uint16_t>(k));
            if (fits) {
                auto q = [&](float v, float base, float range) {
                    return static_cast<uint16_t>(range > 0.0f ? std::lround(std::clamp((v - base) / range, 0.0f, 1.0f) * 65535.0f) : 0);
                };
                t.pos.push_back({ q(pos[k].x, lo.x, t.posRange.x), q(pos[k].y, lo.y, t.posRange.y), q(pos[k].z, lo.z, t.posRange.z) });
            } else {
                t.posFull.push_back(pos[k]);
            }
        }
        // Scale.
        for (uint32_t k : reduceKeys(n, o.scaleError, [&](uint32_t a, uint32_t c, uint32_t i) {
                 return glm::distance(scl[i], glm::mix(scl[a], scl[c], static_cast<float>(i - a) / static_cast<float>(c - a)));
             })) {
            t.sclFrames.push_back(static_cast<uint16_t>(k));
            t.scl.push_back(scl[k]);
        }
        // A track that never moves is one key (identity scale and rotation need none at all).
        auto constant = [](auto& keyFrames, auto& keys, auto same) {
            if (keys.size() < 2) return;
            for (size_t i = 1; i < keys.size(); ++i)
                if (!same(keys[i], keys[0])) return;
            keyFrames.resize(1);
            keys.resize(1);
        };
        constant(t.rotFrames, t.rot, [](const auto& a, const auto& b) { return a.a == b.a && a.b == b.b && a.c == b.c && a.largest == b.largest; });
        if (t.posFull.empty()) constant(t.posFrames, t.pos, [](const auto& a, const auto& b) { return a.x == b.x && a.y == b.y && a.z == b.z; });
        else constant(t.posFrames, t.posFull, [](const auto& a, const auto& b) { return a == b; });
        constant(t.sclFrames, t.scl, [](const auto& a, const auto& b) { return a == b; });
    }
    return out;
}

std::vector<std::vector<glm::mat4>> unpackFrames(const PackedAnimation& packed) {
    std::vector<std::vector<glm::mat4>> frames(packed.frameCount, std::vector<glm::mat4>(packed.tracks.size()));
    for (uint32_t f = 0; f < packed.frameCount; ++f)
        for (size_t b = 0; b < packed.tracks.size(); ++b) frames[f][b] = packed.local(b, f);
    return frames;
}

AnimCompressStats compressAnimations(ModelData& model, const AnimCompressOptions& options) {
    AnimCompressStats stats;
    for (ModelAnimation& a : model.animations) {
        if (a.compressed() || a.frames.empty() || a.frames.size() > 65535) continue;
        const size_t dense = a.frames.size() * a.frames[0].size() * sizeof(glm::mat4);
        PackedAnimation packed = packFrames(a.frames, options);
        const size_t bytes = packed.bytes();
        if (bytes >= dense) continue; // no gain: leave it
        ++stats.clips;
        stats.denseBytes += dense;
        stats.packedBytes += bytes;
        a.packed = std::make_shared<const PackedAnimation>(std::move(packed));
        std::vector<std::vector<glm::mat4>>().swap(a.frames);
    }
    return stats;
}

// ---- ModelAnimation, either way

size_t ModelAnimation::frameCount() const { return packed ? packed->frameCount : frames.size(); }

void ModelAnimation::localsAt(size_t frame, std::vector<glm::mat4>& out) const {
    if (packed) {
        out.resize(packed->tracks.size());
        const uint32_t f = static_cast<uint32_t>(std::min(frame, static_cast<size_t>(packed->frameCount) - 1));
        for (size_t b = 0; b < out.size(); ++b) out[b] = packed->local(b, f);
    } else {
        out = frames[std::min(frame, frames.size() - 1)];
    }
}

void ModelAnimation::sampleLocals(float frame, std::vector<glm::mat4>& out) const {
    const size_t count = frameCount();
    if (count == 0) return;
    const size_t f0 = std::min(static_cast<size_t>(frame), count - 1);
    const size_t f1 = std::min(f0 + 1, count - 1);
    const float t = frame - static_cast<float>(f0);
    if (!packed) {
        out.resize(frames[f0].size());
        for (size_t b = 0; b < out.size(); ++b) out[b] = frames[f0][b] * (1.0f - t) + frames[f1][b] * t;
        return;
    }
    out.resize(packed->tracks.size());
    for (size_t b = 0; b < out.size(); ++b)
        out[b] = packed->local(b, static_cast<uint32_t>(f0)) * (1.0f - t) + packed->local(b, static_cast<uint32_t>(f1)) * t;
}

} // namespace kke
