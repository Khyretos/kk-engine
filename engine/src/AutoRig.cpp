#include "kke/AutoRig.h"

#include "kke/AnimRig.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

namespace kke {

namespace {

// The rotation turning direction a onto b (shortest arc).
glm::quat fromTo(glm::vec3 a, glm::vec3 b) {
    a = glm::normalize(a);
    b = glm::normalize(b);
    const float d = glm::dot(a, b);
    if (d > 0.99999f) return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    if (d < -0.99999f) {
        glm::vec3 axis = glm::cross(a, glm::vec3(1, 0, 0));
        if (glm::length(axis) < 1e-3f) axis = glm::cross(a, glm::vec3(0, 1, 0));
        return glm::angleAxis(3.14159265f, glm::normalize(axis));
    }
    return glm::normalize(glm::quat(1.0f + d, glm::cross(a, b)));
}

glm::quat rotationOf(const glm::mat4& m) {
    glm::mat3 r(m);
    for (int c = 0; c < 3; ++c) r[c] = glm::normalize(r[c]);
    return glm::normalize(glm::quat_cast(r));
}

glm::mat4 compose(const glm::vec3& p, const glm::quat& q) { return glm::translate(glm::mat4(1.0f), p) * glm::mat4_cast(q); }

float segmentDistance(const glm::vec3& p, const glm::vec3& a, const glm::vec3& b) {
    const glm::vec3 ab = b - a;
    const float t = std::clamp(glm::dot(p - a, ab) / std::max(glm::dot(ab, ab), 1e-12f), 0.0f, 1.0f);
    return glm::distance(p, a + ab * t);
}

struct Segment {
    int bone = -1;
    glm::vec3 a{ 0.0f }, b{ 0.0f };
    int side = 0; // +1 the left side only, -1 the right only, 0 both
};

} // namespace

namespace {

AutoRigReport rig(ModelData& body, const ModelData& reference, const AutoRigOptions& options) {
    AutoRigReport report;
    if (body.meshes.empty()) {
        report.reason = "the model has no mesh";
        return report;
    }
    const size_t n = reference.bones.size();
    auto find = [&](const std::string& name) {
        for (size_t i = 0; i < n; ++i)
            if (canonicalBoneName(reference.bones[i].name) == name) return static_cast<int>(i);
        return -1;
    };
    const char* sides[2] = { "_l", "_r" }; // [0] left, [1] right
    const int pelvis = find("pelvis"), spine1 = find("spine_01"), spine2 = find("spine_02"), spine3 = find("spine_03"), neck = find("neck_01"),
              head = find("head");
    std::array<int, 2> clav{}, upper{}, lower{}, hand{}, thigh{}, calf{}, foot{}, ball{}, fingerTip{}, toeTip{};
    for (int s = 0; s < 2; ++s) {
        const std::string sfx = sides[s];
        clav[s] = find("clavicle" + sfx);
        upper[s] = find("upperarm" + sfx);
        lower[s] = find("lowerarm" + sfx);
        hand[s] = find("hand" + sfx);
        thigh[s] = find("thigh" + sfx);
        calf[s] = find("calf" + sfx);
        foot[s] = find("foot" + sfx);
        ball[s] = find("ball" + sfx);
        fingerTip[s] = find("middle_04" + sfx); // UAL's middle_04_leaf; may be missing
        toeTip[s] = -1;
        for (size_t i = 0; i < n; ++i) // ball's child (UAL's ball_leaf), if any
            if (ball[s] >= 0 && reference.bones[i].parent == ball[s]) toeTip[s] = static_cast<int>(i);
    }
    for (int b : { pelvis, spine1, spine2, spine3, neck, head, clav[0], clav[1], upper[0], upper[1], lower[0], lower[1], hand[0], hand[1], thigh[0],
                   thigh[1], calf[0], calf[1], foot[0], foot[1], ball[0], ball[1] })
        if (b < 0) {
            report.reason = "the reference skeleton lacks a UE-style humanoid bone (pelvis, spine_01..03, neck_01, Head, clavicle, upperarm, "
                            "lowerarm, hand, thigh, calf, foot, ball)";
            return report;
        }

    const std::vector<glm::mat4> refRest = computeRestPose(reference);
    std::vector<glm::vec3> refPos(n);
    std::vector<glm::quat> refRot(n);
    for (size_t b = 0; b < n; ++b) {
        refPos[b] = glm::vec3(refRest[b][3]);
        refRot[b] = rotationOf(refRest[b]);
    }

    // 1. Face the way the reference faces.
    const glm::vec3 refForward = modelForward(reference);
    glm::vec3 meshForward(options.forward.x, 0.0f, options.forward.z);
    meshForward = glm::length(meshForward) > 1e-4f ? glm::normalize(meshForward) : glm::vec3(0, 0, 1);
    const float yaw = std::atan2(glm::cross(meshForward, refForward).y, glm::dot(meshForward, refForward));
    const glm::mat3 turn = glm::mat3(glm::rotate(glm::mat4(1.0f), yaw, glm::vec3(0, 1, 0)));
    glm::vec3 lo(1e9f), hi(-1e9f);
    for (ModelMesh& mesh : body.meshes)
        for (ModelVertex& v : mesh.vertices) {
            v.position = turn * v.position;
            v.normal = turn * v.normal;
            lo = glm::min(lo, v.position);
            hi = glm::max(hi, v.position);
        }
    const glm::vec3 up(0, 1, 0);
    const glm::vec3 left = glm::normalize(glm::cross(up, refForward)); // facing +Z, the left is +X
    const float refHeight = reference.boundsMax.y - reference.boundsMin.y > 0.1f ? reference.boundsMax.y - reference.boundsMin.y : refPos[size_t(head)].y * 1.15f;
    const float height = hi.y - lo.y;
    if (height < 0.2f) {
        report.reason = "the model is under 20 cm tall (not in meters?)";
        return report;
    }
    const float s = height / refHeight;
    report.scale = s;
    const glm::vec3 centre((lo.x + hi.x) * 0.5f, lo.y, (lo.z + hi.z) * 0.5f);
    auto lat = [&](const glm::vec3& p) { return glm::dot(p - centre, left); }; // > 0 = her left
    auto scaled = [&](int b) { return centre + glm::vec3(refPos[size_t(b)].x, refPos[size_t(b)].y - reference.boundsMin.y, refPos[size_t(b)].z) * s; };

    // The skin as welded points (UV seams and material borders split
    // vertices that are one point of the skin), and the triangles between them.
    std::vector<glm::vec3> points;
    std::vector<std::vector<uint32_t>> pointOf(body.meshes.size());
    std::unordered_map<uint64_t, uint32_t> weld;
    auto key = [](const glm::vec3& p) {
        auto q = [](float x) { return static_cast<uint64_t>(static_cast<int64_t>(std::lround(x * 1e4f)) & 0x1fffff); }; // 0.1 mm
        return q(p.x) | (q(p.y) << 21) | (q(p.z) << 42);
    };
    for (size_t m = 0; m < body.meshes.size(); ++m)
        for (const ModelVertex& v : body.meshes[m].vertices) {
            auto [it, added] = weld.emplace(key(v.position), static_cast<uint32_t>(points.size()));
            if (added) points.push_back(v.position);
            pointOf[m].push_back(it->second);
        }
    report.points = points.size();

    auto centroid = [&](auto&& keep, glm::vec3& out) {
        glm::dvec3 sum(0.0);
        size_t count = 0;
        for (const glm::vec3& p : points)
            if (keep(p)) {
                sum += glm::dvec3(p);
                ++count;
            }
        if (count < 3) return false;
        out = glm::vec3(sum / static_cast<double>(count));
        return true;
    };
    std::vector<bool> placed(n, false);
    std::vector<glm::vec3> posA(n);
    auto place = [&](int b, const glm::vec3& p) {
        posA[size_t(b)] = p;
        placed[size_t(b)] = true;
    };
    // The points within a slab of a height (or along an arm), widening the
    // slab where the mesh is coarse and none fall inside it.
    auto slabCentroid = [&](auto&& keep, glm::vec3& out) {
        for (float slab : { 0.015f * s, 0.03f * s, 0.06f * s })
            if (centroid([&](const glm::vec3& p) { return keep(p, slab); }, out)) return true;
        return false;
    };

    // 2a. Legs: hip, knee, ankle in the middle of that leg at their height.
    for (int si = 0; si < 2; ++si) {
        const float side = si == 0 ? 1.0f : -1.0f;
        for (int b : { thigh[si], calf[si], foot[si] }) {
            const float y = scaled(b).y;
            glm::vec3 c;
            if (!slabCentroid([&](const glm::vec3& p, float slab) { return lat(p) * side > 0.005f * s && std::abs(lat(p)) < 0.3f * s && std::abs(p.y - y) < slab; }, c)) {
                report.reason = "no leg found at the height of " + reference.bones[size_t(b)].name;
                return report;
            }
            place(b, glm::vec3(c.x, y, c.z));
        }
    }
    // 2b. The spine, neck and head in the middle of the torso.
    for (int b : { pelvis, spine1, spine2, spine3, neck, head }) {
        const glm::vec3 t = scaled(b);
        glm::vec3 c;
        if (!slabCentroid([&](const glm::vec3& p, float slab) { return std::abs(lat(p)) < 0.1f * s && std::abs(p.y - t.y) < slab; }, c)) {
            report.reason = "no torso found at the height of " + reference.bones[size_t(b)].name;
            return report;
        }
        place(b, t + refForward * glm::dot(c - t, refForward));
    }
    // 2c. Arms: each arm's own axis (whatever angle it hangs at), the
    // shoulder where the reference has it, elbow and wrist at the
    // reference's proportions of the arm's length, centred in the arm.
    std::array<glm::vec3, 2> shoulder{}, elbow{}, wrist{};
    for (int si = 0; si < 2; ++si) {
        const float side = si == 0 ? 1.0f : -1.0f;
        const float waist = posA[size_t(spine1)].y;
        auto isArm = [&](const glm::vec3& p) { return lat(p) * side > 0.25f * s && p.y > waist; };
        glm::vec3 c;
        if (!centroid(isArm, c)) {
            report.reason = std::string("no ") + (si == 0 ? "left" : "right") + " arm found away from the body";
            return report;
        }
        // Principal axis of the arm's points (power iteration on the covariance).
        glm::dmat3 cov(0.0);
        for (const glm::vec3& p : points)
            if (isArm(p)) {
                const glm::dvec3 d(p - c);
                cov += glm::outerProduct(d, d);
            }
        glm::dvec3 axis(left * side);
        for (int it = 0; it < 32; ++it) axis = glm::normalize(cov * axis);
        glm::vec3 dir(axis);
        if (glm::dot(dir, left * side) < 0.0f) dir = -dir;
        shoulder[size_t(si)] = c + dir * glm::dot(scaled(upper[si]) - c, dir);
        float reach = 0.0f;
        for (const glm::vec3& p : points)
            if (isArm(p)) reach = std::max(reach, glm::dot(p - shoulder[size_t(si)], dir));
        const glm::vec3 rs = refPos[size_t(upper[si])], re = refPos[size_t(lower[si])], rw = refPos[size_t(hand[si])];
        const float refArm = fingerTip[si] >= 0 ? glm::distance(rs, refPos[size_t(fingerTip[si])]) : (glm::distance(rs, re) + glm::distance(re, rw)) * 1.42f;
        const float fElbow = glm::distance(rs, re) / refArm, fWrist = (glm::distance(rs, re) + glm::distance(re, rw)) / refArm;
        auto ring = [&](float t) {
            glm::vec3 r;
            const glm::vec3 guess = shoulder[size_t(si)] + dir * t;
            if (!slabCentroid([&](const glm::vec3& p, float slab) { return isArm(p) && std::abs(glm::dot(p - shoulder[size_t(si)], dir) - t) < slab; }, r))
                return guess;
            return r;
        };
        elbow[size_t(si)] = ring(reach * fElbow);
        wrist[size_t(si)] = ring(reach * fWrist);
        place(upper[si], shoulder[size_t(si)]);
        place(lower[si], elbow[size_t(si)]);
        place(hand[si], wrist[size_t(si)]);
        report.armDropDegrees[si] = glm::degrees(std::asin(std::clamp(-dir.y, -1.0f, 1.0f)));
    }

    // 3. The A-pose (as modelled) skeleton: reference rotations, turned
    // along her arms; unplaced bones keep their offset from the parent.
    std::vector<glm::quat> adjust(n, glm::quat(1.0f, 0.0f, 0.0f, 0.0f)), rotA(n);
    std::vector<glm::vec3> placedAt = posA;
    for (size_t b = 0; b < n; ++b) {
        const int p = reference.bones[b].parent;
        if (p >= 0) adjust[b] = adjust[size_t(p)]; // children follow the turned arm
        for (int si = 0; si < 2; ++si) {
            if (static_cast<int>(b) == upper[si])
                adjust[b] = fromTo(refPos[size_t(lower[si])] - refPos[size_t(upper[si])], elbow[size_t(si)] - shoulder[size_t(si)]);
            if (static_cast<int>(b) == lower[si])
                adjust[b] = fromTo(refPos[size_t(hand[si])] - refPos[size_t(lower[si])], wrist[size_t(si)] - elbow[size_t(si)]);
        }
        rotA[b] = glm::normalize(adjust[b] * refRot[b]);
        if (placed[b]) continue;
        if (p < 0) posA[b] = scaled(static_cast<int>(b));
        else posA[b] = posA[size_t(p)] + adjust[b] * ((refPos[b] - refPos[size_t(p)]) * s);
    }

    // 4. Skin weights: the nearest bone segments, blended.
    std::vector<Segment> segs;
    auto seg = [&](int bone, const glm::vec3& a, const glm::vec3& b, int side) { segs.push_back({ bone, a, b, side }); };
    const glm::vec3 headTop = posA[size_t(head)] + up * 0.2f * s;
    seg(pelvis, posA[size_t(pelvis)], posA[size_t(spine1)], 0);
    seg(spine1, posA[size_t(spine1)], posA[size_t(spine2)], 0);
    seg(spine2, posA[size_t(spine2)], posA[size_t(spine3)], 0);
    seg(spine3, posA[size_t(spine3)], posA[size_t(neck)], 0);
    seg(neck, posA[size_t(neck)], posA[size_t(head)], 0);
    seg(head, posA[size_t(head)], headTop, 0);
    for (int si = 0; si < 2; ++si) {
        const int side = si == 0 ? 1 : -1;
        const glm::vec3 handEnd = fingerTip[si] >= 0 ? posA[size_t(fingerTip[si])] : posA[size_t(hand[si])] + (wrist[size_t(si)] - elbow[size_t(si)]) * 0.7f;
        const glm::vec3 toeEnd = toeTip[si] >= 0 ? posA[size_t(toeTip[si])] : posA[size_t(ball[si])] + refForward * 0.07f * s;
        seg(clav[si], posA[size_t(clav[si])], posA[size_t(upper[si])], side);
        seg(upper[si], posA[size_t(upper[si])], posA[size_t(lower[si])], side);
        seg(lower[si], posA[size_t(lower[si])], posA[size_t(hand[si])], side);
        seg(hand[si], posA[size_t(hand[si])], handEnd, side);
        seg(thigh[si], posA[size_t(thigh[si])], posA[size_t(calf[si])], side);
        seg(calf[si], posA[size_t(calf[si])], posA[size_t(foot[si])], side);
        seg(foot[si], posA[size_t(foot[si])], posA[size_t(ball[si])], side);
        seg(ball[si], posA[size_t(ball[si])], toeEnd, side);
    }
    const size_t ns = segs.size();
    auto allowed = [&](const glm::vec3& p, const Segment& sg) {
        const float l = lat(p);
        return sg.side == 0 || (sg.side > 0 ? l > -0.005f * s : l < 0.005f * s);
    };
    const float blend = std::max(options.blend, 1e-3f) * s;
    std::vector<float> w(points.size() * ns, 0.0f);
    for (size_t i = 0; i < points.size(); ++i) {
        float best = 1e9f;
        std::vector<float> d(ns, 1e9f);
        for (size_t k = 0; k < ns; ++k)
            if (allowed(points[i], segs[k])) {
                d[k] = segmentDistance(points[i], segs[k].a, segs[k].b);
                best = std::min(best, d[k]);
            }
        for (size_t k = 0; k < ns; ++k) {
            if (d[k] > 1e8f) continue;
            const float x = (d[k] - best) / blend;
            w[i * ns + k] = std::exp(-x * x);
        }
    }
    // Smooth over the surface (welded neighbours), so joints bend softly.
    std::vector<std::vector<uint32_t>> next(points.size());
    for (size_t m = 0; m < body.meshes.size(); ++m) {
        const auto& idx = body.meshes[m].indices;
        for (size_t t = 0; t + 2 < idx.size(); t += 3)
            for (int e = 0; e < 3; ++e) {
                const uint32_t a = pointOf[m][idx[t + size_t(e)]], b = pointOf[m][idx[t + size_t((e + 1) % 3)]];
                if (a == b) continue;
                next[a].push_back(b);
                next[b].push_back(a);
            }
    }
    for (auto& list : next) {
        std::sort(list.begin(), list.end());
        list.erase(std::unique(list.begin(), list.end()), list.end());
    }
    auto normalise = [&](size_t i) {
        float sum = 0.0f;
        for (size_t k = 0; k < ns; ++k) {
            if (!allowed(points[i], segs[k])) w[i * ns + k] = 0.0f;
            sum += w[i * ns + k];
        }
        if (sum > 0.0f)
            for (size_t k = 0; k < ns; ++k) w[i * ns + k] /= sum;
    };
    for (size_t i = 0; i < points.size(); ++i) normalise(i);
    std::vector<float> smoothed(w.size());
    // The face and jaw are the skull: points from just under the head
    // joint up, in front of it, go with the head alone (blended with the
    // neck, the chin would slide over the teeth when the head nods).
    const glm::vec3 skullBase = posA[size_t(head)] - up * 0.01f * s;
    std::vector<bool> skull(points.size(), false);
    for (size_t i = 0; i < points.size(); ++i) {
        const glm::vec3 d = points[i] - skullBase;
        skull[i] = d.y > 0.0f && glm::dot(d, refForward) > 0.035f * s && std::abs(lat(points[i])) < 0.1f * s;
    }
    auto toSkull = [&] {
        for (size_t i = 0; i < points.size(); ++i)
            if (skull[i])
                for (size_t k = 0; k < ns; ++k) w[i * ns + k] = segs[k].bone == head ? 1.0f : 0.0f;
    };
    toSkull();
    for (int pass = 0; pass < options.smoothing; ++pass) {
        for (size_t i = 0; i < points.size(); ++i)
            for (size_t k = 0; k < ns; ++k) {
                float avg = 0.0f;
                for (uint32_t j : next[i]) avg += w[size_t(j) * ns + k];
                smoothed[i * ns + k] = next[i].empty() ? w[i * ns + k] : 0.5f * w[i * ns + k] + 0.5f * avg / static_cast<float>(next[i].size());
            }
        w.swap(smoothed);
        toSkull();
        for (size_t i = 0; i < points.size(); ++i) normalise(i);
    }

    // 5. Into the reference's rest pose: every bone turned as the
    // reference's, her bone lengths kept (the arms come up to a T).
    std::vector<glm::vec3> posT(n);
    std::vector<glm::mat4> worldA(n), worldT(n);
    for (size_t b = 0; b < n; ++b) {
        const int p = reference.bones[b].parent;
        posT[b] = p < 0 ? posA[b] : posT[size_t(p)] + refRot[size_t(p)] * (glm::inverse(rotA[size_t(p)]) * (posA[b] - posA[size_t(p)]));
        worldA[b] = compose(posA[b], rotA[b]);
        worldT[b] = compose(posT[b], refRot[b]);
    }
    std::vector<glm::mat4> repose(n);
    for (size_t b = 0; b < n; ++b) repose[b] = worldT[b] * glm::inverse(worldA[b]);

    // Small parts inside the head (eyes, teeth, tongue) move with it.
    std::vector<bool> rigidHead(body.meshes.size(), false);
    for (size_t m = 0; m < body.meshes.size(); ++m) {
        glm::vec3 mn(1e9f), mx(-1e9f);
        for (const ModelVertex& v : body.meshes[m].vertices) {
            mn = glm::min(mn, v.position);
            mx = glm::max(mx, v.position);
        }
        rigidHead[m] = glm::distance(mn, mx) < 0.15f * s && glm::distance((mn + mx) * 0.5f, posA[size_t(head)] + up * 0.06f * s) < 0.15f * s;
    }

    lo = glm::vec3(1e9f);
    hi = glm::vec3(-1e9f);
    for (size_t m = 0; m < body.meshes.size(); ++m) {
        ModelMesh& mesh = body.meshes[m];
        for (size_t v = 0; v < mesh.vertices.size(); ++v) {
            ModelVertex& vx = mesh.vertices[v];
            const size_t i = pointOf[m][v];
            std::array<std::pair<float, int>, 4> top{};
            for (auto& t : top) t = { 0.0f, 0 };
            if (rigidHead[m]) top[0] = { 1.0f, head };
            else
                for (size_t k = 0; k < ns; ++k) {
                    const float wk = w[i * ns + k];
                    if (wk <= top[3].first) continue;
                    top[3] = { wk, segs[k].bone };
                    std::sort(top.begin(), top.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
                }
            float sum = 0.0f;
            for (const auto& t : top) sum += t.first;
            if (sum <= 0.0f) {
                top[0] = { 1.0f, pelvis };
                sum = 1.0f;
            }
            glm::vec3 pos(0.0f), nrm(0.0f);
            for (int k = 0; k < 4; ++k) {
                vx.joints[k] = static_cast<uint32_t>(top[size_t(k)].second);
                vx.weights[k] = top[size_t(k)].first / sum;
                const glm::mat4& r = repose[size_t(top[size_t(k)].second)];
                pos += vx.weights[k] * glm::vec3(r * glm::vec4(vx.position, 1.0f));
                nrm += vx.weights[k] * (glm::mat3(r) * vx.normal);
            }
            vx.position = pos;
            if (glm::length(nrm) > 1e-6f) vx.normal = glm::normalize(nrm);
            lo = glm::min(lo, pos);
            hi = glm::max(hi, pos);
        }
        mesh.skinned = true;
    }

    body.bones.assign(n, ModelBone{});
    for (size_t b = 0; b < n; ++b) {
        const int p = reference.bones[b].parent;
        body.bones[b].name = reference.bones[b].name;
        body.bones[b].parent = p;
        body.bones[b].localRest = p < 0 ? worldT[b] : glm::inverse(worldT[size_t(p)]) * worldT[b];
        body.bones[b].inverseBind = glm::inverse(worldT[b]);
    }
    body.animations.clear();
    body.boundsMin = lo;
    body.boundsMax = hi;
    report.ok = true;
    return report;
}

} // namespace

AutoRigReport autoRigHumanoid(ModelData& body, const ModelData& reference, const AutoRigOptions& options) {
    // On a copy: a body that can't be rigged is left exactly as it was.
    ModelData work = body;
    AutoRigReport report = rig(work, reference, options);
    if (report.ok) body = std::move(work);
    return report;
}

} // namespace kke
