#include "kke/Timeline.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <functional>

namespace kke {

using nlohmann::json;

const char* easeName(Ease e) {
    switch (e) {
    case Ease::Linear: return "linear";
    case Ease::Step: return "step";
    case Ease::SmoothStep: return "smooth";
    case Ease::EaseIn: return "in";
    case Ease::EaseOut: return "out";
    case Ease::EaseInOut: return "inout";
    }
    return "linear";
}

bool easeFromName(const std::string& n, Ease& out) {
    static const std::pair<const char*, Ease> table[] = { { "linear", Ease::Linear }, { "step", Ease::Step }, { "smooth", Ease::SmoothStep },
                                                          { "smoothstep", Ease::SmoothStep }, { "in", Ease::EaseIn }, { "out", Ease::EaseOut },
                                                          { "inout", Ease::EaseInOut } };
    for (const auto& [name, e] : table)
        if (n == name) {
            out = e;
            return true;
        }
    return false;
}

float applyEase(Ease e, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    switch (e) {
    case Ease::Linear: return t;
    case Ease::Step: return 0.0f;
    case Ease::SmoothStep: return t * t * (3.0f - 2.0f * t);
    case Ease::EaseIn: return t * t;
    case Ease::EaseOut: return 1.0f - (1.0f - t) * (1.0f - t);
    case Ease::EaseInOut: return t < 0.5f ? 4.0f * t * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) * 0.5f;
    }
    return t;
}

const char* valueKindName(ValueKind k) {
    switch (k) {
    case ValueKind::Scalar: return "scalar";
    case ValueKind::Vec3: return "vec3";
    case ValueKind::Quat: return "quat";
    case ValueKind::Color: return "color";
    }
    return "scalar";
}

bool valueKindFromName(const std::string& n, ValueKind& out) {
    static const std::pair<const char*, ValueKind> table[] = { { "scalar", ValueKind::Scalar }, { "number", ValueKind::Scalar }, { "vec3", ValueKind::Vec3 },
                                                               { "quat", ValueKind::Quat }, { "color", ValueKind::Color } };
    for (const auto& [name, k] : table)
        if (n == name) {
            out = k;
            return true;
        }
    return false;
}

int valueComponents(ValueKind k) {
    switch (k) {
    case ValueKind::Scalar: return 1;
    case ValueKind::Vec3: return 3;
    case ValueKind::Quat: return 4;
    case ValueKind::Color: return 4;
    }
    return 1;
}

const char* wrapModeName(WrapMode w) {
    switch (w) {
    case WrapMode::Once: return "once";
    case WrapMode::Loop: return "loop";
    case WrapMode::PingPong: return "pingpong";
    }
    return "once";
}

bool wrapModeFromName(const std::string& n, WrapMode& out) {
    if (n == "once") out = WrapMode::Once;
    else if (n == "loop") out = WrapMode::Loop;
    else if (n == "pingpong" || n == "ping-pong") out = WrapMode::PingPong;
    else return false;
    return true;
}

// ---- the state at a time ---------------------------------------------------------

struct Timeline::State {
    struct Value {
        ValueKind kind = ValueKind::Scalar;
        glm::vec4 v{ 0.0f };
    };
    struct ClipState {
        std::string target, clip;
        float local = 0.0f;
        bool loop = false;
        float speed = 1.0f, blend = 0.2f;
    };
    std::map<std::pair<std::string, std::string>, Value> values;
    std::set<std::string> active;
    std::map<std::string, ClipState> clips; // by target, clip and start
};

Timeline::Timeline() = default;
Timeline::~Timeline() = default;
Timeline::Timeline(Timeline&&) noexcept = default;
Timeline& Timeline::operator=(Timeline&&) noexcept = default;

Timeline::Timeline(const Timeline& o)
    : wrap(o.wrap), speed(o.speed), m_tracks(o.m_tracks), m_events(o.m_events), m_markers(o.m_markers), m_spans(o.m_spans), m_clips(o.m_clips),
      m_subs(o.m_subs), m_duration(o.m_duration), m_time(o.m_time), m_playing(o.m_playing), m_finished(o.m_finished), m_fireAtStart(o.m_fireAtStart),
      m_dir(o.m_dir), m_loops(o.m_loops) {
    for (TimelineSub& s : m_subs)
        if (s.timeline) s.timeline = std::make_shared<Timeline>(*s.timeline); // nested timelines are copied, not shared
}

