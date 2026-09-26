#include "kke/Animator.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace kke {

namespace {

BoneTRS decompose(const glm::mat4& m) {
    BoneTRS out;
    glm::vec3 skew;
    glm::vec4 persp;
    if (!glm::decompose(m, out.s, out.r, out.t, skew, persp)) out = BoneTRS{};
    out.r = glm::normalize(out.r);
    return out;
}

// Wraps (looping) or clamps a time into [0, duration].
float clipTime(float t, float duration, bool loop) {
    if (duration <= 0.0f) return 0.0f;
    if (loop) {
        t = std::fmod(t, duration);
        return t < 0.0f ? t + duration : t;
    }
    return std::clamp(t, 0.0f, duration);
}

} // namespace

void blendPoses(const Pose& a, const Pose& b, float w, Pose& out) {
    const size_t n = std::min(a.size(), b.size());
    out.resize(n);
    w = std::clamp(w, 0.0f, 1.0f);
    for (size_t i = 0; i < n; ++i) {
        out[i].t = glm::mix(a[i].t, b[i].t, w);
        out[i].s = glm::mix(a[i].s, b[i].s, w);
        out[i].r = glm::slerp(a[i].r, b[i].r, w); // glm::slerp takes the short way
    }
}

void poseToLocals(const Pose& pose, std::vector<glm::mat4>& locals) {
    locals.resize(pose.size());
    for (size_t i = 0; i < pose.size(); ++i)
        locals[i] = glm::translate(glm::mat4(1.0f), pose[i].t) * glm::mat4_cast(pose[i].r) * glm::scale(glm::mat4(1.0f), pose[i].s);
}

AnimationSet::AnimationSet(const ModelData& model) {
    m_rest.reserve(model.bones.size());
    for (const ModelBone& b : model.bones) m_rest.push_back(decompose(b.localRest));
    for (const ModelAnimation& a : model.animations) {
        Clip c;
        c.name = a.name;
        c.duration = a.duration;
        c.sampleRate = a.sampleRate > 0.0f ? a.sampleRate : 30.0f;
        c.frames.reserve(a.frames.size());
        for (const auto& frame : a.frames) {
            Pose p(frame.size());
            for (size_t b = 0; b < frame.size(); ++b) p[b] = decompose(frame[b]);
            c.frames.push_back(std::move(p));
        }
        m_clips.push_back(std::move(c));
    }
}

int AnimationSet::find(const std::string& part) const {
    for (size_t i = 0; i < m_clips.size(); ++i)
        if (m_clips[i].name.find(part) != std::string::npos) return static_cast<int>(i);
    return -1;
}

void AnimationSet::sample(int clip, float time, bool loop, Pose& out) const {
    if (clip < 0 || clip >= static_cast<int>(m_clips.size()) || m_clips[clip].frames.empty()) {
        out = m_rest;
        return;
    }
    const Clip& c = m_clips[clip];
    const float t = clipTime(time, c.duration, loop);
    const float f = t * c.sampleRate;
    const size_t last = c.frames.size() - 1;
    size_t f0 = std::min(static_cast<size_t>(f), last);
    size_t f1 = f0 + 1;
    if (f1 > last) f1 = loop ? 0 : last;
    blendPoses(c.frames[f0], c.frames[f1], f - static_cast<float>(f0), out);
}

void AnimationSet::extractRootMotion(const ModelData& model, int bone) {
    if (bone < 0 || bone >= static_cast<int>(model.bones.size()) || m_rootBone >= 0) return;
    m_rootBone = bone;
    // The parents' rest transform takes the bone's local travel to model space.
    glm::mat4 parent(1.0f);
    std::vector<int> chain;
    for (int p = model.bones[bone].parent; p >= 0; p = model.bones[p].parent) chain.push_back(p);
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) parent = parent * model.bones[*it].localRest;
    const glm::mat4 toLocal = glm::inverse(parent);
    for (Clip& c : m_clips) {
        c.root.clear();
        if (c.frames.empty()) continue;
        const glm::vec3 start = glm::vec3(parent * glm::vec4(c.frames[0][bone].t, 1.0f));
        for (Pose& f : c.frames) {
            glm::vec3 p = glm::vec3(parent * glm::vec4(f[bone].t, 1.0f));
            const glm::vec3 travel(p.x - start.x, 0.0f, p.z - start.z);
            c.root.push_back(travel);
            f[bone].t = glm::vec3(toLocal * glm::vec4(p - travel, 1.0f));
        }
    }
}

glm::vec3 AnimationSet::rootAt(const Clip& c, float t) const {
    if (c.root.empty()) return glm::vec3(0.0f);
    const float f = t * c.sampleRate;
    const size_t last = c.root.size() - 1;
    const size_t f0 = std::min(static_cast<size_t>(std::max(0.0f, f)), last);
    const size_t f1 = std::min(f0 + 1, last);
    return glm::mix(c.root[f0], c.root[f1], glm::clamp(f - static_cast<float>(f0), 0.0f, 1.0f));
}

glm::vec3 AnimationSet::rootTravel(int clip, float from, float to, bool loop) const {
    if (clip < 0 || clip >= static_cast<int>(m_clips.size()) || m_clips[clip].root.empty()) return glm::vec3(0.0f);
    const Clip& c = m_clips[clip];
    if (!loop || c.duration <= 0.0f) return rootAt(c, clipTime(to, c.duration, false)) - rootAt(c, clipTime(from, c.duration, false));
    const float cycles = std::floor(to / c.duration) - std::floor(from / c.duration);
    return rootAt(c, clipTime(to, c.duration, true)) - rootAt(c, clipTime(from, c.duration, true)) + cycles * c.root.back();
}

