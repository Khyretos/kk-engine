#include "Agility.h"

#include "Scenery.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace pet_companion {

namespace {

constexpr float kBin = 0.1f; // profile step, metres

// Heights where a vertical line at (x, z) meets the model's triangles (model space).
std::vector<float> hitsAt(const kke::ModelData& d, float x, float z) {
    std::vector<float> ys;
    for (const kke::ModelMesh& m : d.meshes) {
        for (size_t i = 0; i + 2 < m.indices.size(); i += 3) {
            const glm::vec3& a = m.vertices[m.indices[i]].position;
            const glm::vec3& b = m.vertices[m.indices[i + 1]].position;
            const glm::vec3& c = m.vertices[m.indices[i + 2]].position;
            // Barycentric in the XZ plane.
            const float den = (b.z - c.z) * (a.x - c.x) + (c.x - b.x) * (a.z - c.z);
            if (std::abs(den) < 1e-9f) continue;
            const float u = ((b.z - c.z) * (x - c.x) + (c.x - b.x) * (z - c.z)) / den;
            const float v = ((c.z - a.z) * (x - c.x) + (a.x - c.x) * (z - c.z)) / den;
            const float w = 1.0f - u - v;
            if (u < 0.0f || v < 0.0f || w < 0.0f) continue;
            ys.push_back(u * a.y + v * b.y + w * c.y);
        }
    }
    std::sort(ys.begin(), ys.end());
    return ys;
}

float smoothstep01(float x) {
    x = std::clamp(x, 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
}

} // namespace

const char* obstacleName(Obstacle::Kind k) {
    switch (k) {
    case Obstacle::Kind::Jump: return "jump";
    case Obstacle::Kind::Tyre: return "tyre";
    case Obstacle::Kind::Weave: return "weave poles";
    case Obstacle::Kind::Ramp: return "A-frame";
    case Obstacle::Kind::SeeSaw: return "seesaw";
    }
    return "obstacle";
}

float Obstacle::speed(float run) const {
    switch (kind) {
    case Kind::Jump:
    case Kind::Tyre: return std::max(3.5f, run * 0.8f);
    case Kind::Weave: return 1.9f;
    case Kind::Ramp:
    case Kind::SeeSaw: return 1.7f;
    }
    return 2.0f;
}

float Obstacle::height(float along) const {
    if (profile.empty()) return 0.0f;
    const float f = (along + length * 0.5f) / kBin;
    if (f <= 0.0f) return profile.front();
    const size_t i = size_t(f);
    if (i + 1 >= profile.size()) return profile.back();
    const float t = f - float(i);
    return profile[i] * (1.0f - t) + profile[i + 1] * t;
}

ObstaclePose traverse(const Obstacle& o, float t, float dogHeight) {
    ObstaclePose p;
    t = std::clamp(t, 0.0f, o.span());
    const float along = t - o.span() * 0.5f; // from the centre
    const glm::vec3 side(o.dir.z, 0.0f, -o.dir.x);
    p.feet = o.centre + o.dir * along;
    p.forward = o.dir;
    switch (o.kind) {
    case Obstacle::Kind::Jump:
    case Obstacle::Kind::Tyre: {
        // Take off a body length before, land one after.
        const float reach = std::max(0.8f, dogHeight * 1.4f);
        const float s = (along + reach) / (2.0f * reach);
        if (s > 0.0f && s < 1.0f) {
            // Feet over the bar, or the body through the middle of the tyre.
            const float top = o.kind == Obstacle::Kind::Jump ? o.clear + 0.1f : std::max(0.1f, o.clear - dogHeight * 0.45f);
            p.feet.y = top * 4.0f * s * (1.0f - s);
            p.act = DogAct::Leap;
            p.phase = s;
            p.pitch = std::atan(top * 4.0f * (1.0f - 2.0f * s) / (2.0f * reach)) * 57.29578f * 0.6f;
        }
        break;
    }
    case Obstacle::Kind::Weave: {
        if (o.poles.size() >= 2) {
            const float first = o.poles.front(), last = o.poles.back();
            const float spacing = (last - first) / float(o.poles.size() - 1);
            // In and out of the line: S curves round each pole, the first one on its right.
            const float amp = 0.3f;
            const float fade = smoothstep01((along - (first - spacing)) / spacing) * smoothstep01(((last + spacing) - along) / spacing);
            const float w = 3.14159265f * (along - first) / spacing + 1.5707963f;
            p.feet += side * (amp * fade * std::sin(w));
            const float dSide = amp * fade * std::cos(w) * 3.14159265f / spacing;
            p.forward = glm::normalize(o.dir + side * dSide);
        }
        break;
    }
    case Obstacle::Kind::Ramp:
    case Obstacle::Kind::SeeSaw: {
        p.feet.y = o.height(along);
        const float slope = (o.height(along + 0.15f) - o.height(along - 0.15f)) / 0.3f;
        p.pitch = std::atan(slope) * 57.29578f;
        break;
    }
    }
    return p;
}

void AgilityCourse::build(command_kit::Scenery& scenery, kke::ModelModule& models) {
    struct Spec {
        Obstacle::Kind kind;
        const char* asset;
        glm::vec3 centre;
        float yaw; // the way across, AiWorld's degrees (0 = +Z, 90 = +X)
    };
    // A loop through the field east of the garden, out of the gate and back.
    const Spec course[] = {
        { Obstacle::Kind::Jump, "SM_Prop_Obstacle_Jump_01", { 15.5f, 0.0f, 0.0f }, 90.0f },
        { Obstacle::Kind::Tyre, "SM_Prop_Obstacle_TubeJump_01", { 20.5f, 0.0f, -4.0f }, 90.0f },
        { Obstacle::Kind::Weave, "SM_Prop_Obstacle_Poles_01", { 26.5f, 0.0f, -7.0f }, 90.0f },
        { Obstacle::Kind::Ramp, "SM_Prop_Obstacle_Ramp_01", { 32.5f, 0.0f, -2.5f }, 0.0f },
        { Obstacle::Kind::Jump, "SM_Prop_Obstacle_Jump_01", { 32.5f, 0.0f, 4.5f }, 0.0f },
        { Obstacle::Kind::SeeSaw, "SM_Prop_Obstacle_SeeSaw_01", { 27.0f, 0.0f, 8.0f }, -90.0f },
        { Obstacle::Kind::Tyre, "SM_Prop_Obstacle_TubeJump_01", { 21.0f, 0.0f, 5.0f }, -90.0f },
        { Obstacle::Kind::Jump, "SM_Prop_Obstacle_Jump_01", { 16.0f, 0.0f, 7.0f }, -90.0f },
    };
    const std::vector<std::string> dogs{ "POLYGON_Dogs" };
    m_obstacles.clear();
    for (const Spec& s : course) {
        Obstacle o;
        o.kind = s.kind;
        o.centre = s.centre;
        o.dir = glm::vec3(std::sin(glm::radians(s.yaw)), 0.0f, std::cos(glm::radians(s.yaw)));
        // The pack's jump, tyre, ramp and seesaw are crossed along their
        // own Z; the weave poles stand in a row along X.
        const bool alongX = s.kind == Obstacle::Kind::Weave;
        const float placeYaw = alongX ? 90.0f - s.yaw : -s.yaw;
        o.model = scenery.place(s.asset, s.centre, placeYaw, 1.0f, true, dogs);
        const kke::ModelData* d = o.model ? models.model(models.instanceModel(o.model)) : nullptr;
        if (d) {
            // Measure it: the line across it (model space) and its length along that line.
            auto at = [&](float along, float lateral) {
                return alongX ? hitsAt(*d, along, lateral) : hitsAt(*d, lateral, along);
            };
            const float lo = alongX ? d->boundsMin.x : d->boundsMin.z, hi = alongX ? d->boundsMax.x : d->boundsMax.z;
            o.length = hi - lo;
            switch (s.kind) {
            case Obstacle::Kind::Jump: {
                const std::vector<float> ys = at(0.0f, 0.0f);
                o.clear = ys.empty() ? 0.6f : ys.back();
                break;
            }
            case Obstacle::Kind::Tyre: {
                // The hole: the widest gap between surfaces up the middle.
                const std::vector<float> ys = at(0.0f, 0.0f);
                float gap = 0.0f;
                o.clear = 0.8f;
                for (size_t i = 0; i + 1 < ys.size(); ++i)
                    if (ys[i] > 0.15f && ys[i + 1] - ys[i] > gap) {
                        gap = ys[i + 1] - ys[i];
                        o.clear = (ys[i] + ys[i + 1]) * 0.5f;
                    }
                break;
            }
            case Obstacle::Kind::Weave: {
                // Poles: where the mesh stands high, in clusters along the row.
                std::vector<float> xs;
                for (const kke::ModelMesh& m : d->meshes)
                    for (const kke::ModelVertex& v : m.vertices)
                        if (v.position.y > 0.4f) xs.push_back(v.position.x);
                std::sort(xs.begin(), xs.end());
                float sum = 0.0f;
                int n = 0;
                for (size_t i = 0; i < xs.size(); ++i) {
                    sum += xs[i];
                    ++n;
                    if (i + 1 == xs.size() || xs[i + 1] - xs[i] > 0.15f) {
                        o.poles.push_back(sum / float(n));
                        sum = 0.0f;
                        n = 0;
                    }
                }
                o.length = std::max(o.length, 1.0f);
                break;
            }
            case Obstacle::Kind::Ramp:
            case Obstacle::Kind::SeeSaw: {
                for (float a = lo; a <= hi + 1e-3f; a += kBin) {
                    const std::vector<float> ys = at(a, 0.0f);
                    o.profile.push_back(ys.empty() ? 0.0f : ys.back());
                }
                break;
            }
            }
            // The model's own centre along the line may not be its origin.
            const float mid = (lo + hi) * 0.5f;
            o.centre += o.dir * mid;
            for (float& pole : o.poles) pole -= mid;
        } else {
            // No pack: jumps (a tyre is a jump too) and poles as blocks; no ramps.
            const glm::vec3 side(o.dir.z, 0.0f, -o.dir.x);
            const glm::vec3 white(0.92f), red(0.85f, 0.2f, 0.2f);
            if (s.kind == Obstacle::Kind::Ramp || s.kind == Obstacle::Kind::SeeSaw) continue;
            if (s.kind == Obstacle::Kind::Weave) {
                for (int i = 0; i < 6; ++i) {
                    const float along = -1.5f + 0.6f * float(i);
                    o.poles.push_back(along);
                    scenery.block(s.centre + o.dir * along + glm::vec3(0, 0.45f, 0), { 0.025f, 0.45f, 0.025f }, i % 2 ? red : white);
                }
                o.length = 3.4f;
            } else {
                o.kind = Obstacle::Kind::Jump;
                o.clear = 0.55f;
                o.length = 0.3f;
                for (float sgn : { -1.0f, 1.0f })
                    scenery.block(s.centre + side * (0.75f * sgn) + glm::vec3(0, 0.5f, 0), { 0.04f, 0.5f, 0.04f }, white);
                scenery.block(s.centre + glm::vec3(0, 0.55f, 0), glm::abs(side) * 0.72f + glm::vec3(0, 0.025f, 0) + glm::abs(o.dir) * 0.025f, red, false);
            }
        }
        m_obstacles.push_back(std::move(o));
    }
    m_start = { 13.0f, 0.0f, -1.0f };
    m_startFacing = { 1.0f, 0.0f, 0.0f };
    reset();
}

void AgilityCourse::reset() {
    m_running = m_finished = false;
    m_next = 0;
    m_faults = 0;
    m_time = 0.0f;
}

void AgilityCourse::update(float dt) {
    if (m_running && !m_finished) m_time += dt;
}

bool AgilityCourse::took(int i) {
    if (m_finished) return false;
    if (i != m_next) {
        ++m_faults;
        return false;
    }
    if (m_next == 0) {
        m_running = true;
        m_time = 0.0f;
        m_faults = 0;
    }
    ++m_next;
    if (m_next >= int(m_obstacles.size())) {
        m_finished = true;
        if (m_best < 0.0f || result() < m_best) m_best = result();
    }
    return true;
}

void AgilityCourse::refused() {
    if (m_running && !m_finished) ++m_faults;
}

int AgilityCourse::near(const glm::vec3& p, float within) const {
    int best = -1;
    float bestD = within;
    for (size_t i = 0; i < m_obstacles.size(); ++i) {
        const glm::vec3 d = m_obstacles[i].centre - p;
        const float dist = std::sqrt(d.x * d.x + d.z * d.z) - m_obstacles[i].length * 0.5f;
        if (dist < bestD) {
            bestD = dist;
            best = int(i);
        }
    }
    return best;
}

} // namespace pet_companion