Timeline& Timeline::operator=(const Timeline& o) {
    if (this != &o) {
        Timeline copy(o);
        *this = std::move(copy);
    }
    return *this;
}

// ---- authoring ---------------------------------------------------------------------

size_t Timeline::addTrack(const std::string& target, const std::string& property, ValueKind kind, bool additive) {
    if (m_tracks.size() >= kMaxTracks) return m_tracks.size() - 1;
    TimelineTrack t;
    t.target = target;
    t.property = property;
    t.kind = kind;
    t.additive = additive;
    m_tracks.push_back(std::move(t));
    return m_tracks.size() - 1;
}

void Timeline::addKey(size_t track, float time, const glm::vec4& value, Ease ease) {
    if (track >= m_tracks.size() || !std::isfinite(time)) return;
    std::vector<TimelineKey>& keys = m_tracks[track].keys;
    TimelineKey k;
    k.time = std::max(time, 0.0f);
    k.value = value;
    k.ease = ease;
    const auto at = std::lower_bound(keys.begin(), keys.end(), k.time, [](const TimelineKey& a, float t) { return a.time < t; });
    if (at != keys.end() && at->time == k.time) *at = k;
    else keys.insert(at, k);
}

void Timeline::addEvent(float time, const std::string& name, const std::string& data) {
    if (m_events.size() >= kMaxEvents || !std::isfinite(time)) return;
    TimelineEvent e{ std::max(time, 0.0f), name, data };
    const auto at = std::upper_bound(m_events.begin(), m_events.end(), e.time, [](float t, const TimelineEvent& x) { return t < x.time; });
    m_events.insert(at, std::move(e)); // after others at the same time: they keep the order they were added in
}

void Timeline::addMarker(float time, const std::string& name) {
    if (m_markers.size() >= kMaxEvents || !std::isfinite(time)) return;
    TimelineMarker m{ std::max(time, 0.0f), name };
    const auto at = std::upper_bound(m_markers.begin(), m_markers.end(), m.time, [](float t, const TimelineMarker& x) { return t < x.time; });
    m_markers.insert(at, std::move(m));
}

void Timeline::addSpan(const std::string& target, float start, float end) {
    if (m_spans.size() >= kMaxEvents || !std::isfinite(start) || !std::isfinite(end)) return;
    m_spans.push_back({ target, std::max(start, 0.0f), end });
}

void Timeline::addClip(const TimelineClip& clip) {
    if (m_clips.size() >= kMaxEvents || !std::isfinite(clip.start) || !std::isfinite(clip.end)) return;
    TimelineClip c = clip;
    c.start = std::max(c.start, 0.0f);
    if (!(c.speed > 0.0f) || !std::isfinite(c.speed)) c.speed = 1.0f;
    m_clips.push_back(std::move(c));
}

bool Timeline::addSub(float start, std::shared_ptr<Timeline> timeline, float speed, const std::string& name) {
    if (!timeline || !(speed > 0.0f) || !std::isfinite(speed) || !std::isfinite(start) || timeline.get() == this) return false;
    // a timeline inside itself, however deep, would never end
    std::vector<const Timeline*> stack{ timeline.get() };
    size_t depth = 0;
    while (!stack.empty()) {
        const Timeline* t = stack.back();
        stack.pop_back();
        if (t == this || ++depth > 4096) return false;
        for (const TimelineSub& s : t->m_subs)
            if (s.timeline) stack.push_back(s.timeline.get());
    }
    m_subs.push_back({ name, std::max(start, 0.0f), speed, std::move(timeline) });
    return true;
}

float Timeline::duration() const {
    if (m_duration > 0.0f) return m_duration;
    float end = 0.0f;
    for (const TimelineTrack& t : m_tracks)
        if (!t.keys.empty()) end = std::max(end, t.keys.back().time);
    for (const TimelineEvent& e : m_events) end = std::max(end, e.time);
    for (const TimelineMarker& m : m_markers) end = std::max(end, m.time);
    for (const TimelineSpan& s : m_spans) end = std::max(end, std::max(s.start, s.end));
    for (const TimelineClip& c : m_clips) end = std::max(end, std::max(c.start, c.end));
    for (const TimelineSub& s : m_subs)
        if (s.timeline) end = std::max(end, s.start + s.timeline->duration() / s.speed);
    return end;
}