Animator::Animator(const AnimationSet& set) : m_set(&set), m_pose(set.restPose()) {}

int Animator::addClipState(const std::string& name, int clip, bool loop, float speed) {
    State s;
    s.name = name;
    s.clip = clip;
    s.loop = loop;
    s.speed = speed;
    m_states.push_back(std::move(s));
    return static_cast<int>(m_states.size()) - 1;
}

int Animator::addBlendState(const std::string& name, BlendSpace1D space, bool loop) {
    std::sort(space.points.begin(), space.points.end(), [](const auto& a, const auto& b) { return a.value < b.value; });
    State s;
    s.name = name;
    s.space = std::move(space);
    s.loop = loop;
    m_states.push_back(std::move(s));
    return static_cast<int>(m_states.size()) - 1;
}

int Animator::findState(const std::string& name) const {
    for (size_t i = 0; i < m_states.size(); ++i) if (m_states[i].name == name) return static_cast<int>(i);
    return -1;
}

void Animator::play(int state, float fade, bool restart) {
    if (state < 0 || state >= static_cast<int>(m_states.size())) return;
    if (state == m_current && !restart) return;
    m_previous = m_current;
    m_prevTime = m_time;
    m_prevPhase = m_phase;
    m_current = state;
    m_time = 0.0f;
    m_phase = 0.0f;
    m_fadeLength = m_previous >= 0 ? std::max(0.0f, fade) : 0.0f;
    m_fade = 0.0f;
}

float Animator::stateDuration(const State& s) const {
    if (s.clip >= 0) return m_set->duration(s.clip) / std::max(1e-3f, s.speed);
    const auto& pts = s.space.points;
    if (pts.empty()) return 0.0f;
    if (m_param <= pts.front().value) return m_set->duration(pts.front().clip);
    if (m_param >= pts.back().value) return m_set->duration(pts.back().clip);
    for (size_t i = 0; i + 1 < pts.size(); ++i) {
        if (m_param > pts[i + 1].value) continue;
        float w = (m_param - pts[i].value) / std::max(1e-6f, pts[i + 1].value - pts[i].value);
        return glm::mix(m_set->duration(pts[i].clip), m_set->duration(pts[i + 1].clip), w);
    }
    return m_set->duration(pts.back().clip);
}

bool Animator::finished() const {
    if (m_current < 0) return true;
    const State& s = m_states[m_current];
    return !s.loop && m_time >= stateDuration(s);
}

void Animator::evaluate(const State& s, float time, float& phase, Pose& out) const {
    if (s.clip >= 0) {
        m_set->sample(s.clip, time * s.speed, s.loop, out);
        return;
    }
    const auto& pts = s.space.points;
    if (pts.empty()) { out = m_set->restPose(); return; }
    // Shared phase: every clip in the space at the same fraction of its cycle.
    auto at = [&](int clip) { return phase * m_set->duration(clip); };
    if (pts.size() == 1 || m_param <= pts.front().value) { m_set->sample(pts.front().clip, at(pts.front().clip), s.loop, out); return; }
    if (m_param >= pts.back().value) { m_set->sample(pts.back().clip, at(pts.back().clip), s.loop, out); return; }
    for (size_t i = 0; i + 1 < pts.size(); ++i) {
        if (m_param > pts[i + 1].value) continue;
        float w = (m_param - pts[i].value) / std::max(1e-6f, pts[i + 1].value - pts[i].value);
        Pose a, b;
        m_set->sample(pts[i].clip, at(pts[i].clip), s.loop, a);
        m_set->sample(pts[i + 1].clip, at(pts[i + 1].clip), s.loop, b);
        blendPoses(a, b, w, out);
        return;
    }
}

void Animator::update(float dt) {
    if (m_current < 0) { m_pose = m_set->restPose(); return; }
    auto advance = [&](const State& s, float& time, float& phase) {
        time += dt;
        const float d = stateDuration(s);
        if (d > 0.0f) {
            phase += dt / d;
            phase = s.loop ? phase - std::floor(phase) : std::min(phase, 1.0f);
        }
    };
    const State& cur = m_states[m_current];
    auto travel = [&](const State& s, float before, float after) {
        return s.clip >= 0 ? m_set->rootTravel(s.clip, before * s.speed, after * s.speed, s.loop) : glm::vec3(0.0f);
    };
    const float curBefore = m_time;
    advance(cur, m_time, m_phase);
    m_rootDelta = travel(cur, curBefore, m_time);
    evaluate(cur, m_time, m_phase, m_scratchA);
    if (m_previous >= 0 && m_fade < m_fadeLength) {
        const State& prev = m_states[m_previous];
        const float prevBefore = m_prevTime;
        advance(prev, m_prevTime, m_prevPhase);
        const glm::vec3 prevDelta = travel(prev, prevBefore, m_prevTime);
        evaluate(prev, m_prevTime, m_prevPhase, m_scratchB);
        m_fade += dt;
        // Smoothstep: no visible "kink" at the start and end of a fade.
        float w = std::clamp(m_fade / std::max(1e-6f, m_fadeLength), 0.0f, 1.0f);
        w = w * w * (3.0f - 2.0f * w);
        blendPoses(m_scratchB, m_scratchA, w, m_pose);
        m_rootDelta = glm::mix(prevDelta, m_rootDelta, w);
    } else {
        m_previous = -1;
        m_pose = m_scratchA;
    }
}

} // namespace kke
