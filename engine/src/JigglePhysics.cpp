#include "kke/JigglePhysics.h"

#include "kke/AnimRig.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <tuple>

namespace kke {

namespace {

const glm::vec3 kGravity(0.0f, -9.81f, 0.0f);

// Shortest rotation taking direction a to direction b (both non-zero).
glm::quat rotationBetween(const glm::vec3& a, const glm::vec3& b) {
    const float la = glm::length(a), lb = glm::length(b);
    if (la < 1e-8f || lb < 1e-8f) return glm::quat(1, 0, 0, 0);
    const glm::vec3 u = a / la, v = b / lb;
    const float d = glm::dot(u, v);
    if (d > 0.999999f) return glm::quat(1, 0, 0, 0);
    if (d < -0.999999f) {
        glm::vec3 axis = glm::cross(glm::vec3(1, 0, 0), u);
        if (glm::dot(axis, axis) < 1e-6f) axis = glm::cross(glm::vec3(0, 1, 0), u);
        return glm::angleAxis(3.14159265f, glm::normalize(axis));
    }
    const glm::vec3 c = glm::cross(u, v);
    const float s = std::sqrt((1.0f + d) * 2.0f);
    return glm::normalize(glm::quat(s * 0.5f, c.x / s, c.y / s, c.z / s));
}

glm::mat4 compose(const BoneTRS& b) {
    return glm::translate(glm::mat4(1.0f), b.t) * glm::mat4_cast(b.r) * glm::scale(glm::mat4(1.0f), b.s);
}

glm::quat rotationOf(const glm::mat4& m) {
    glm::mat3 r(m);
    for (int i = 0; i < 3; ++i) {
        const float l = glm::length(r[i]);
        if (l > 1e-8f) r[i] /= l;
    }
    return glm::normalize(glm::quat_cast(r));
}

float smooth01(float x) {
    x = std::clamp(x, 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
}

glm::vec3 closestOnSegment(const glm::vec3& p, const glm::vec3& a, const glm::vec3& b) {
    const glm::vec3 ab = b - a;
    const float l2 = glm::dot(ab, ab);
    if (l2 < 1e-12f) return a;
    return a + ab * std::clamp(glm::dot(p - a, ab) / l2, 0.0f, 1.0f);
}

void collide(glm::vec3& p, float radius, const std::vector<JiggleCollider>& colliders) {
    for (const JiggleCollider& c : colliders) {
        const glm::vec3 q = closestOnSegment(p, c.a, c.b);
        const glm::vec3 d = p - q;
        const float min = c.radius + radius;
        const float l2 = glm::dot(d, d);
        if (l2 >= min * min) continue;
        const float l = std::sqrt(l2);
        p = l > 1e-6f ? q + d * (min / l) : q + glm::vec3(0, min, 0);
    }
}

// The pull back toward the pose: `stiffness`, weaker near rest by `soften`.
float pull(const JiggleSettings& s, float error, float scale) {
    const float e = error / std::max(scale, 1e-4f);
    return std::clamp(s.stiffness * glm::mix(1.0f, std::min(1.0f, e * 2.0f), s.soften), 0.0f, 1.0f);
}

// Verlet with the two drags: relative to the anchor's motion this step
// (`drag`), and in the world (`airDrag`).
glm::vec3 verletVelocity(const glm::vec3& v, const glm::vec3& anchorV, const JiggleSettings& s) {
    return (anchorV + (v - anchorV) * (1.0f - s.drag)) * (1.0f - s.airDrag);
}

} // namespace

// ---------------------------------------------------------------------

int JiggleClock::advance(float dt) {
    if (!(dt > 0.0f)) return 0;
    m_accum += dt;
    int n = static_cast<int>(m_accum / m_step);
    m_accum -= n * m_step;
    if (n > m_maxSteps) n = m_maxSteps; // a hitch: drop the rest instead of spiralling
    return n;
}

std::vector<glm::mat4> restModelTransforms(const ModelData& model) {
    std::vector<glm::mat4> m(model.bones.size());
    for (size_t b = 0; b < model.bones.size(); ++b) {
        const int p = model.bones[b].parent;
        m[b] = p >= 0 ? m[p] * model.bones[b].localRest : model.bones[b].localRest;
    }
    return m;
}

// ---------------------------------------------------------------------
// JiggleRig

JiggleRig::JiggleRig(const ModelData& model, const std::vector<Chain>& chains, float stepHz) : m_chains(chains), m_clock(stepHz) {
    const std::vector<glm::mat4> rest = restModelTransforms(model);
    const int n = static_cast<int>(model.bones.size());
    for (size_t c = 0; c < chains.size(); ++c) {
        const int root = chains[c].root;
        if (root < 0 || root >= n) continue;
        std::vector<int> pointOf(n, -1);
        std::vector<int> childCount(n, 0);
        auto add = [&](Point p) {
            m_points.push_back(p);
            return static_cast<int>(m_points.size()) - 1;
        };
        Point r;
        r.bone = root;
        r.chain = static_cast<int>(c);
        pointOf[root] = add(r);
        for (int b = root + 1; b < n; ++b) {
            const int parent = model.bones[b].parent;
            if (parent < 0 || pointOf[parent] < 0) continue;
            Point p;
            p.bone = b;
            p.parent = pointOf[parent];
            p.chain = static_cast<int>(c);
            p.restLength = glm::length(glm::vec3(rest[b][3]) - glm::vec3(rest[parent][3]));
            if (childCount[parent]++ == 0) p.firstChildOf = pointOf[parent];
            pointOf[b] = add(p);
        }
        // Virtual tips on every leaf, so the last bone turns too.
        const size_t chainEnd = m_points.size();
        for (size_t i = 0; i < chainEnd; ++i) {
            if (m_points[i].chain != static_cast<int>(c) || m_points[i].bone < 0 || childCount[m_points[i].bone] > 0) continue;
            const int bone = m_points[i].bone;
            glm::vec3 tip = chains[c].tipOffset;
            if (glm::dot(tip, tip) < 1e-10f) {
                if (bone == root) continue; // a lone bone with no tip: nothing to point with
                // Continue the parent segment, in this bone's space.
                const glm::vec3 head = glm::vec3(rest[bone][3]);
                const glm::vec3 ext = head + (head - glm::vec3(rest[model.bones[bone].parent][3]));
                tip = glm::vec3(glm::inverse(rest[bone]) * glm::vec4(ext, 1.0f));
            }
            Point t;
            t.parent = static_cast<int>(i);
            t.chain = static_cast<int>(c);
            t.tipLocal = tip;
            t.restLength = glm::length(glm::mat3(rest[bone]) * tip);
            t.firstChildOf = static_cast<int>(i);
            m_points.push_back(t);
        }
    }
    // Parents must come before children; tips were appended after their
    // chain, which keeps that true.
}

void JiggleRig::stepOnce(float h, float t, const std::vector<JiggleCollider>& colliders) {
    for (Point& p : m_points) {
        p.animStep = glm::mix(p.animPrevFrame, p.animNow, t);
        if (p.parent < 0) {
            p.prev = p.pos;
            p.pos = p.animStep;
            p.delta = glm::quat(1, 0, 0, 0);
            p.offsetPrev = p.offsetCur = glm::vec3(0.0f);
            continue;
        }
        const JiggleSettings& s = m_chains[p.chain].settings;
        Point& q = m_points[p.parent];
        const glm::vec3 v = p.pos - p.prev;
        const glm::vec3 vq = q.pos - q.prev;
        p.prev = p.pos;
        p.pos += verletVelocity(v, vq, s) + kGravity * (s.gravity * h * h);

        // Where the pose wants it: the animated segment, carried along by
        // how the bones above it have already turned.
        const glm::quat carry = q.parent >= 0 ? m_points[q.parent].delta : glm::quat(1, 0, 0, 0);
        const glm::vec3 seg = carry * (p.animStep - q.animStep);
        const glm::vec3 target = q.pos + seg;
        p.pos = glm::mix(p.pos, target, pull(s, glm::length(p.pos - target), p.restLength));

        if (s.angleLimit > 0.0f) {
            const glm::vec3 a = p.pos - q.pos;
            const float la = glm::length(a), ls = glm::length(seg);
            if (la > 1e-6f && ls > 1e-6f) {
                const float cosA = std::clamp(glm::dot(a, seg) / (la * ls), -1.0f, 1.0f);
                const float limit = glm::radians(std::min(s.angleLimit, 179.0f));
                const float angle = std::acos(cosA);
                if (angle > limit) {
                    const glm::quat full = rotationBetween(seg, a);
                    const glm::quat part = glm::slerp(glm::quat(1, 0, 0, 0), full, limit / angle);
                    p.pos = q.pos + part * (seg / ls) * la;
                }
            }
        }
        // Length: kept, or springy with `stretch`, always within maxStretch.
        glm::vec3 d = p.pos - q.pos;
        float len = glm::length(d);
        if (len > 1e-7f && p.restLength > 1e-7f) {
            float want = glm::mix(p.restLength, len, s.stretch);
            want = std::clamp(want, p.restLength / s.maxStretch, p.restLength * s.maxStretch);
            p.pos = q.pos + d * (want / len);
        }
        collide(p.pos, s.radius, colliders);
        if (p.firstChildOf >= 0) q.delta = rotationBetween(seg, p.pos - q.pos) * carry;
        p.offsetPrev = p.offsetCur;
        p.offsetCur = p.pos - p.animStep;
    }
}

void JiggleRig::apply(const ModelData& model, Pose& pose, const glm::mat4& toWorld, float dt, const std::vector<JiggleCollider>& colliders) {
    if (m_points.empty() || pose.size() < model.bones.size()) return;
    m_scratchModel = poseToModel(model, pose);
    std::vector<glm::mat4>& mm = m_scratchModel;
    // Animated positions this frame.
    float moved = 0.0f;
    for (Point& p : m_points) {
        p.animPrevFrame = p.animNow;
        const glm::vec3 local = p.bone >= 0 ? glm::vec3(mm[p.bone][3]) : glm::vec3(mm[m_points[p.parent].bone] * glm::vec4(p.tipLocal, 1.0f));
        p.animNow = glm::vec3(toWorld * glm::vec4(local, 1.0f));
        moved = std::max(moved, glm::length(p.animNow - p.animPrevFrame));
    }
    if (!m_initialized || moved > teleportDistance) {
        for (Point& p : m_points) {
            p.pos = p.prev = p.animStep = p.animPrevFrame = p.animNow;
            p.offsetPrev = p.offsetCur = glm::vec3(0.0f);
            p.rendered = p.animNow;
            p.delta = glm::quat(1, 0, 0, 0);
        }
        m_initialized = true;
        m_sleeping = false;
        m_quietSteps = 0;
    }

    int steps = m_clock.advance(dt);
    if (m_sleeping && moved < 1e-5f && colliders.empty()) steps = 0;
    else m_sleeping = false;
    for (int k = 0; k < steps; ++k) {
        stepOnce(m_clock.step(), static_cast<float>(k + 1) / static_cast<float>(steps), colliders);
        ++m_stepsRun;
    }
    if (steps > 0) {
        float speed = 0.0f;
        for (const Point& p : m_points) speed = std::max(speed, glm::length(p.offsetCur - p.offsetPrev));
        m_quietSteps = (speed < 1e-5f && moved < 1e-5f) ? m_quietSteps + steps : 0;
        if (m_quietSteps > 45) m_sleeping = true;
    }

    // Rendered: the current pose plus the interpolated lag.
    const float a = m_clock.alpha();
    for (Point& p : m_points) p.rendered = p.animNow + glm::mix(p.offsetPrev, p.offsetCur, a);

    // Turn each bone toward its first child's point.
    const glm::mat4 toModel = glm::inverse(toWorld);
    for (Point& c : m_points) {
        if (c.firstChildOf < 0) continue;
        Point& owner = m_points[c.firstChildOf];
        const int b = owner.bone;
        if (b < 0) continue;
        const JiggleSettings& s = m_chains[owner.chain].settings;
        const int parent = model.bones[b].parent;
        const glm::mat4 parentM = parent >= 0 ? mm[parent] : glm::mat4(1.0f);
        BoneTRS& trs = pose[b];
        // Refresh this bone from its (possibly just changed) parent.
        mm[b] = parentM * compose(trs);
        // Head: follows its point (stretch moves it), root stays animated.
        if (owner.parent >= 0 && s.stretch > 0.0f) {
            const glm::vec3 headModel = glm::vec3(toModel * glm::vec4(owner.rendered, 1.0f));
            const glm::vec3 t = glm::vec3(glm::inverse(parentM) * glm::vec4(headModel, 1.0f));
            trs.t = glm::mix(trs.t, t, s.blend);
            mm[b] = parentM * compose(trs);
        }
        const glm::vec3 childLocal = c.bone >= 0 ? pose[c.bone].t : c.tipLocal;
        const glm::vec3 head = glm::vec3(mm[b][3]);
        const glm::vec3 dAnim = glm::vec3(mm[b] * glm::vec4(childLocal, 1.0f)) - head;
        const glm::vec3 dSim = glm::vec3(toModel * glm::vec4(c.rendered, 1.0f)) - head;
        const glm::quat r = glm::slerp(glm::quat(1, 0, 0, 0), rotationBetween(dAnim, dSim), s.blend);
        const glm::quat parentRot = rotationOf(parentM);
        trs.r = glm::normalize(glm::inverse(parentRot) * r * parentRot * trs.r);
        // Squash and stretch of a one-bone chain along its axis, volume kept.
        if (c.bone < 0 && s.stretch > 0.0f) {
            const float la = glm::length(dAnim);
            const float ratio = la > 1e-6f ? glm::mix(1.0f, glm::length(dSim) / la, s.blend) : 1.0f;
            const glm::vec3 t = glm::abs(c.tipLocal);
            const float tl = glm::length(c.tipLocal);
            int axis = t.x > t.y ? (t.x > t.z ? 0 : 2) : (t.y > t.z ? 1 : 2);
            if (tl > 1e-6f && t[axis] > 0.95f * tl) {
                const float side = 1.0f / std::sqrt(std::max(ratio, 1e-3f));
                glm::vec3 sc(side);
                sc[axis] = ratio;
                trs.s = trs.s * sc;
            }
        }
        mm[b] = parentM * compose(trs);
        // Children of this bone that aren't in the rig still need the new
        // parent: poseToModel() by the caller recomputes those.
    }
}

float JiggleRig::maxSwingDegrees() const {
    float most = 0.0f;
    for (const Point& p : m_points) {
        if (p.parent < 0) continue;
        const Point& q = m_points[p.parent];
        const glm::vec3 a = p.rendered - q.rendered, b = p.animNow - q.animNow;
        const float la = glm::length(a), lb = glm::length(b);
        if (la < 1e-6f || lb < 1e-6f) continue;
        most = std::max(most, glm::degrees(std::acos(std::clamp(glm::dot(a, b) / (la * lb), -1.0f, 1.0f))));
    }
    return most;
}

float JiggleRig::maxStretchNow() const {
    float most = 0.0f;
    for (const Point& p : m_points) {
        if (p.parent < 0 || p.restLength <= 1e-6f) continue;
        most = std::max(most, std::abs(glm::length(p.rendered - m_points[p.parent].rendered) / p.restLength - 1.0f));
    }
    return most;
}

// ---------------------------------------------------------------------
// Adding soft tissue to a rig

int addJiggleBone(ModelData& model, int parent, const std::string& name, const glm::vec3& position, const glm::vec3& axis,
                  float radius, float strength) {
    if (parent < 0 || parent >= static_cast<int>(model.bones.size())) return -1;
    const std::vector<glm::mat4> rest = restModelTransforms(model);
    const glm::vec3 ax = glm::dot(axis, axis) > 1e-10f ? glm::normalize(axis) : glm::vec3(0, 1, 0);
    const glm::mat4 restNew = glm::translate(glm::mat4(1.0f), position) * glm::mat4_cast(rotationBetween(glm::vec3(0, 1, 0), ax));
    // Mesh geometry space -> model space at rest (the same for every bone
    // of a well-formed rig; the parent's is the one that matters here).
    const glm::mat4 geomToModel = rest[parent] * model.bones[parent].inverseBind;

    ModelBone bone;
    bone.name = name;
    bone.parent = parent;
    bone.localRest = glm::inverse(rest[parent]) * restNew;
    bone.inverseBind = glm::inverse(restNew) * geomToModel;
    model.bones.push_back(bone);
    const int index = static_cast<int>(model.bones.size()) - 1;
    for (ModelAnimation& anim : model.animations)
        for (std::vector<glm::mat4>& frame : anim.frames)
            if (frame.size() + 1 == model.bones.size()) frame.push_back(bone.localRest);

    // Which bones count as "this body part": the parent and its ancestors.
    std::vector<uint8_t> allowed(model.bones.size(), 0);
    for (int b = parent; b >= 0; b = model.bones[b].parent) allowed[b] = 1;
    const float r = std::max(radius, 1e-4f);
    for (ModelMesh& mesh : model.meshes) {
        if (!mesh.skinned) continue;
        for (ModelVertex& v : mesh.vertices) {
            const glm::vec3 p = glm::vec3(geomToModel * glm::vec4(v.position, 1.0f));
            const float w = std::clamp(strength, 0.0f, 1.0f) * smooth01(1.0f - glm::length(p - position) / r);
            if (w <= 1e-4f) continue;
            int dominant = 0;
            for (int i = 1; i < 4; ++i)
                if (v.weights[i] > v.weights[dominant]) dominant = i;
            const uint32_t db = v.joints[dominant];
            if (db >= allowed.size() || !allowed[db]) continue;
            int slot = 0;
            for (int i = 1; i < 4; ++i)
                if (v.weights[i] < v.weights[slot]) slot = i;
            v.weights[slot] = 0.0f;
            float sum = v.weights[0] + v.weights[1] + v.weights[2] + v.weights[3];
            if (sum > 1e-6f) v.weights *= (1.0f - w) / sum;
            v.joints[slot] = static_cast<uint32_t>(index);
            v.weights[slot] = sum > 1e-6f ? w : 1.0f;
        }
    }
    return index;
}

size_t inflateSkin(ModelData& model, const glm::vec3& center, float radius, float amount, const glm::vec3& direction, float directionBias) {
    const std::vector<glm::mat4> rest = restModelTransforms(model);
    const float r = std::max(radius, 1e-4f);
    const bool hasDir = glm::dot(direction, direction) > 1e-10f;
    const glm::vec3 dir = hasDir ? glm::normalize(direction) : glm::vec3(0.0f);
    size_t moved = 0;
    for (ModelMesh& mesh : model.meshes) {
        std::vector<float> weightOf(mesh.vertices.size(), 0.0f);
        for (size_t i = 0; i < mesh.vertices.size(); ++i) {
            ModelVertex& v = mesh.vertices[i];
            glm::mat4 s(1.0f);
            if (mesh.skinned) {
                s = glm::mat4(0.0f);
                for (int k = 0; k < 4; ++k)
                    if (v.weights[k] > 0.0f && v.joints[k] < model.bones.size())
                        s += (rest[v.joints[k]] * model.bones[v.joints[k]].inverseBind) * v.weights[k];
                if (std::abs(glm::determinant(glm::mat3(s))) < 1e-10f) continue;
            }
            const glm::vec3 p = glm::vec3(s * glm::vec4(v.position, 1.0f));
            const float f = smooth01(1.0f - glm::length(p - center) / r);
            if (f <= 0.0f) continue;
            // The push depends on position only, never on the vertex normal:
            // low-poly art is flat shaded (each corner split per face with
            // its own normal), and a normal-based push would tear it apart.
            const glm::vec3 radial = glm::length(p - center) > 1e-6f ? glm::normalize(p - center) : (hasDir ? dir : glm::vec3(0, 1, 0));
            glm::vec3 n = radial;
            if (hasDir) {
                // Only the side facing the direction swells.
                const float facing = std::max(0.0f, glm::dot(radial, dir));
                n = glm::normalize(glm::mix(radial, dir, directionBias)) * facing;
            }
            const glm::vec3 np = p + n * (amount * f);
            v.position = glm::vec3(glm::inverse(s) * glm::vec4(np, 1.0f));
            weightOf[i] = f;
            ++moved;
        }
        // Normals of moved vertices from the new surface (area weighted),
        // blended by how much each moved so the edge of the region blends in.
        std::vector<glm::vec3> acc(mesh.vertices.size(), glm::vec3(0.0f));
        bool any = false;
        for (size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
            const uint32_t i0 = mesh.indices[t], i1 = mesh.indices[t + 1], i2 = mesh.indices[t + 2];
            if (weightOf[i0] <= 0.0f && weightOf[i1] <= 0.0f && weightOf[i2] <= 0.0f) continue;
            const glm::vec3 fn = glm::cross(mesh.vertices[i1].position - mesh.vertices[i0].position, mesh.vertices[i2].position - mesh.vertices[i0].position);
            acc[i0] += fn;
            acc[i1] += fn;
            acc[i2] += fn;
            any = true;
        }
        if (!any) continue;
        for (size_t i = 0; i < mesh.vertices.size(); ++i) {
            if (weightOf[i] <= 0.0f || glm::dot(acc[i], acc[i]) < 1e-20f) continue;
            glm::vec3 fresh = glm::normalize(acc[i]);
            if (glm::dot(fresh, mesh.vertices[i].normal) < 0.0f) fresh = -fresh;
            mesh.vertices[i].normal = glm::normalize(glm::mix(mesh.vertices[i].normal, fresh, weightOf[i]));
        }
    }
    return moved;
}

// ---------------------------------------------------------------------
// Humanoid soft tissue

HumanoidJiggleSetup addHumanoidSoftTissue(ModelData& model, const HumanoidSoftTissue& o) {
    HumanoidJiggleSetup out;
    auto find = [&](std::initializer_list<const char*> names) {
        for (const char* want : names)
            for (size_t i = 0; i < model.bones.size(); ++i)
                if (canonicalBoneName(model.bones[i].name) == want) return static_cast<int>(i);
        return -1;
    };
    const int pelvis = find({ "pelvis" });
    const int chest = find({ "spine_03", "chest", "spine_02", "spine2" });
    const int belly = find({ "spine_01", "spine", "spine1" });
    const int thighL = find({ "thigh_l", "upperleg_l", "leftupleg" }), thighR = find({ "thigh_r", "upperleg_r", "rightupleg" });
    if (pelvis < 0) out.missing.push_back("pelvis");
    if (chest < 0) out.missing.push_back("spine_03 (chest)");
    if (thighL < 0 || thighR < 0) out.missing.push_back("thigh_l / thigh_r");
    if (pelvis < 0 || thighL < 0 || thighR < 0) return out;

    std::vector<glm::mat4> rest = restModelTransforms(model);
    auto at = [&](int b) { return glm::vec3(rest[b][3]); };
    const glm::vec3 fwd = modelForward(model);
    glm::vec3 left = at(thighL) - at(thighR);
    left.y = 0.0f;
    left = glm::length(left) > 1e-5f ? glm::normalize(left) : glm::vec3(1, 0, 0);
    const float height = std::max(model.boundsMax.y - model.boundsMin.y, 0.5f);
    const float k = height / 1.8f; // sizes below are for a 1.8 m person

    // Skin in model space at rest, with each vertex's dominant bone.
    struct SkinPoint { glm::vec3 p; int bone; };
    std::vector<SkinPoint> skin;
    for (const ModelMesh& mesh : model.meshes) {
        if (!mesh.skinned) continue;
        for (const ModelVertex& v : mesh.vertices) {
            int d = 0;
            for (int i = 1; i < 4; ++i)
                if (v.weights[i] > v.weights[d]) d = i;
            const int b = static_cast<int>(v.joints[d]);
            if (b < 0 || b >= static_cast<int>(model.bones.size())) continue;
            skin.push_back({ glm::vec3(rest[b] * model.bones[b].inverseBind * glm::vec4(v.position, 1.0f)), b });
        }
    }
    auto isUnder = [&](int b, int ancestor) {
        for (; b >= 0; b = model.bones[b].parent)
            if (b == ancestor) return true;
        return false;
    };
    // The skin point furthest along `dir` on one side, near a height band.
    auto extreme = [&](int bone, float sideSign, float yMin, float yMax, const glm::vec3& dir, bool childrenToo, glm::vec3& found) {
        float best = -1e9f;
        bool ok = false;
        for (const SkinPoint& s : skin) {
            const bool mine = s.bone == bone || (childrenToo && model.bones[s.bone].parent == bone) || (s.bone >= 0 && isUnder(bone, s.bone) && s.bone != 0);
            if (!mine) continue;
            if (s.p.y < yMin || s.p.y > yMax) continue;
            const float side = glm::dot(s.p - at(pelvis), left) * sideSign;
            if (side < 0.03f * k || side > 0.2f * k) continue;
            const float d = glm::dot(s.p, dir);
            if (d > best) {
                best = d;
                found = s.p;
                ok = true;
            }
        }
        return ok;
    };

    // Breasts: the most forward chest skin either side of the sternum.
    if (chest >= 0) {
        const float y = at(chest).y;
        const float r = 0.085f * k;
        for (int side = 0; side < 2; ++side) {
            const float sign = side == 0 ? 1.0f : -1.0f;
            glm::vec3 front(0.0f);
            if (!extreme(chest, sign, y - 0.2f * k, y + 0.12f * k, fwd, false, front)) {
                out.missing.push_back(side == 0 ? "left chest skin" : "right chest skin");
                continue;
            }
            const glm::vec3 centre = front - fwd * (r * 0.8f);
            if (o.bust > 0.0f) inflateSkin(model, centre + fwd * (r * 0.5f), r * 1.35f, o.bust, fwd, 0.65f);
            if (o.breastBones) {
                const float len = r * 0.9f + o.bust;
                const int b = addJiggleBone(model, chest, side == 0 ? "Breast_L" : "Breast_R", centre, fwd, r * 1.7f, 1.0f);
                if (b >= 0) {
                    out.addedBones.push_back(b);
                    JiggleRig::Chain c;
                    c.root = b;
                    c.tipOffset = glm::vec3(0, len, 0);
                    c.settings = o.breast;
                    out.chains.push_back(c);
                }
            }
        }
    }
    // Glutes: the most backward skin on each side just below the pelvis.
    {
        rest = restModelTransforms(model);
        const float y = at(pelvis).y;
        const float r = 0.095f * k;
        for (int side = 0; side < 2; ++side) {
            const float sign = side == 0 ? 1.0f : -1.0f;
            glm::vec3 back(0.0f);
            if (!extreme(pelvis, sign, y - 0.2f * k, y + 0.04f * k, -fwd, true, back)) {
                out.missing.push_back(side == 0 ? "left glute skin" : "right glute skin");
                continue;
            }
            const glm::vec3 centre = back + fwd * (r * 0.8f);
            if (o.glutes > 0.0f) inflateSkin(model, centre - fwd * (r * 0.5f), r * 1.4f, o.glutes, -fwd, 0.6f);
            if (o.hips > 0.0f) inflateSkin(model, at(pelvis) + left * (sign * 0.14f * k) - glm::vec3(0, 0.06f * k, 0), r * 1.6f, o.hips, left * sign, 0.7f);
            if (o.gluteBones) {
                const int b = addJiggleBone(model, pelvis, side == 0 ? "Glute_L" : "Glute_R", centre, -fwd, r * 1.6f, 0.9f);
                if (b >= 0) {
                    out.addedBones.push_back(b);
                    JiggleRig::Chain c;
                    c.root = b;
                    c.tipOffset = glm::vec3(0, r * 0.9f + o.glutes, 0);
                    c.settings = o.glute;
                    out.chains.push_back(c);
                }
            }
        }
    }
    // Skin zones (no bones): belly, and the back of each thigh.
    rest = restModelTransforms(model);
    auto zone = [&](int bone, const glm::vec3& p, float radius, float maxOffset) {
        JiggleSkin::Zone z;
        z.bone = bone;
        z.radius = radius;
        z.maxOffset = maxOffset;
        z.settings = o.skin;
        out.zones.push_back(z);
        out.zonePositions.push_back(p);
    };
    if (o.bellyZone && belly >= 0) zone(belly, at(belly) + fwd * (0.1f * k), 0.13f * k, 0.018f * k);
    if (o.thighZones) {
        for (int t : { thighL, thighR }) {
            const int calf = [&] {
                for (size_t i = 0; i < model.bones.size(); ++i)
                    if (model.bones[i].parent == t) return static_cast<int>(i);
                return -1;
            }();
            const glm::vec3 mid = calf >= 0 ? (at(t) * 0.6f + at(calf) * 0.4f) : at(t) - glm::vec3(0, 0.15f * k, 0);
            zone(t, mid - fwd * (0.04f * k), 0.12f * k, 0.015f * k);
        }
    }
    return out;
}

// ---------------------------------------------------------------------
// JiggleSkin

glm::vec3 skinJiggleDisplacement(const std::vector<SkinJiggleOffset>& zones, const glm::vec3& p) {
    glm::vec3 d(0.0f);
    for (const SkinJiggleOffset& z : zones) {
        const glm::vec3 e = p - z.center;
        const float r2 = z.radius * z.radius;
        const float l2 = glm::dot(e, e);
        if (l2 >= r2) continue;
        const float x = 1.0f - l2 / r2; // smooth, cheap (no sqrt), zero slope at the edge
        d += z.offset * (x * x);
    }
    return d;
}

JiggleSkin::JiggleSkin(const ModelData& model, std::vector<Zone> zones, const std::vector<glm::vec3>& modelPositions, float stepHz)
    : m_zones(std::move(zones)), m_clock(stepHz) {
    const std::vector<glm::mat4> rest = restModelTransforms(model);
    for (size_t i = 0; i < m_zones.size() && i < modelPositions.size(); ++i) {
        Zone& z = m_zones[i];
        if (z.bone < 0 || z.bone >= static_cast<int>(rest.size())) continue;
        z.local = glm::vec3(glm::inverse(rest[z.bone]) * glm::vec4(modelPositions[i], 1.0f));
    }
    m_zones.erase(std::remove_if(m_zones.begin(), m_zones.end(), [&](const Zone& z) { return z.bone < 0 || z.bone >= static_cast<int>(rest.size()); }),
                  m_zones.end());
    m_state.resize(m_zones.size());
    m_out.resize(m_zones.size());
}

void JiggleSkin::apply(const ModelData& model, const Pose& pose, const glm::mat4& toWorld, float dt) {
    if (m_zones.empty()) return;
    const std::vector<glm::mat4> mm = poseToModel(model, pose);
    for (size_t i = 0; i < m_zones.size(); ++i) {
        State& s = m_state[i];
        s.animPrev = s.animNow;
        s.animNow = glm::vec3(toWorld * mm[m_zones[i].bone] * glm::vec4(m_zones[i].local, 1.0f));
        if (!m_initialized || glm::length(s.animNow - s.animPrev) > 1.5f) {
            s.pos = s.prev = s.animPrev = s.animNow;
            s.offPrev = s.offCur = glm::vec3(0.0f);
        }
    }
    m_initialized = true;
    const int steps = m_clock.advance(dt);
    const float h = m_clock.step();
    for (int k = 0; k < steps; ++k) {
        const float t0 = static_cast<float>(k) / steps, t1 = static_cast<float>(k + 1) / steps;
        for (size_t i = 0; i < m_zones.size(); ++i) {
            const Zone& z = m_zones[i];
            const JiggleSettings& js = z.settings;
            State& s = m_state[i];
            const glm::vec3 a0 = glm::mix(s.animPrev, s.animNow, t0), a1 = glm::mix(s.animPrev, s.animNow, t1);
            const glm::vec3 v = s.pos - s.prev;
            s.prev = s.pos;
            s.pos += verletVelocity(v, a1 - a0, js) + kGravity * (js.gravity * h * h);
            s.pos = glm::mix(s.pos, a1, pull(js, glm::length(s.pos - a1), z.maxOffset));
            glm::vec3 off = s.pos - a1;
            const float l = glm::length(off);
            if (l > z.maxOffset) {
                off *= z.maxOffset / l;
                s.pos = a1 + off;
            }
            s.offPrev = s.offCur;
            s.offCur = off;
        }
    }
    const glm::mat3 toModel = glm::mat3(glm::inverse(toWorld));
    const float a = m_clock.alpha();
    for (size_t i = 0; i < m_zones.size(); ++i) {
        const State& s = m_state[i];
        m_out[i].center = glm::vec3(mm[m_zones[i].bone] * glm::vec4(m_zones[i].local, 1.0f));
        m_out[i].radius = m_zones[i].radius;
        m_out[i].offset = toModel * (glm::mix(s.offPrev, s.offCur, a) * m_zones[i].settings.blend);
    }
}

// ---------------------------------------------------------------------
// JellyBody

void extractRotation(const glm::mat3& a, glm::quat& q, int iterations) {
    for (int i = 0; i < iterations; ++i) {
        const glm::mat3 r = glm::mat3_cast(q);
        const glm::vec3 num = glm::cross(r[0], a[0]) + glm::cross(r[1], a[1]) + glm::cross(r[2], a[2]);
        const float den = std::abs(glm::dot(r[0], a[0]) + glm::dot(r[1], a[1]) + glm::dot(r[2], a[2])) + 1e-9f;
        const glm::vec3 omega = num / den;
        const float w = glm::length(omega);
        if (w < 1e-9f) break;
        q = glm::normalize(glm::angleAxis(w, omega / w) * q);
    }
}

JellyBody::JellyBody(const Params& p) : m_p(p) { reset(); }

void JellyBody::reset() {
    m_p.cells = glm::max(m_p.cells, glm::ivec3(1));
    const glm::ivec3 n = m_p.cells + 1;
    const size_t count = static_cast<size_t>(n.x) * n.y * n.z;
    m_rest.resize(count);
    m_pinned.assign(count, 0);
    for (int z = 0; z < n.z; ++z)
        for (int y = 0; y < n.y; ++y)
            for (int x = 0; x < n.x; ++x) {
                const glm::vec3 f = glm::vec3(x, y, z) / glm::vec3(m_p.cells);
                m_rest[id(x, y, z)] = glm::mix(m_p.min, m_p.max, f);
                if (m_p.pinBottom && y == 0) m_pinned[id(x, y, z)] = 1;
            }
    m_x = m_prev = m_rest;
    m_goal.assign(count, glm::vec3(0.0f));
    m_goalW.assign(count, 0.0f);
    m_cellRot.assign(static_cast<size_t>(m_p.cells.x) * m_p.cells.y * m_p.cells.z, glm::quat(1, 0, 0, 0));
    m_particleMass = m_p.mass / static_cast<float>(count);
    m_accum = 0.0f;
}

void JellyBody::poke(const glm::vec3& at, const glm::vec3& impulse, float radius) {
    // The impulse is shared by the particles in reach, weighted by falloff.
    const float h = 1.0f / m_p.stepHz;
    std::vector<float> f(m_x.size(), 0.0f);
    float total = 0.0f;
    for (size_t i = 0; i < m_x.size(); ++i) {
        if (m_pinned[i]) continue;
        f[i] = smooth01(1.0f - glm::length(m_x[i] - at) / std::max(radius, 1e-4f));
        total += f[i];
    }
    if (total <= 0.0f) return;
    for (size_t i = 0; i < m_x.size(); ++i)
        if (f[i] > 0.0f) m_prev[i] -= impulse * (f[i] / (total * m_particleMass)) * h;
}

void JellyBody::step(float dt, std::vector<Ball>& balls) {
    const float h = 1.0f / m_p.stepHz;
    m_accum += std::min(dt, 0.1f);
    while (m_accum >= h) {
        substep(h, balls);
        m_accum -= h;
    }
}

void JellyBody::substep(float h, std::vector<Ball>& balls) {
    const glm::vec3 g(0.0f, m_p.gravity, 0.0f);
    // Integrate.
    for (size_t i = 0; i < m_x.size(); ++i) {
        if (m_pinned[i]) {
            m_prev[i] = m_x[i] = m_rest[i];
            continue;
        }
        const glm::vec3 v = (m_x[i] - m_prev[i]) * (1.0f - m_p.damping);
        m_prev[i] = m_x[i];
        m_x[i] += v + g * (h * h);
    }
    std::vector<glm::vec3> ballPrev(balls.size());
    for (size_t b = 0; b < balls.size(); ++b) {
        ballPrev[b] = balls[b].pos;
        balls[b].vel += g * h;
        balls[b].pos += balls[b].vel * h;
    }

    const glm::vec3 spacing = (m_p.max - m_p.min) / glm::vec3(m_p.cells);
    const float particleRadius = 0.5f * std::max({ spacing.x, spacing.y, spacing.z });
    const glm::ivec3 c = m_p.cells;
    for (int it = 0; it < std::max(1, m_p.iterations); ++it) {
        // Shape matching over every 2x2x2 cell.
        std::fill(m_goal.begin(), m_goal.end(), glm::vec3(0.0f));
        std::fill(m_goalW.begin(), m_goalW.end(), 0.0f);
        size_t cell = 0;
        for (int z = 0; z < c.z; ++z)
            for (int y = 0; y < c.y; ++y)
                for (int x = 0; x < c.x; ++x, ++cell) {
                    size_t ids[8];
                    int k = 0;
                    for (int dz = 0; dz < 2; ++dz)
                        for (int dy = 0; dy < 2; ++dy)
                            for (int dx = 0; dx < 2; ++dx) ids[k++] = id(x + dx, y + dy, z + dz);
                    glm::vec3 cx(0.0f), cr(0.0f);
                    for (size_t i : ids) {
                        cx += m_x[i];
                        cr += m_rest[i];
                    }
                    cx *= 0.125f;
                    cr *= 0.125f;
                    glm::mat3 apq(0.0f);
                    for (size_t i : ids) apq += glm::outerProduct(m_x[i] - cx, m_rest[i] - cr);
                    extractRotation(apq, m_cellRot[cell], 3);
                    const glm::mat3 r = glm::mat3_cast(m_cellRot[cell]);
                    for (size_t i : ids) {
                        m_goal[i] += cx + r * (m_rest[i] - cr);
                        m_goalW[i] += 1.0f;
                    }
                }
        for (size_t i = 0; i < m_x.size(); ++i) {
            if (m_pinned[i] || m_goalW[i] <= 0.0f) continue;
            m_x[i] = glm::mix(m_x[i], m_goal[i] / m_goalW[i], m_p.stiffness);
        }
        // Balls against particles, both ways (position based).
        for (Ball& b : balls) {
            const float min = b.radius + particleRadius;
            // Cheap reject: the ball is nowhere near the jelly's bounds.
            if (b.pos.y - b.radius > m_p.max.y + (m_p.max.y - m_p.min.y) + particleRadius) continue;
            const float wb = 1.0f / std::max(b.mass, 1e-4f), wp = 1.0f / std::max(m_particleMass, 1e-6f);
            for (size_t i = 0; i < m_x.size(); ++i) {
                const glm::vec3 d = b.pos - m_x[i];
                const float l2 = glm::dot(d, d);
                if (l2 >= min * min) continue;
                const float l = std::sqrt(l2);
                const glm::vec3 n = l > 1e-6f ? d / l : glm::vec3(0, 1, 0);
                const float pen = min - l;
                if (m_pinned[i]) {
                    b.pos += n * pen;
                    continue;
                }
                b.pos += n * (pen * wb / (wb + wp));
                m_x[i] -= n * (pen * wp / (wb + wp));
            }
        }
        // The top as a height field: a ball whose centre is over the jelly
        // and below its top surface is inside, however it got there (fast,
        // squeezed between particles, pushed by another ball): out it goes,
        // upward, and the surface under it gives way. Distance tests alone
        // can't promise that.
        for (Ball& b : balls) {
            const glm::vec3 f = (b.pos - m_p.min) / (m_p.max - m_p.min) * glm::vec3(c);
            if (f.x < 0.0f || f.z < 0.0f || f.x > static_cast<float>(c.x) || f.z > static_cast<float>(c.z)) continue;
            const int x0 = std::min(static_cast<int>(f.x), c.x - 1), z0 = std::min(static_cast<int>(f.z), c.z - 1);
            const float tx = f.x - x0, tz = f.z - z0;
            const size_t ids[4] = { id(x0, c.y, z0), id(x0 + 1, c.y, z0), id(x0, c.y, z0 + 1), id(x0 + 1, c.y, z0 + 1) };
            const float w[4] = { (1 - tx) * (1 - tz), tx * (1 - tz), (1 - tx) * tz, tx * tz };
            float top = 0.0f;
            for (int k = 0; k < 4; ++k) top += m_x[ids[k]].y * w[k];
            top += particleRadius * 0.5f;
            const float pen = top + b.radius - b.pos.y;
            if (pen <= 0.0f || b.pos.y < m_p.floorY) continue;
            // Only from above: something below the top by more than its own
            // size isn't touching the top (it's beside the jelly, or under a lip).
            if (pen > 2.0f * b.radius + particleRadius) continue;
            float w2 = 0.0f;
            for (float wk : w) w2 += wk * wk;
            // The surface pushes back with the column of jelly under it, not
            // just its top particles (those are ~10 g each: a ball would
            // sink to the plate before the shape matching could answer).
            const float column = m_particleMass * static_cast<float>(c.y + 1);
            const float wb = 1.0f / std::max(b.mass, 1e-4f), wp = 1.0f / std::max(column, 1e-6f) * w2;
            const float share = wb / (wb + wp);
            b.pos.y += pen * share;
            for (int k = 0; k < 4; ++k)
                if (!m_pinned[ids[k]]) m_x[ids[k]].y -= pen * (1.0f - share) * w[k] / std::max(w2, 1e-6f);
            // Rolling on jelly is sticky.
            const glm::vec3 moved = b.pos - ballPrev[&b - balls.data()];
            b.pos.x -= moved.x * 0.02f;
            b.pos.z -= moved.z * 0.02f;
        }
        // Floor.
        for (size_t i = 0; i < m_x.size(); ++i) {
            if (m_pinned[i] || m_x[i].y >= m_p.floorY) continue;
            m_x[i].y = m_p.floorY;
            m_x[i].x = glm::mix(m_x[i].x, m_prev[i].x, m_p.floorFriction);
            m_x[i].z = glm::mix(m_x[i].z, m_prev[i].z, m_p.floorFriction);
        }
    }
    // Balls: velocity from what the constraints did, then the floor and
    // each other.
    for (size_t b = 0; b < balls.size(); ++b) {
        Ball& ball = balls[b];
        ball.vel = (ball.pos - ballPrev[b]) / h;
        if (ball.pos.y < m_p.floorY + ball.radius) {
            ball.pos.y = m_p.floorY + ball.radius;
            if (ball.vel.y < 0.0f) ball.vel.y = -ball.vel.y * ball.restitution;
            ball.vel.x *= 0.995f;
            ball.vel.z *= 0.995f;
        }
        for (size_t o = b + 1; o < balls.size(); ++o) {
            Ball& other = balls[o];
            const glm::vec3 d = other.pos - ball.pos;
            const float min = ball.radius + other.radius, l = glm::length(d);
            if (l >= min || l < 1e-6f) continue;
            const glm::vec3 n = d / l;
            const float w1 = 1.0f / ball.mass, w2 = 1.0f / other.mass;
            const float pen = min - l;
            ball.pos -= n * (pen * w1 / (w1 + w2));
            other.pos += n * (pen * w2 / (w1 + w2));
            const float vn = glm::dot(other.vel - ball.vel, n);
            if (vn < 0.0f) {
                const float j = -(1.0f + 0.5f) * vn / (w1 + w2);
                ball.vel -= n * (j * w1);
                other.vel += n * (j * w2);
            }
        }
    }
}

float JellyBody::deformation() const {
    if (m_x.empty()) return 0.0f;
    float sum = 0.0f;
    for (size_t i = 0; i < m_x.size(); ++i) sum += glm::length(m_x[i] - m_rest[i]);
    return sum / static_cast<float>(m_x.size());
}

void JellyBody::buildSurface(int res) {
    res = std::max(1, res);
    m_embed.clear();
    m_surfUv.clear();
    m_surfIdx.clear();
    // A rounded box: unit-cube face grids, welded, pulled onto a box with
    // rounded edges (jelly out of a mould), then embedded in the lattice.
    std::map<std::tuple<int, int, int>, uint32_t> weld;
    std::vector<glm::vec3> restPos;
    const glm::vec3 size = m_p.max - m_p.min, mid = (m_p.max + m_p.min) * 0.5f;
    const float round = 0.18f * std::min({ size.x, size.y, size.z });
    const glm::vec3 inner = glm::max(size * 0.5f - glm::vec3(round), glm::vec3(0.0f));
    auto vertex = [&](const glm::vec3& cube, const glm::vec2& uv) {
        const auto key = std::make_tuple(static_cast<int>(std::lround((cube.x + 1) * res)), static_cast<int>(std::lround((cube.y + 1) * res)),
                                         static_cast<int>(std::lround((cube.z + 1) * res)));
        if (auto it = weld.find(key); it != weld.end()) return it->second;
        const glm::vec3 local = cube * (size * 0.5f);
        const glm::vec3 core = glm::clamp(local, -inner, inner);
        glm::vec3 d = local - core;
        const float l = glm::length(d);
        const glm::vec3 p = mid + core + (l > 1e-6f ? d / l * round : glm::vec3(0.0f));
        const uint32_t index = static_cast<uint32_t>(restPos.size());
        restPos.push_back(glm::clamp(p, m_p.min, m_p.max));
        m_surfUv.push_back(uv);
        weld[key] = index;
        return index;
    };
    const glm::vec3 axes[3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
    for (int face = 0; face < 6; ++face) {
        const int a = face / 2;
        const float sgn = face % 2 ? -1.0f : 1.0f;
        const glm::vec3 n = axes[a] * sgn;
        const glm::vec3 u = axes[(a + 1) % 3], v = glm::cross(n, u);
        std::vector<uint32_t> grid((res + 1) * (res + 1));
        for (int j = 0; j <= res; ++j)
            for (int i = 0; i <= res; ++i) {
                const float fu = -1.0f + 2.0f * i / res, fv = -1.0f + 2.0f * j / res;
                grid[j * (res + 1) + i] = vertex(n + u * fu + v * fv, glm::vec2(static_cast<float>(i) / res, static_cast<float>(j) / res));
            }
        for (int j = 0; j < res; ++j)
            for (int i = 0; i < res; ++i) {
                const uint32_t i00 = grid[j * (res + 1) + i], i10 = grid[j * (res + 1) + i + 1];
                const uint32_t i01 = grid[(j + 1) * (res + 1) + i], i11 = grid[(j + 1) * (res + 1) + i + 1];
                // u x v = n: counter-clockwise seen from outside.
                m_surfIdx.insert(m_surfIdx.end(), { i00, i10, i11, i00, i11, i01 });
            }
    }
    for (const glm::vec3& p : restPos) {
        const glm::vec3 f = (p - m_p.min) / (m_p.max - m_p.min) * glm::vec3(m_p.cells);
        const glm::ivec3 cc = glm::clamp(glm::ivec3(glm::floor(f)), glm::ivec3(0), m_p.cells - 1);
        const uint32_t cell = static_cast<uint32_t>((cc.z * m_p.cells.y + cc.y) * m_p.cells.x + cc.x);
        m_embed.push_back({ cell, glm::clamp(f - glm::vec3(cc), glm::vec3(0.0f), glm::vec3(1.0f)) });
    }
    m_surfPos = restPos;
    m_surfNrm.assign(restPos.size(), glm::vec3(0, 1, 0));
    deform();
}

void JellyBody::deform() {
    const int cx = m_p.cells.x, cy = m_p.cells.y;
    for (size_t v = 0; v < m_embed.size(); ++v) {
        const Embed& e = m_embed[v];
        const int x = static_cast<int>(e.cell % cx), y = static_cast<int>((e.cell / cx) % cy), z = static_cast<int>(e.cell / (cx * cy));
        const glm::vec3 w = e.w;
        auto at = [&](int dx, int dy, int dz) { return m_x[id(x + dx, y + dy, z + dz)]; };
        const glm::vec3 x00 = glm::mix(at(0, 0, 0), at(1, 0, 0), w.x), x10 = glm::mix(at(0, 1, 0), at(1, 1, 0), w.x);
        const glm::vec3 x01 = glm::mix(at(0, 0, 1), at(1, 0, 1), w.x), x11 = glm::mix(at(0, 1, 1), at(1, 1, 1), w.x);
        m_surfPos[v] = glm::mix(glm::mix(x00, x10, w.y), glm::mix(x01, x11, w.y), w.z);
    }
    std::fill(m_surfNrm.begin(), m_surfNrm.end(), glm::vec3(0.0f));
    for (size_t t = 0; t + 2 < m_surfIdx.size(); t += 3) {
        const uint32_t a = m_surfIdx[t], b = m_surfIdx[t + 1], c = m_surfIdx[t + 2];
        const glm::vec3 n = glm::cross(m_surfPos[b] - m_surfPos[a], m_surfPos[c] - m_surfPos[a]);
        m_surfNrm[a] += n;
        m_surfNrm[b] += n;
        m_surfNrm[c] += n;
    }
    for (glm::vec3& n : m_surfNrm) {
        const float l = glm::length(n);
        n = l > 1e-12f ? n / l : glm::vec3(0, 1, 0);
    }
}

} // namespace kke