// ---- evaluation --------------------------------------------------------------------

namespace {

glm::vec4 evalTrack(const TimelineTrack& track, float t) {
    const std::vector<TimelineKey>& k = track.keys;
    if (t <= k.front().time) return k.front().value;
    if (t >= k.back().time) return k.back().value;
    const auto i1 = std::upper_bound(k.begin(), k.end(), t, [](float x, const TimelineKey& key) { return x < key.time; });
    const TimelineKey& b = *i1;
    const TimelineKey& a = *(i1 - 1);
    const float u = applyEase(a.ease, (t - a.time) / (b.time - a.time));
    if (track.kind == ValueKind::Quat) {
        const glm::quat qa(a.value.w, a.value.x, a.value.y, a.value.z), qb(b.value.w, b.value.x, b.value.y, b.value.z);
        const glm::quat q = glm::normalize(glm::slerp(glm::normalize(qa), glm::normalize(qb), u));
        return { q.x, q.y, q.z, q.w };
    }
    return glm::mix(a.value, b.value, u);
}

} // namespace

void Timeline::collect(float t, State& state, int depth, bool valuesOnly) const {
    for (const TimelineTrack& track : m_tracks) {
        if (track.muted || track.keys.empty()) continue;
        const glm::vec4 v = evalTrack(track, t);
        const auto key = std::make_pair(track.target, track.property);
        auto it = state.values.find(key);
        if (!track.additive || it == state.values.end()) {
            state.values[key] = { track.kind, v };
        } else if (track.kind == ValueKind::Quat) {
            const glm::quat base(it->second.v.w, it->second.v.x, it->second.v.y, it->second.v.z), add(v.w, v.x, v.y, v.z);
            const glm::quat q = glm::normalize(base * add);
            it->second.v = { q.x, q.y, q.z, q.w };
        } else {
            it->second.v += v;
        }
    }
    if (valuesOnly) return; // the sub-timelines of one that has ended would only add values too: they hold their own last
    for (const TimelineSpan& s : m_spans)
        if (t >= s.start && (s.end < 0.0f || t < s.end)) state.active.insert(s.target);
    for (const TimelineClip& c : m_clips) {
        if (!(t >= c.start && (c.end < 0.0f || t < c.end))) continue;
        State::ClipState cs;
        cs.target = c.target;
        cs.clip = c.clip;
        cs.local = (t - c.start) * c.speed;
        cs.loop = c.loop;
        cs.speed = c.speed;
        cs.blend = c.blend;
        state.clips[c.target + "\n" + c.clip + "\n" + std::to_string(c.start)] = std::move(cs);
    }
    if (depth + 1 > static_cast<int>(kMaxNesting)) return;
    for (const TimelineSub& s : m_subs) {
        if (!s.timeline) continue;
        const float local = (t - s.start) * s.speed;
        const float length = s.timeline->duration();
        if (local < 0.0f) continue;
        if (local <= length) s.timeline->collect(local, state, depth + 1);
        else s.timeline->collect(length, state, depth + 1, /*valuesOnly=*/true); // over: holds its last values
    }
}

void Timeline::fireRange(float from, float to, bool includeStart, TimelineSink& sink, int depth) const {
    auto crossed = [&](float time) { return (time > from || (includeStart && time == from)) && time <= to; };
    for (const TimelineEvent& e : m_events)
        if (crossed(e.time)) sink.event(e);
    for (const TimelineMarker& m : m_markers)
        if (crossed(m.time)) sink.marker(m);
    if (depth + 1 > static_cast<int>(kMaxNesting)) return;
    for (const TimelineSub& s : m_subs) {
        if (!s.timeline) continue;
        const float la = (from - s.start) * s.speed, lb = (to - s.start) * s.speed;
        const float subDuration = s.timeline->duration();
        if (lb < 0.0f || la > subDuration) continue;
        const bool include = la < 0.0f || (includeStart && la == 0.0f);
        s.timeline->fireRange(std::max(la, 0.0f), std::min(lb, subDuration), include, sink, depth + 1);
    }
}

