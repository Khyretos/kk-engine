#include "kke/AnimCompress.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/matrix_decompose.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

namespace kke {

namespace {

constexpr float kHalfSqrt2 = 0.70710678118f;
constexpr float kDegrees = 57.29577951f;

// The angle between two rotations, from the rotation that takes one to the
// other (acos of a dot product cannot see angles under 0.04 degrees in
// single precision: identical quaternions would "differ").
float rotationErrorDegrees(const glm::quat& a, const glm::quat& b) {
    const glm::quat d = a * glm::conjugate(b);
    return 2.0f * std::atan2(glm::length(glm::vec3(d.x, d.y, d.z)), std::abs(d.w)) * kDegrees;
}

// Keeps the first and last value and every value the curve through the kept
// ones would miss by more than `tol` (Ramer-Douglas-Peucker over time). A
// track that never moves comes back as its first value alone.
template <typename T, typename Err, typename Mix>
std::vector<uint16_t> reduceKeys(const std::vector<T>& v, float tol, Err err, Mix mix) {
    std::vector<uint16_t> keys;
    const size_t n = v.size();
    if (n == 0) return keys;
    std::vector<char> keep(n, 0);
    keep[0] = keep[n - 1] = 1;
    std::vector<std::pair<size_t, size_t>> stack;
    if (n > 2) stack.emplace_back(0, n - 1);
    while (!stack.empty()) {
        const auto [a, b] = stack.back();
        stack.pop_back();
        float worst = 0.0f;
        size_t at = 0;
        for (size_t i = a + 1; i < b; ++i) {
            const float t = static_cast<float>(i - a) / static_cast<float>(b - a);
            const float e = err(v[i], mix(v[a], v[b], t));
            if (e > worst) {
                worst = e;
                at = i;
            }
        }
        if (worst > tol && at != 0) {
            keep[at] = 1;
            if (at - a > 1) stack.emplace_back(a, at);
            if (b - at > 1) stack.emplace_back(at, b);
        }
    }
    for (size_t i = 0; i < n; ++i)
        if (keep[i]) keys.push_back(static_cast<uint16_t>(i));
    if (keys.size() == 2 && err(v[0], v[n - 1]) <= tol) keys.pop_back();
    return keys;
}

uint16_t quantize(float value, float lo, float step) {
    if (step <= 0.0f) return 0;
    return static_cast<uint16_t>(std::clamp(std::lround((value - lo) / step), 0L, 65535L));
}

// Little-endian bytes in and out, with bounds on the way in.
struct Writer {
    std::vector<uint8_t> out;
    template <typename T>
    void put(T v) {
        const auto* p = reinterpret_cast<const uint8_t*>(&v);
        out.insert(out.end(), p, p + sizeof v);
    }
};
struct Reader {
    const std::vector<uint8_t>& in;
    size_t pos = 0;
    bool bad = false;
    template <typename T>
    T get() {
        T v{};
        if (bad || in.size() - pos < sizeof v) {
            bad = true;
            return v;
        }
        std::memcpy(&v, in.data() + pos, sizeof v);
        pos += sizeof v;
        return v;
    }
    bool has(size_t bytes) const { return !bad && in.size() - pos >= bytes; }
};

} // namespace

AnimCompressionStats& AnimCompressionStats::operator+=(const AnimCompressionStats& o) {
    rawBytes += o.rawBytes;
    packedBytes += o.packedBytes;
    rawKeys += o.rawKeys;
    keys += o.keys;
    maxPositionError = std::max(maxPositionError, o.maxPositionError);
    maxRotationErrorDegrees = std::max(maxRotationErrorDegrees, o.maxRotationErrorDegrees);
    maxScaleError = std::max(maxScaleError, o.maxScaleError);
    return *this;
}

// ---- evaluating -------------------------------------------------------------------

glm::vec3 PackedClip::vec3At(const Channel& c, size_t k) const {
    if (m_quantized)
        return { c.lo[0] + static_cast<float>(c.quant[3 * k]) * c.step[0], c.lo[1] + static_cast<float>(c.quant[3 * k + 1]) * c.step[1],
                 c.lo[2] + static_cast<float>(c.quant[3 * k + 2]) * c.step[2] };
    return { c.raw[3 * k], c.raw[3 * k + 1], c.raw[3 * k + 2] };
}

glm::quat PackedClip::quatAt(const Channel& c, size_t k) const {
    float v[4];
    if (m_quantized) {
        const int drop = c.largest[k] & 3;
        float sum = 0.0f;
        int next = 0;
        for (int i = 0; i < 4; ++i) {
            if (i == drop) continue;
            const float x = static_cast<float>(c.quant[3 * k + static_cast<size_t>(next++)]) / 65535.0f * 2.0f * kHalfSqrt2 - kHalfSqrt2;
            v[i] = x;
            sum += x * x;
        }
        v[drop] = std::sqrt(std::max(0.0f, 1.0f - sum));
    } else {
        for (int i = 0; i < 4; ++i) v[i] = c.raw[4 * k + static_cast<size_t>(i)];
    }
    return glm::quat(v[3], v[0], v[1], v[2]); // glm::quat(w, x, y, z)
}

template <typename T, typename Get, typename Mix>
T PackedClip::evaluate(const Channel& c, size_t f, Get get, Mix mix) const {
    const std::vector<uint16_t>& fr = c.frames;
    const size_t n = fr.size();
    if (n == 1 || f >= fr.back()) return get(c, n - 1);
    const size_t k1 = static_cast<size_t>(std::upper_bound(fr.begin(), fr.end(), static_cast<uint16_t>(f)) - fr.begin());
    const size_t k0 = k1 - 1; // fr[0] is 0, so k1 >= 1
    if (fr[k0] == f) return get(c, k0);
    const float t = static_cast<float>(f - fr[k0]) / static_cast<float>(fr[k1] - fr[k0]);
    return mix(get(c, k0), get(c, k1), t);
}

void PackedClip::frame(size_t f, Pose& out) const {
    if (m_frames == 0) {
        out.clear();
        return;
    }
    f = std::min(f, m_frames - 1);
    out.resize(m_bones.size());
    auto getV = [this](const Channel& c, size_t k) { return vec3At(c, k); };
    auto getQ = [this](const Channel& c, size_t k) { return quatAt(c, k); };
    auto mixV = [](const glm::vec3& a, const glm::vec3& b, float t) { return glm::mix(a, b, t); };
    auto mixQ = [](const glm::quat& a, const glm::quat& b, float t) { return glm::slerp(a, b, t); };
    for (size_t b = 0; b < m_bones.size(); ++b) {
        out[b].t = evaluate<glm::vec3>(m_bones[b].pos, f, getV, mixV);
        out[b].r = evaluate<glm::quat>(m_bones[b].rot, f, getQ, mixQ);
        out[b].s = evaluate<glm::vec3>(m_bones[b].scl, f, getV, mixV);
    }
}

void PackedClip::sample(float time, bool loop, Pose& out) const {
    if (m_frames == 0) {
        out.clear();
        return;
    }
    float t = time;
    if (duration <= 0.0f) {
        t = 0.0f;
    } else if (loop) {
        t = std::fmod(t, duration);
        if (t < 0.0f) t += duration;
    } else {
        t = std::clamp(t, 0.0f, duration);
    }
    const float f = t * (sampleRate > 0.0f ? sampleRate : 30.0f);
    const size_t last = m_frames - 1;
    const size_t f0 = std::min(static_cast<size_t>(std::max(f, 0.0f)), last);
    size_t f1 = f0 + 1;
    if (f1 > last) f1 = loop ? 0 : last;
    thread_local Pose a, b;
    frame(f0, a);
    frame(f1, b);
    blendPoses(a, b, f - static_cast<float>(f0), out);
}

std::vector<Pose> PackedClip::unpack() const {
    std::vector<Pose> frames(m_frames);
    for (size_t f = 0; f < m_frames; ++f) frame(f, frames[f]);
    return frames;
}

size_t PackedClip::keyCount() const {
    size_t n = 0;
    for (const Track& t : m_bones) n += t.pos.keys() + t.rot.keys() + t.scl.keys();
    return n;
}

size_t PackedClip::byteSize() const {
    size_t bytes = 30 + std::min<size_t>(name.size(), 65535);
    auto vec = [&](const Channel& c) { return 4 + 2 * c.keys() + (m_quantized ? 24 + 6 * c.keys() : 12 * c.keys()); };
    auto rot = [&](const Channel& c) { return 4 + 2 * c.keys() + (m_quantized ? 7 * c.keys() : 16 * c.keys()); };
    for (const Track& t : m_bones) bytes += vec(t.pos) + rot(t.rot) + vec(t.scl);
    return bytes;
}

// ---- packing ----------------------------------------------------------------------

bool packFrames(const std::vector<Pose>& frames, const std::string& name, float duration, float sampleRate, const AnimCompressionOptions& options,
                PackedClip& out, AnimCompressionStats* stats, std::string* error) {
    auto fail = [&](const std::string& why) {
        if (error) *error = why;
        return false;
    };
    const size_t n = frames.size();
    if (n == 0) return fail("the clip has no frames");
    if (n > PackedClip::kMaxFrames) return fail("the clip has " + std::to_string(n) + " frames (at most " + std::to_string(PackedClip::kMaxFrames) + ")");
    const size_t bones = frames[0].size();
    if (bones == 0) return fail("the clip has no bones");

    PackedClip clip;
    clip.name = name;
    clip.duration = duration;
    clip.sampleRate = sampleRate;
    clip.m_frames = n;
    clip.m_quantized = options.quantize;
    clip.m_bones.resize(bones);

    auto errV = [](const glm::vec3& a, const glm::vec3& b) { return glm::length(a - b); };
    auto mixV = [](const glm::vec3& a, const glm::vec3& b, float t) { return glm::mix(a, b, t); };
    auto errQ = [](const glm::quat& a, const glm::quat& b) { return rotationErrorDegrees(a, b); };
    auto mixQ = [](const glm::quat& a, const glm::quat& b, float t) { return glm::slerp(a, b, t); };

    auto storeVec3 = [&](PackedClip::Channel& ch, const std::vector<glm::vec3>& values, const std::vector<uint16_t>& keys) {
        ch.frames = keys;
        if (!options.quantize) {
            for (uint16_t k : keys) {
                ch.raw.push_back(values[k].x);
                ch.raw.push_back(values[k].y);
                ch.raw.push_back(values[k].z);
            }
            return;
        }
        glm::vec3 lo(1e30f), hi(-1e30f);
        for (uint16_t k : keys) {
            lo = glm::min(lo, values[k]);
            hi = glm::max(hi, values[k]);
        }
        for (int a = 0; a < 3; ++a) {
            ch.lo[a] = lo[a];
            ch.step[a] = (hi[a] - lo[a]) / 65535.0f;
        }
        for (uint16_t k : keys)
            for (int a = 0; a < 3; ++a) ch.quant.push_back(quantize(values[k][a], ch.lo[a], ch.step[a]));
    };
    auto storeQuat = [&](PackedClip::Channel& ch, const std::vector<glm::quat>& values, const std::vector<uint16_t>& keys) {
        ch.frames = keys;
        for (uint16_t k : keys) {
            const glm::quat q = glm::normalize(values[k]);
            const float c[4] = { q.x, q.y, q.z, q.w };
            if (!options.quantize) {
                for (float x : c) ch.raw.push_back(x);
                continue;
            }
            int big = 0;
            for (int i = 1; i < 4; ++i)
                if (std::abs(c[i]) > std::abs(c[big])) big = i;
            const float sign = c[big] < 0.0f ? -1.0f : 1.0f; // q and -q are the same rotation: keep the big one positive
            ch.largest.push_back(static_cast<uint8_t>(big));
            for (int i = 0; i < 4; ++i) {
                if (i == big) continue;
                const float x = std::clamp(c[i] * sign, -kHalfSqrt2, kHalfSqrt2);
                ch.quant.push_back(static_cast<uint16_t>(std::lround((x + kHalfSqrt2) / (2.0f * kHalfSqrt2) * 65535.0f)));
            }
        }
    };

    std::vector<glm::vec3> P(n), S(n);
    std::vector<glm::quat> R(n);
    for (size_t b = 0; b < bones; ++b) {
        for (size_t f = 0; f < n; ++f) {
            const BoneTRS tr = b < frames[f].size() ? frames[f][b] : BoneTRS{};
            P[f] = tr.t;
            S[f] = tr.s;
            R[f] = glm::normalize(tr.r);
            if (f > 0 && glm::dot(R[f - 1], R[f]) < 0.0f) R[f] = -R[f]; // the short way round, so slerp between keys is the same curve
        }
        storeVec3(clip.m_bones[b].pos, P, reduceKeys(P, options.positionTolerance, errV, mixV));
        storeQuat(clip.m_bones[b].rot, R, reduceKeys(R, options.rotationToleranceDegrees, errQ, mixQ));
        storeVec3(clip.m_bones[b].scl, S, reduceKeys(S, options.scaleTolerance, errV, mixV));
    }

    if (stats) {
        AnimCompressionStats s;
        s.rawBytes = n * bones * sizeof(glm::mat4);
        s.packedBytes = clip.byteSize();
        s.rawKeys = n * bones * 3;
        s.keys = clip.keyCount();
        Pose got;
        for (size_t f = 0; f < n; ++f) {
            clip.frame(f, got);
            for (size_t b = 0; b < bones; ++b) {
                const BoneTRS orig = b < frames[f].size() ? frames[f][b] : BoneTRS{};
                s.maxPositionError = std::max(s.maxPositionError, glm::length(got[b].t - orig.t));
                s.maxScaleError = std::max(s.maxScaleError, glm::length(got[b].s - orig.s));
                s.maxRotationErrorDegrees = std::max(s.maxRotationErrorDegrees, rotationErrorDegrees(got[b].r, glm::normalize(orig.r)));
            }
        }
        *stats = s;
    }
    out = std::move(clip);
    return true;
}

bool packAnimation(const ModelAnimation& clip, const AnimCompressionOptions& options, PackedClip& out, AnimCompressionStats* stats, std::string* error) {
    std::vector<Pose> frames(clip.frames.size());
    for (size_t f = 0; f < clip.frames.size(); ++f) {
        frames[f].resize(clip.frames[f].size());
        for (size_t b = 0; b < clip.frames[f].size(); ++b) {
            BoneTRS tr;
            glm::vec3 skew;
            glm::vec4 persp;
            if (!glm::decompose(clip.frames[f][b], tr.s, tr.r, tr.t, skew, persp)) tr = BoneTRS{};
            tr.r = glm::normalize(tr.r);
            frames[f][b] = tr;
        }
    }
    return packFrames(frames, clip.name, clip.duration, clip.sampleRate > 0.0f ? clip.sampleRate : 30.0f, options, out, stats, error);
}

// ---- the file -----------------------------------------------------------------------

std::vector<uint8_t> PackedClip::serialize() const {
    Writer w;
    w.out.reserve(byteSize());
    for (char c : { 'K', 'K', 'A', 'C' }) w.put<uint8_t>(static_cast<uint8_t>(c));
    w.put<uint32_t>(1);
    w.put<uint32_t>(m_quantized ? 1u : 0u);
    w.put<float>(duration);
    w.put<float>(sampleRate);
    w.put<uint32_t>(static_cast<uint32_t>(m_frames));
    w.put<uint32_t>(static_cast<uint32_t>(m_bones.size()));
    const size_t nameLen = std::min<size_t>(name.size(), 65535);
    w.put<uint16_t>(static_cast<uint16_t>(nameLen));
    w.out.insert(w.out.end(), name.begin(), name.begin() + static_cast<std::ptrdiff_t>(nameLen));
    for (const Track& t : m_bones) {
        for (const Channel* c : { &t.pos, &t.rot, &t.scl }) {
            const bool rotation = c == &t.rot;
            w.put<uint32_t>(static_cast<uint32_t>(c->keys()));
            for (uint16_t f : c->frames) w.put<uint16_t>(f);
            if (m_quantized) {
                if (!rotation) {
                    for (float x : c->lo) w.put<float>(x);
                    for (float x : c->step) w.put<float>(x);
                }
                for (uint16_t q : c->quant) w.put<uint16_t>(q);
                if (rotation)
                    for (uint8_t l : c->largest) w.put<uint8_t>(l);
            } else {
                for (float x : c->raw) w.put<float>(x);
            }
        }
    }
    return std::move(w.out);
}

bool PackedClip::deserialize(const std::vector<uint8_t>& bytes, PackedClip& out, std::string* error) {
    auto fail = [&](const std::string& why) {
        if (error) *error = "not a packed animation: " + why;
        return false;
    };
    Reader r{ bytes };
    const char magic[4] = { static_cast<char>(r.get<uint8_t>()), static_cast<char>(r.get<uint8_t>()), static_cast<char>(r.get<uint8_t>()),
                            static_cast<char>(r.get<uint8_t>()) };
    if (r.bad || std::memcmp(magic, "KKAC", 4) != 0) return fail("wrong header");
    if (r.get<uint32_t>() != 1) return fail("unknown version");
    const uint32_t flags = r.get<uint32_t>();
    PackedClip clip;
    clip.duration = r.get<float>();
    clip.sampleRate = r.get<float>();
    const uint32_t frames = r.get<uint32_t>();
    const uint32_t bones = r.get<uint32_t>();
    const uint16_t nameLen = r.get<uint16_t>();
    if (r.bad) return fail("cut short");
    if (!std::isfinite(clip.duration) || !std::isfinite(clip.sampleRate) || clip.duration < 0.0f || clip.sampleRate <= 0.0f) return fail("bad time values");
    if (frames == 0 || frames > kMaxFrames) return fail("bad frame count");
    if (bones == 0 || bones > 4096) return fail("bad bone count");
    if (!r.has(nameLen)) return fail("cut short");
    clip.name.assign(reinterpret_cast<const char*>(bytes.data() + r.pos), nameLen);
    r.pos += nameLen;
    clip.m_frames = frames;
    clip.m_quantized = (flags & 1u) != 0;
    clip.m_bones.resize(bones);
    for (Track& t : clip.m_bones) {
        for (Channel* c : { &t.pos, &t.rot, &t.scl }) {
            const bool rotation = c == &t.rot;
            const uint32_t keys = r.get<uint32_t>();
            if (r.bad || keys == 0 || keys > frames) return fail("bad key count");
            if (!r.has(static_cast<size_t>(keys) * 2)) return fail("cut short");
            c->frames.resize(keys);
            for (uint16_t& f : c->frames) f = r.get<uint16_t>();
            if (c->frames.front() != 0 || c->frames.back() >= frames) return fail("key frames out of range");
            for (size_t i = 1; i < keys; ++i)
                if (c->frames[i] <= c->frames[i - 1]) return fail("key frames out of order");
            if (clip.m_quantized) {
                if (!rotation) {
                    for (float& x : c->lo) x = r.get<float>();
                    for (float& x : c->step) x = r.get<float>();
                    for (int a = 0; a < 3; ++a)
                        if (r.bad || !std::isfinite(c->lo[a]) || !std::isfinite(c->step[a])) return fail("bad range");
                }
                if (!r.has(static_cast<size_t>(keys) * (6 + (rotation ? 1 : 0)))) return fail("cut short");
                c->quant.resize(static_cast<size_t>(keys) * 3);
                for (uint16_t& q : c->quant) q = r.get<uint16_t>();
                if (rotation) {
                    c->largest.resize(keys);
                    for (uint8_t& l : c->largest) {
                        l = r.get<uint8_t>();
                        if (l > 3) return fail("bad rotation");
                    }
                }
            } else {
                const size_t floats = static_cast<size_t>(keys) * (rotation ? 4 : 3);
                if (!r.has(floats * 4)) return fail("cut short");
                c->raw.resize(floats);
                for (float& x : c->raw) {
                    x = r.get<float>();
                    if (!std::isfinite(x)) return fail("bad number");
                }
            }
        }
    }
    if (r.bad) return fail("cut short");
    if (r.pos != bytes.size()) return fail("extra bytes at the end");
    out = std::move(clip);
    return true;
}

} // namespace kke
