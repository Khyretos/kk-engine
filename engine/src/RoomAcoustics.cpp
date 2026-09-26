#include "kke/RoomAcoustics.h"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>

namespace kke {

std::vector<glm::vec3> fibonacciSphere(int count) {
    std::vector<glm::vec3> dirs;
    count = std::max(1, count);
    dirs.reserve(size_t(count));
    const float golden = glm::pi<float>() * (3.0f - std::sqrt(5.0f));
    for (int i = 0; i < count; ++i) {
        const float y = 1.0f - 2.0f * (float(i) + 0.5f) / float(count);
        const float r = std::sqrt(std::max(0.0f, 1.0f - y * y));
        const float a = golden * float(i);
        dirs.emplace_back(std::cos(a) * r, y, std::sin(a) * r);
    }
    return dirs;
}

namespace {
glm::vec3 turnY(const glm::vec3& d, float c, float s) { return {d.x * c + d.z * s, d.y, -d.x * s + d.z * c}; }
bool isFloor(const AcousticRay& r) { return r.normal.y >= 0.7f; }
constexpr float kSpeedOfSound = 343.0f;
} // namespace

RoomAcoustics probeRoom(const glm::vec3& at, const AcousticRayFn& ray, const AudioMaterialTable& materials, const RoomProbeSettings& s) {
    RoomAcoustics room;
    if (!ray) return room;
    const float rc = std::cos(s.rotation), rs = std::sin(s.rotation);

    // The sphere: how big, how closed overhead, how absorbent.
    int hits = 0, up = 0, upHits = 0, ceilingHits = 0;
    float distSum = 0.0f, absorbSum = 0.0f, hitAbsorbSum = 0.0f, upDistSum = 0.0f;
    float bestUp = -1.0f;
    for (const glm::vec3& d0 : fibonacciSphere(s.rays)) {
        const glm::vec3 d = turnY(d0, rc, rs);
        const AcousticRay r = ray(at, d, s.maxDistance);
        const bool hit = r.hit && r.distance < s.maxDistance;
        if (hit) {
            ++hits;
            distSum += r.distance;
            const float a = materials.get(r.material).absorption;
            absorbSum += a;
            hitAbsorbSum += a;
        } else {
            absorbSum += 1.0f; // sound that leaves never comes back
        }
        if (d.y >= 0.35f) {
            ++up;
            if (hit) ++upHits;
            if (hit && r.normal.y <= -0.7f) { // a ceiling, not the top of a wall
                ++ceilingHits;
                upDistSum += r.distance * d.y; // height above the listener
                if (d.y > bestUp) {
                    bestUp = d.y;
                    room.ceilingMaterial = r.material;
                }
            }
        }
    }

    // The level ring: walls and ways out. A level ray at ear height never
    // ends on a flat floor, so a big hall's floor can't pass for an opening.
    const int n = std::max(3, s.ringRays);
    room.ring.resize(size_t(n));
    for (int i = 0; i < n; ++i) {
        const float a = glm::two_pi<float>() * float(i) / float(n);
        RoomSample& smp = room.ring[size_t(i)];
        smp.dir = turnY(glm::vec3(std::sin(a), 0.0f, std::cos(a)), rc, rs);
        smp.ray = ray(at, smp.dir, s.maxDistance);
        if (smp.ray.hit && smp.ray.distance >= s.maxDistance) smp.ray.hit = false;
        if (smp.ray.hit) {
            // Normals face the listener (a ray reports front or back faces).
            if (glm::dot(smp.ray.normal, smp.dir) > 0.0f) smp.ray.normal = -smp.ray.normal;
        }
    }
    int wallHits = 0;
    glm::vec3 openSum(0.0f);
    for (int i = 0; i < n; ++i) {
        const RoomSample& me = room.ring[size_t(i)];
        // Where the wall of each neighbour that hit a wall crosses this ray.
        float through = 0.0f;
        bool behindNeighbour = false;
        for (int k : {i - 1, i + 1}) {
            const RoomSample& nb = room.ring[size_t((k + n) % n)];
            if (!nb.ray.hit || isFloor(nb.ray)) continue;
            const glm::vec3 np = at + nb.dir * nb.ray.distance;
            const float facing = glm::dot(me.dir, nb.ray.normal);
            if (facing >= -1e-3f) continue; // parallel to that wall
            const float cross = glm::dot(np - at, nb.ray.normal) / facing;
            if (cross <= 0.0f) continue;
            through = through > 0.0f ? std::min(through, cross) : cross;
            if (me.ray.hit && me.ray.distance > cross + s.openingDepth) behindNeighbour = true;
        }
        const bool escaped = !me.ray.hit;
        if (escaped || behindNeighbour) {
            RoomOpening o;
            o.dir = me.dir;
            o.through = through;
            o.reach = escaped ? s.maxDistance : me.ray.distance;
            room.openings.push_back(o);
            openSum += me.dir;
        } else if (!isFloor(me.ray)) {
            ++wallHits;
        }
    }

    const float count = float(s.rays);
    room.enclosure = float(hits) / count;
    room.walls = float(wallHits) / float(n);
    room.ceiling = up ? float(upHits) / float(up) : 0.0f;
    room.ceilingHeight = ceilingHits ? upDistSum / float(ceilingHits) : 0.0f;
    room.openness = float(room.openings.size()) / float(n);
    room.meanDistance = hits ? distSum / float(hits) : s.maxDistance;
    room.absorption = std::max(s.minAbsorption, absorbSum / count);
    room.surfaceAbsorption = hits ? hitAbsorbSum / float(hits) : 1.0f;
    // Sabine's RT60 = 0.161 V / (S a); for a room of "radius" r (V/S = r/3)
    // that is 0.0537 r / a.
    room.rt60 = std::clamp(0.0537f * room.meanDistance / room.absorption, 0.05f, 4.0f);
    // Reverb needs something to bounce between: walls matter, a roof matters
    // more (an alley rings a little, a hall a lot, a field not at all), and
    // soft surfaces send little back (a padded room is dry even closed).
    const float reflective = std::clamp(1.0f - 1.4f * room.surfaceAbsorption, 0.15f, 1.0f);
    room.wet = std::clamp((0.4f * room.ceiling * (0.4f + 0.6f * room.walls) + 0.1f * room.walls * room.walls) * reflective, 0.0f, 0.5f);
    room.damping = std::clamp(0.15f + (absorbSum / count) * 0.8f, 0.1f, 0.8f);
    room.preDelay = std::min(0.08f, room.meanDistance / kSpeedOfSound);
    if (glm::length(openSum) > 1e-4f) room.openingDir = glm::normalize(openSum);
    return room;
}

// ------------------------------------------------------------ RoomTracker
RoomTracker::RoomTracker(const Settings& s) : settings(s) { reset(); }

void RoomTracker::reset() {
    m_room = RoomAcoustics{};
    m_have = false;
    m_bins.assign(size_t(std::max(4, settings.bins)), Bin{});
    m_openings.clear();
}

void RoomTracker::update(const glm::vec3& listener, const RoomAcoustics& probe, const AudioMaterialTable& materials) {
    (void)materials;
    if (m_have && glm::length(listener - m_at) > settings.snapDistance) reset();
    if (m_bins.size() != size_t(std::max(4, settings.bins))) m_bins.assign(size_t(std::max(4, settings.bins)), Bin{});
    const float k = m_have ? std::clamp(settings.blend, 0.0f, 1.0f) : 1.0f;
    auto mix = [k](float& a, float b) { a += (b - a) * k; };
    mix(m_room.enclosure, probe.enclosure);
    mix(m_room.walls, probe.walls);
    mix(m_room.ceiling, probe.ceiling);
    mix(m_room.meanDistance, probe.meanDistance);
    mix(m_room.absorption, probe.absorption);
    mix(m_room.surfaceAbsorption, probe.surfaceAbsorption);
    mix(m_room.rt60, probe.rt60);
    mix(m_room.wet, probe.wet);
    mix(m_room.damping, probe.damping);
    mix(m_room.preDelay, probe.preDelay);
    mix(m_room.openness, probe.openness);
    if (probe.ceilingHeight > 0.0f) {
        if (m_room.ceilingHeight <= 0.0f) m_room.ceilingHeight = probe.ceilingHeight;
        else mix(m_room.ceilingHeight, probe.ceilingHeight);
        m_room.ceilingMaterial = probe.ceilingMaterial;
    } else if (m_room.ceiling < 0.2f) {
        m_room.ceilingHeight = 0.0f;
    }
    m_room.ring = probe.ring;
    m_at = listener;
    m_have = true;

    // Openings: this probe's, plus the last few probes' (kept until they
    // age out, or a ray now finds a wall close in that direction).
    for (Remembered& r : m_openings) ++r.age;
    for (const RoomOpening& o : probe.openings) {
        // The same way seen again replaces the old sighting.
        m_openings.erase(std::remove_if(m_openings.begin(), m_openings.end(),
                                        [&](const Remembered& r) { return r.age > 0 && glm::dot(r.opening.dir, o.dir) > 0.99f; }),
                         m_openings.end());
        m_openings.push_back({o, 0});
    }
    m_openings.erase(std::remove_if(m_openings.begin(), m_openings.end(),
                                    [&](const Remembered& r) {
                                        if (r.age >= settings.openingMemory) return true;
                                        if (r.age == 0) return false;
                                        for (const RoomSample& smp : probe.ring)
                                            if (glm::dot(smp.dir, r.opening.dir) > 0.995f && smp.ray.hit &&
                                                smp.ray.distance < std::max(r.opening.through, 0.5f))
                                                return true; // a wall there now: moved, or a door shut
                                        return false;
                                    }),
                     m_openings.end());
    m_room.openings.clear();
    glm::vec3 sum(0.0f);
    for (const Remembered& r : m_openings) {
        m_room.openings.push_back(r.opening);
        sum += r.opening.dir;
    }
    m_room.openingDir = glm::length(sum) > 1e-4f ? glm::normalize(sum) : glm::vec3(0.0f);

    // Echo bins: the wall distance in each direction, steadied.
    const int nb = int(m_bins.size());
    for (Bin& b : m_bins) ++b.age;
    for (const RoomSample& smp : probe.ring) {
        float a = std::atan2(smp.dir.x, smp.dir.z);
        if (a < 0.0f) a += glm::two_pi<float>();
        Bin& b = m_bins[size_t(int(a / glm::two_pi<float>() * float(nb)) % nb)];
        const bool wall = smp.ray.hit && !isFloor(smp.ray);
        if (wall && b.hit && b.age < settings.openingMemory) b.distance += (smp.ray.distance - b.distance) * 0.5f;
        else b.distance = wall ? smp.ray.distance : 0.0f;
        b.hit = wall;
        b.material = smp.ray.material;
        b.age = 0;
    }
}

std::vector<EchoTap> RoomTracker::echoes(int maxTaps, const AudioMaterialTable& materials) const {
    std::vector<EchoTap> taps;
    if (maxTaps <= 0 || !m_have) return taps;
    const int nb = int(m_bins.size());
    // There and back: the extra path is twice the distance; the echo is
    // as much quieter as that path is longer than the direct sound's
    // (taken as ~2 m: sounds you hear echo are usually near you), less
    // what the wall soaks up. Walls within a metre fold into the direct
    // sound (no audible delay) and are left out.
    auto tapFor = [&](const glm::vec3& dir, float distance, uint32_t material) {
        EchoTap t;
        t.dir = dir;
        t.delay = 2.0f * distance / kSpeedOfSound;
        t.gain = (1.0f - materials.get(material).absorption) * 2.0f / (2.0f + 2.0f * distance);
        return t;
    };
    std::vector<EchoTap> all;
    for (int i = 0; i < nb; ++i) {
        const Bin& b = m_bins[size_t(i)];
        if (!b.hit || b.age >= settings.openingMemory || b.distance < 1.0f) continue;
        const float a = (float(i) + 0.5f) / float(nb) * glm::two_pi<float>();
        all.push_back(tapFor(glm::vec3(std::sin(a), 0.0f, std::cos(a)), b.distance, b.material));
    }
    // One echo per wall: neighbours arriving within 4 ms are the same
    // surface; keep the strongest, pointing at their average direction.
    std::sort(all.begin(), all.end(), [](const EchoTap& x, const EchoTap& y) { return x.gain > y.gain; });
    for (const EchoTap& t : all) {
        bool merged = false;
        for (EchoTap& kept : taps)
            if (std::fabs(kept.delay - t.delay) < 0.004f && glm::dot(kept.dir, t.dir) > 0.3f) {
                kept.dir = glm::normalize(kept.dir + t.dir * (t.gain / std::max(kept.gain, 1e-6f)));
                merged = true;
                break;
            }
        if (!merged) taps.push_back(t);
    }
    if (m_room.ceilingHeight >= 1.0f && m_room.ceiling > 0.5f)
        taps.push_back(tapFor(glm::vec3(0.0f, 1.0f, 0.0f), m_room.ceilingHeight, m_room.ceilingMaterial));
    std::sort(taps.begin(), taps.end(), [](const EchoTap& x, const EchoTap& y) { return x.gain > y.gain; });
    if (int(taps.size()) > maxTaps) taps.resize(size_t(maxTaps));
    return taps;
}

// ------------------------------------------------------------------ Reverb
namespace {
// Freeverb's tunings, in samples at 44.1 kHz; scaled to the actual rate.
const int kCombTuning[8] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
const int kAllPassTuning[4] = {556, 441, 341, 225};
const int kStereoSpread = 23;
constexpr float kFixedGain = 0.015f;
constexpr float kAllPassFeedback = 0.5f;
} // namespace

Reverb::Reverb(int sampleRate) : m_rate(std::max(8000, sampleRate)) {
    const float scale = float(m_rate) / 44100.0f;
    for (int ch = 0; ch < 2; ++ch) {
        for (int i = 0; i < 8; ++i) m_comb[ch][i].buf.assign(size_t(float(kCombTuning[i] + ch * kStereoSpread) * scale), 0.0f);
        for (int i = 0; i < 4; ++i) m_allpass[ch][i].buf.assign(size_t(float(kAllPassTuning[i] + ch * kStereoSpread) * scale), 0.0f);
    }
    m_pre.assign(size_t(0.1f * float(m_rate)) + 1, 0.0f);
    updateFeedback();
}

void Reverb::clear() {
    for (auto& ch : m_comb)
        for (Comb& c : ch) {
            std::fill(c.buf.begin(), c.buf.end(), 0.0f);
            c.store = 0.0f;
        }
    for (auto& ch : m_allpass)
        for (AllPass& a : ch) std::fill(a.buf.begin(), a.buf.end(), 0.0f);
    std::fill(m_pre.begin(), m_pre.end(), 0.0f);
}

void Reverb::setRoom(float rt60, float damping, float wet, float preDelay) {
    m_targetRt60 = std::clamp(rt60, 0.05f, 8.0f);
    m_targetDamp = std::clamp(damping, 0.0f, 0.95f);
    m_targetWet = std::clamp(wet, 0.0f, 1.0f);
    m_targetPre = std::clamp(preDelay, 0.0f, 0.099f);
}

void Reverb::updateFeedback() {
    for (auto& ch : m_comb)
        for (Comb& c : ch) {
            // A comb of delay d seconds loses 20 log10(g) dB per pass:
            // 60 dB after RT60 means g = 10^(-3 d / RT60).
            const float d = float(c.buf.size()) / float(m_rate);
            c.feedback = std::min(0.985f, std::pow(10.0f, -3.0f * d / m_rt60));
        }
}

void Reverb::process(const float* in, float* out, int frames) {
    // Glide to the targets once per block (a block is ~10 ms).
    const float k = 0.3f;
    m_rt60 += (m_targetRt60 - m_rt60) * k;
    m_damp += (m_targetDamp - m_damp) * k;
    const float wetFrom = m_wet;
    m_wet += (m_targetWet - m_wet) * k;
    m_preDelay += (m_targetPre - m_preDelay) * k;
    updateFeedback();
    if (wetFrom < 1e-5f && m_wet < 1e-5f) return; // dry room: nothing to add (the tail has died with the wet level)
    const size_t preN = m_pre.size();
    const size_t delay = std::min(preN - 1, size_t(m_preDelay * float(m_rate)));
    const float damp1 = m_damp, damp2 = 1.0f - m_damp;
    for (int f = 0; f < frames; ++f) {
        m_pre[m_preIdx] = in[f];
        const float x = m_pre[(m_preIdx + preN - delay) % preN] * kFixedGain;
        m_preIdx = (m_preIdx + 1) % preN;
        const float wet = wetFrom + (m_wet - wetFrom) * (float(f) / float(frames));
        for (int ch = 0; ch < 2; ++ch) {
            float acc = 0.0f;
            for (Comb& c : m_comb[ch]) {
                const float y = c.buf[c.idx];
                c.store = y * damp2 + c.store * damp1;
                c.buf[c.idx] = x + c.store * c.feedback;
                if (++c.idx >= c.buf.size()) c.idx = 0;
                acc += y;
            }
            for (AllPass& a : m_allpass[ch]) {
                const float b = a.buf[a.idx];
                const float y = -acc + b;
                a.buf[a.idx] = acc + b * kAllPassFeedback;
                if (++a.idx >= a.buf.size()) a.idx = 0;
                acc = y;
            }
            out[2 * f + ch] += acc * wet;
        }
    }
}

} // namespace kke