void Timeline::evaluate(float t, TimelineSink& sink) const {
    State now;
    collect(std::clamp(t, 0.0f, duration()), now, 0);
    for (const auto& [key, v] : now.values) sink.value(key.first, key.second, v.kind, v.v);
    for (const std::string& a : now.active) sink.active(a, true);
    for (const auto& [k, c] : now.clips) {
        ClipEvent e;
        e.kind = ClipEvent::Kind::Seek;
        e.target = c.target;
        e.clip = c.clip;
        e.localTime = c.local;
        e.loop = c.loop;
        e.speed = c.speed;
        e.blend = c.blend;
        sink.clip(e);
    }
}

void Timeline::apply(float t, TimelineSink& sink, bool force, bool seeking) {
    State now;
    collect(t, now, 0);
    if (!m_last) m_last = std::make_unique<State>();
    State& last = *m_last;
    const bool full = force || m_force;
    m_force = false;
    for (const auto& [key, v] : now.values) {
        const auto it = last.values.find(key);
        if (full || it == last.values.end() || it->second.v != v.v) sink.value(key.first, key.second, v.kind, v.v);
    }
    for (const std::string& a : now.active)
        if (full || !last.active.count(a)) sink.active(a, true);
    for (const std::string& a : last.active)
        if (!now.active.count(a)) sink.active(a, false);
    auto clipEvent = [](ClipEvent::Kind kind, const State::ClipState& c) {
        ClipEvent e;
        e.kind = kind;
        e.target = c.target;
        e.clip = c.clip;
        e.localTime = c.local;
        e.loop = c.loop;
        e.speed = c.speed;
        e.blend = c.blend;
        return e;
    };
    for (const auto& [k, c] : now.clips) {
        if (!last.clips.count(k)) sink.clip(clipEvent(ClipEvent::Kind::Start, c));
        else if (seeking) sink.clip(clipEvent(ClipEvent::Kind::Seek, c));
    }
    for (const auto& [k, c] : last.clips)
        if (!now.clips.count(k)) sink.clip(clipEvent(ClipEvent::Kind::Stop, c));
    last = std::move(now);
}

// ---- playback ----------------------------------------------------------------------

void Timeline::play(float from) {
    const float dur = duration();
    if (from >= 0.0f) m_time = std::min(from, dur);
    else if (m_finished && speed > 0.0f && m_time >= dur) m_time = 0.0f;
    else if (m_finished && speed < 0.0f && m_time <= 0.0f) m_time = dur;
    m_playing = true;
    m_finished = false;
    m_fireAtStart = m_time == 0.0f;
    m_force = true;
}

void Timeline::stop(TimelineSink* sink) {
    m_playing = false;
    m_finished = false;
    m_time = 0.0f;
    m_dir = 1;
    m_loops = 0;
    m_fireAtStart = true;
    m_force = true;
    if (sink) apply(0.0f, *sink, true, true);
}

void Timeline::seek(float t, TimelineSink& sink, bool fireEvents) {
    const float dur = duration();
    t = std::clamp(std::isfinite(t) ? t : 0.0f, 0.0f, dur);
    if (fireEvents && t > m_time) fireRange(m_time, t, false, sink, 0);
    m_time = t;
    m_fireAtStart = t == 0.0f;
    apply(t, sink, true, true);
}

bool Timeline::seekToMarker(const std::string& name, TimelineSink& sink, bool fireEvents) {
    for (const TimelineMarker& m : m_markers)
        if (m.name == name) {
            seek(m.time, sink, fireEvents);
            return true;
        }
    return false;
}

void Timeline::update(float dt, TimelineSink& sink) {
    if (!m_playing || !(dt > 0.0f)) return;
    const float dur = duration();
    bool include = m_fireAtStart;
    m_fireAtStart = false;
    if (dur <= 0.0f) { // nothing to play but what sits at time 0
        fireRange(0.0f, 0.0f, include, sink, 0);
        apply(0.0f, sink, false, false);
        if (wrap == WrapMode::Once) {
            m_playing = false;
            m_finished = true;
            sink.finished();
        }
        return;
    }
    double v = static_cast<double>(dt) * static_cast<double>(speed) * m_dir;
    for (int guard = 0; guard < 16; ++guard) {
        if (v >= 0.0) { // time moves forwards: events fire
            const double target = static_cast<double>(m_time) + v;
            if (target <= dur) {
                fireRange(m_time, static_cast<float>(target), include, sink, 0);
                m_time = static_cast<float>(target);
                break;
            }
            fireRange(m_time, dur, include, sink, 0);
            include = false;
            const double left = target - dur;
            if (wrap == WrapMode::Once) {
                m_time = dur;
                m_playing = false;
                m_finished = true;
                apply(m_time, sink, false, false);
                sink.finished();
                return;
            }
            ++m_loops;
            sink.looped(m_loops);
            if (wrap == WrapMode::Loop) {
                m_time = 0.0f;
                include = true;
                v = left;
            } else {
                m_time = dur;
                m_dir = -m_dir;
                v = -left;
            }
        } else { // backwards: nothing fires
            const double target = static_cast<double>(m_time) + v;
            if (target >= 0.0) {
                m_time = static_cast<float>(target);
                break;
            }
            if (wrap == WrapMode::Once) {
                m_time = 0.0f;
                m_playing = false;
                m_finished = true;
                apply(m_time, sink, false, false);
                sink.finished();
                return;
            }
            ++m_loops;
            sink.looped(m_loops);
            if (wrap == WrapMode::Loop) {
                m_time = dur;
                v = target;
            } else {
                m_time = 0.0f;
                m_dir = -m_dir;
                include = true;
                v = -target;
            }
        }
    }
    apply(m_time, sink, false, false);
}

// ---- data --------------------------------------------------------------------------

json Timeline::toJson() const {
    json j = json::object();
    if (m_duration > 0.0f) j["duration"] = m_duration;
    if (wrap != WrapMode::Once) j["wrap"] = wrapModeName(wrap);
    if (speed != 1.0f) j["speed"] = speed;
    auto value = [](ValueKind kind, const glm::vec4& v) {
        switch (kind) {
        case ValueKind::Scalar: return json(v.x);
        case ValueKind::Vec3: return json::array({ v.x, v.y, v.z });
        default: return json::array({ v.x, v.y, v.z, v.w });
        }
    };
    if (!m_tracks.empty()) {
        json tracks = json::array();
        for (const TimelineTrack& t : m_tracks) {
            json tj = { { "target", t.target }, { "property", t.property }, { "kind", valueKindName(t.kind) } };
            if (t.additive) tj["additive"] = true;
            if (t.muted) tj["muted"] = true;
            json keys = json::array();
            for (const TimelineKey& k : t.keys) {
                json kj = { { "t", k.time }, { "v", value(t.kind, k.value) } };
                if (k.ease != Ease::Linear) kj["ease"] = easeName(k.ease);
                keys.push_back(std::move(kj));
            }
            tj["keys"] = std::move(keys);
            tracks.push_back(std::move(tj));
        }
        j["tracks"] = std::move(tracks);
    }
    if (!m_events.empty()) {
        j["events"] = json::array();
        for (const TimelineEvent& e : m_events) {
            json ej = { { "t", e.time }, { "name", e.name } };
            if (!e.data.empty()) ej["data"] = e.data;
            j["events"].push_back(std::move(ej));
        }
    }
    if (!m_markers.empty()) {
        j["markers"] = json::array();
        for (const TimelineMarker& m : m_markers) j["markers"].push_back({ { "t", m.time }, { "name", m.name } });
    }
    if (!m_spans.empty()) {
        j["spans"] = json::array();
        for (const TimelineSpan& s : m_spans) {
            json sj = { { "target", s.target }, { "start", s.start } };
            if (s.end >= 0.0f) sj["end"] = s.end;
            j["spans"].push_back(std::move(sj));
        }
    }
    if (!m_clips.empty()) {
        j["clips"] = json::array();
        for (const TimelineClip& c : m_clips) {
            json cj = { { "target", c.target }, { "clip", c.clip }, { "start", c.start } };
            if (c.end >= 0.0f) cj["end"] = c.end;
            if (c.loop) cj["loop"] = true;
            if (c.speed != 1.0f) cj["speed"] = c.speed;
            if (c.blend != 0.2f) cj["blend"] = c.blend;
            j["clips"].push_back(std::move(cj));
        }
    }
    if (!m_subs.empty()) {
        j["subs"] = json::array();
        for (const TimelineSub& s : m_subs) {
            json sj = { { "start", s.start }, { "timeline", s.timeline ? s.timeline->toJson() : json::object() } };
            if (!s.name.empty()) sj["name"] = s.name;
            if (s.speed != 1.0f) sj["speed"] = s.speed;
            j["subs"].push_back(std::move(sj));
        }
    }
    return j;
}

namespace {

bool flag(const json& o, const char* key) {
    const auto it = o.find(key);
    return it != o.end() && it->is_boolean() && it->get<bool>();
}

struct Reader {
    std::string* error;
    size_t keys = 0, events = 0;
    bool fail(const std::string& path, const std::string& why) {
        if (error && error->empty()) *error = path + ": " + why;
        return false;
    }
    bool number(const json& o, const char* key, bool required, float fallback, float& out, const std::string& path) {
        const auto it = o.find(key);
        if (it == o.end()) {
            if (required) return fail(path, std::string("needs ") + key);
            out = fallback;
            return true;
        }
        if (!it->is_number() || !std::isfinite(it->get<double>())) return fail(path, std::string(key) + " must be a number");
        out = it->get<float>();
        return true;
    }
    bool text(const json& o, const char* key, bool required, std::string& out, const std::string& path) {
        const auto it = o.find(key);
        if (it == o.end()) {
            if (required) return fail(path, std::string("needs ") + key);
            return true;
        }
        if (!it->is_string() || it->get<std::string>().size() > 256) return fail(path, std::string(key) + " must be text (at most 256 characters)");
        out = it->get<std::string>();
        return true;
    }
    bool vec(const json& v, ValueKind kind, glm::vec4& out, const std::string& path) {
        const int n = valueComponents(kind);
        if (kind == ValueKind::Scalar && v.is_number()) {
            out = glm::vec4(v.get<float>(), 0, 0, 0);
            return std::isfinite(out.x) || fail(path, "not a finite number");
        }
        if (!v.is_array() || (static_cast<int>(v.size()) != n && !(kind == ValueKind::Color && v.size() == 3)))
            return fail(path, std::string("a ") + valueKindName(kind) + " value is " + (n == 1 ? "a number" : "a list of " + std::to_string(n) + " numbers"));
        out = glm::vec4(0.0f, 0.0f, 0.0f, kind == ValueKind::Color ? 1.0f : 0.0f);
        for (size_t i = 0; i < v.size(); ++i) {
            if (!v[i].is_number() || !std::isfinite(v[i].get<double>())) return fail(path, "values are finite numbers");
            out[static_cast<int>(i)] = v[i].get<float>();
        }
        return true;
    }

    bool timeline(const json& j, Timeline& out, const std::string& path, int depth) {
        if (!j.is_object()) return fail(path, "a timeline is an object");
        if (depth > static_cast<int>(Timeline::kMaxNesting)) return fail(path, "timelines nested too deep");
        float f = 0.0f;
        if (!number(j, "duration", false, 0.0f, f, path)) return false;
        out.setDuration(f);
        if (const auto w = j.find("wrap"); w != j.end()) {
            if (!w->is_string() || !wrapModeFromName(w->get<std::string>(), out.wrap)) return fail(path, "wrap is \"once\", \"loop\" or \"pingpong\"");
        }
        if (!number(j, "speed", false, 1.0f, out.speed, path)) return false;
        if (const auto tr = j.find("tracks"); tr != j.end() && !(tr->is_object() && tr->empty())) {
            if (!tr->is_array()) return fail(path, "tracks is a list");
            if (tr->size() > Timeline::kMaxTracks) return fail(path, "too many tracks");
            for (size_t i = 0; i < tr->size(); ++i) {
                const json& t = (*tr)[i];
                const std::string tp = path + ".tracks[" + std::to_string(i) + "]";
                if (!t.is_object()) return fail(tp, "a track is an object");
                std::string target, property, kindName = "scalar";
                if (!text(t, "target", true, target, tp) || !text(t, "property", true, property, tp) || !text(t, "kind", false, kindName, tp)) return false;
                ValueKind kind;
                if (!valueKindFromName(kindName, kind)) return fail(tp, "kind is scalar, vec3, quat or color");
                const bool additive = flag(t, "additive"), muted = flag(t, "muted");
                const size_t index = out.addTrack(target, property, kind, additive);
                out.track(index)->muted = muted;
                const auto ks = t.find("keys");
                if (ks == t.end() || !(ks->is_array() || (ks->is_object() && ks->empty()))) return fail(tp, "needs keys (a list)");
                for (size_t k = 0; k < ks->size(); ++k) {
                    const json& key = (*ks)[k];
                    const std::string kp = tp + ".keys[" + std::to_string(k) + "]";
                    if (!key.is_object()) return fail(kp, "a key is an object {t, v}");
                    float time = 0.0f;
                    glm::vec4 value;
                    if (!number(key, "t", true, 0.0f, time, kp)) return false;
                    if (!key.contains("v")) return fail(kp, "needs v");
                    if (!vec(key["v"], kind, value, kp + ".v")) return false;
                    Ease ease = Ease::Linear;
                    if (const auto e = key.find("ease"); e != key.end()) {
                        if (!e->is_string() || !easeFromName(e->get<std::string>(), ease)) return fail(kp, "ease is linear, step, smooth, in, out or inout");
                    }
                    if (++keys > Timeline::kMaxKeys) return fail(kp, "too many keys");
                    out.addKey(index, time, value, ease);
                }
            }
        }
        auto list = [&](const char* name, const std::function<bool(const json&, const std::string&)>& each) {
            const auto it = j.find(name);
            if (it == j.end() || (it->is_object() && it->empty())) return true; // an empty Lua table arrives as {}
            if (!it->is_array()) return fail(path, std::string(name) + " is a list");
            if (it->size() > Timeline::kMaxEvents) return fail(path, std::string("too many ") + name);
            for (size_t i = 0; i < it->size(); ++i)
                if (!each((*it)[i], path + "." + name + "[" + std::to_string(i) + "]")) return false;
            return true;
        };
        if (!list("events", [&](const json& e, const std::string& p) {
                if (!e.is_object()) return fail(p, "an event is an object {t, name}");
                float t = 0.0f;
                std::string name, data;
                if (!number(e, "t", true, 0.0f, t, p) || !text(e, "name", true, name, p) || !text(e, "data", false, data, p)) return false;
                out.addEvent(t, name, data);
                return true;
            }))
            return false;
        if (!list("markers", [&](const json& e, const std::string& p) {
                if (!e.is_object()) return fail(p, "a marker is an object {t, name}");
                float t = 0.0f;
                std::string name;
                if (!number(e, "t", true, 0.0f, t, p) || !text(e, "name", true, name, p)) return false;
                out.addMarker(t, name);
                return true;
            }))
            return false;
        if (!list("spans", [&](const json& e, const std::string& p) {
                if (!e.is_object()) return fail(p, "a span is an object {target, start, end}");
                float s = 0.0f, en = -1.0f;
                std::string target;
                if (!text(e, "target", true, target, p) || !number(e, "start", true, 0.0f, s, p) || !number(e, "end", false, -1.0f, en, p)) return false;
                out.addSpan(target, s, en);
                return true;
            }))
            return false;
        if (!list("clips", [&](const json& e, const std::string& p) {
                if (!e.is_object()) return fail(p, "a clip is an object {target, clip, start}");
                TimelineClip c;
                if (!text(e, "target", true, c.target, p) || !text(e, "clip", true, c.clip, p) || !number(e, "start", true, 0.0f, c.start, p) ||
                    !number(e, "end", false, -1.0f, c.end, p) || !number(e, "speed", false, 1.0f, c.speed, p) || !number(e, "blend", false, 0.2f, c.blend, p))
                    return false;
                if (!(c.speed > 0.0f)) return fail(p, "speed must be above 0");
                c.loop = flag(e, "loop");
                out.addClip(c);
                return true;
            }))
            return false;
        return list("subs", [&](const json& e, const std::string& p) {
            if (!e.is_object()) return fail(p, "a nested timeline is an object {start, timeline}");
            float s = 0.0f, sp = 1.0f;
            std::string name;
            if (!number(e, "start", true, 0.0f, s, p) || !number(e, "speed", false, 1.0f, sp, p) || !text(e, "name", false, name, p)) return false;
            if (!(sp > 0.0f)) return fail(p, "speed must be above 0");
            const auto sub = e.find("timeline");
            if (sub == e.end()) return fail(p, "needs timeline");
            auto child = std::make_shared<Timeline>();
            if (!timeline(*sub, *child, p + ".timeline", depth + 1)) return false;
            return out.addSub(s, std::move(child), sp, name) || fail(p, "can't be added");
        });
    }
};

} // namespace

bool Timeline::fromJson(const json& j, Timeline& out, std::string* error) {
    if (error) error->clear();
    std::string scratch;
    Reader r{ error ? error : &scratch };
    Timeline t;
    if (!r.timeline(j, t, "timeline", 0)) return false;
    out = std::move(t);
    return true;
}

} // namespace kke
