#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace kke {

// Timelines (ENG-F27, docs/TIMELINE.md): Unreal's Sequencer and Unity's
// Timeline as data. A timeline is tracks of keyframes that drive named
// properties of named targets (a door's position, a light's colour, a
// character's yaw), events at times, markers to jump to, spans that switch
// a target on and off, animation clips that start and stop on an actor, and
// whole timelines nested inside it. It plays once, loops or ping-pongs,
// forwards, backwards and at any speed, and it can be scrubbed to any time.
//
// The timeline knows nothing of the engine: it tells a TimelineSink what
// changed ("door.position is now (0, 1.5, 0)", "event 'door_opened'",
// "torch is on", "hero starts clip Walk 0.4 s in") and the game applies it.
// Every time is evaluated from scratch (the state at t is a function of
// t), so scrubbing, reversing and looping need no special cases.
// Unit-tested in tests/test_timeline.cpp.

enum class Ease : uint8_t { Linear, Step, SmoothStep, EaseIn, EaseOut, EaseInOut };
const char* easeName(Ease e);
bool easeFromName(const std::string& name, Ease& out);
// The curve of a segment: 0..1 in, 0..1 out. Step holds 0 until the next key.
float applyEase(Ease e, float t);

enum class ValueKind : uint8_t { Scalar, Vec3, Quat, Color };
const char* valueKindName(ValueKind k);
bool valueKindFromName(const std::string& name, ValueKind& out);
int valueComponents(ValueKind k);

enum class WrapMode : uint8_t { Once, Loop, PingPong };
const char* wrapModeName(WrapMode w);
bool wrapModeFromName(const std::string& name, WrapMode& out);

struct TimelineKey {
    float time = 0.0f;
    glm::vec4 value{ 0.0f }; // a scalar in x, a vector in xyz, a rotation as x y z w, a colour as r g b a
    Ease ease = Ease::Linear; // the curve from this key to the next
};

struct TimelineTrack {
    std::string target, property;
    ValueKind kind = ValueKind::Scalar;
    // Added to what earlier tracks of the same target and property give
    // (a rotation is multiplied) instead of replacing it: a wobble on a path.
    bool additive = false;
    bool muted = false;
    std::vector<TimelineKey> keys; // by time
};

struct TimelineEvent {
    float time = 0.0f;
    std::string name, data;
};

struct TimelineMarker {
    float time = 0.0f;
    std::string name;
};

// A target that is on from `start` to `end` (end < 0: to the end of the timeline).
struct TimelineSpan {
    std::string target;
    float start = 0.0f, end = -1.0f;
};

// An animation clip played by an actor from `start` to `end`.
struct TimelineClip {
    std::string target, clip;
    float start = 0.0f, end = -1.0f;
    bool loop = false;
    float speed = 1.0f;
    float blend = 0.2f; // seconds to fade in
};

class Timeline;
struct TimelineSub {
    std::string name;
    float start = 0.0f;
    float speed = 1.0f; // above 0
    std::shared_ptr<Timeline> timeline;
};

struct ClipEvent {
    enum class Kind : uint8_t { Start, Stop, Seek } kind = Kind::Start;
    std::string target, clip;
    float localTime = 0.0f; // seconds into the clip (already times the clip's speed)
    bool loop = false;
    float speed = 1.0f;
    float blend = 0.2f;
};

// What a timeline tells its game. Every function does nothing unless overridden.
class TimelineSink {
public:
    virtual ~TimelineSink() = default;
    // A property's new value (only when it changed, or all of them after a seek).
    virtual void value(const std::string& target, const std::string& property, ValueKind kind, const glm::vec4& v) {
        (void)target, (void)property, (void)kind, (void)v;
    }
    // Playback crossed an event (once per crossing; scrubbing and reversing fire none).
    virtual void event(const TimelineEvent& e) { (void)e; }
    virtual void marker(const TimelineMarker& m) { (void)m; }
    // A target switched on or off by a span.
    virtual void active(const std::string& target, bool on) { (void)target, (void)on; }
    virtual void clip(const ClipEvent& e) { (void)e; }
    // A timeline that plays once reached its end (or its start, reversed).
    virtual void finished() {}
    virtual void looped(int count) { (void)count; }
};

class Timeline {
public:
    static constexpr size_t kMaxTracks = 4096, kMaxKeys = 200000, kMaxEvents = 100000, kMaxNesting = 8;

    // ---- authoring
    // Returns the track's index. The keys of one track are kept in time order.
    size_t addTrack(const std::string& target, const std::string& property, ValueKind kind = ValueKind::Scalar, bool additive = false);
    // A key at an existing time replaces it.
    void addKey(size_t track, float time, const glm::vec4& value, Ease ease = Ease::Linear);
    void addScalarKey(size_t track, float time, float v, Ease ease = Ease::Linear) { addKey(track, time, glm::vec4(v, 0, 0, 0), ease); }
    void addVec3Key(size_t track, float time, const glm::vec3& v, Ease ease = Ease::Linear) { addKey(track, time, glm::vec4(v, 0), ease); }
    void addQuatKey(size_t track, float time, const glm::quat& q, Ease ease = Ease::Linear) { addKey(track, time, glm::vec4(q.x, q.y, q.z, q.w), ease); }
    void addEvent(float time, const std::string& name, const std::string& data = {});
    void addMarker(float time, const std::string& name);
    void addSpan(const std::string& target, float start, float end = -1.0f);
    void addClip(const TimelineClip& clip);
    // Nested timelines play inside this one from `start`, at `speed`.
    bool addSub(float start, std::shared_ptr<Timeline> timeline, float speed = 1.0f, const std::string& name = {});

    const std::vector<TimelineTrack>& tracks() const { return m_tracks; }
    TimelineTrack* track(size_t i) { return i < m_tracks.size() ? &m_tracks[i] : nullptr; }
    const std::vector<TimelineEvent>& events() const { return m_events; }
    const std::vector<TimelineMarker>& markers() const { return m_markers; }
    const std::vector<TimelineSpan>& spans() const { return m_spans; }
    const std::vector<TimelineClip>& clips() const { return m_clips; }
    const std::vector<TimelineSub>& subs() const { return m_subs; }

    // Seconds long: the length set, or where the last key, event, span, clip
    // or nested timeline ends.
    float duration() const;
    void setDuration(float seconds) { m_duration = seconds > 0.0f ? seconds : 0.0f; }
    WrapMode wrap = WrapMode::Once;

    // ---- the values at a time, without playing
    struct State;
    // Calls sink.value for every property at `t` (and active / clip for the spans and clips there).
    void evaluate(float t, TimelineSink& sink) const;

    // ---- playback
    // Starts at `from` seconds (default: where it is; a timeline that has
    // played to its end starts over).
    void play(float from = -1.0f);
    void pause() { m_playing = false; }
    void resume() { m_playing = true; }
    // Back to 0, nothing playing. With a sink, what is on at 0 is applied
    // now (spans that were on go off, clips stop); without, at the next update or seek.
    void stop(TimelineSink* sink = nullptr);
    // Moves the playhead. Everything is applied afresh (clips get Seek events). Events fire only when
    // `fireEvents` is set and the move is forwards.
    void seek(float t, TimelineSink& sink, bool fireEvents = false);
    // Jumps to a marker: false when there is none of that name.
    bool seekToMarker(const std::string& name, TimelineSink& sink, bool fireEvents = false);
    void update(float dt, TimelineSink& sink);

    bool playing() const { return m_playing; }
    bool finished() const { return m_finished; }
    float time() const { return m_time; }
    int loops() const { return m_loops; }
    // Negative plays backwards.
    float speed = 1.0f;

    // ---- data
    nlohmann::json toJson() const;
    // False with a path to the mistake ("tracks[1].keys[0]: needs t").
    static bool fromJson(const nlohmann::json& j, Timeline& out, std::string* error = nullptr);

    Timeline();
    ~Timeline();
    Timeline(const Timeline&);
    Timeline& operator=(const Timeline&);
    Timeline(Timeline&&) noexcept;
    Timeline& operator=(Timeline&&) noexcept;

private:
    // valuesOnly: a nested timeline that has ended keeps its last values, but its spans, clips and events are over.
    void collect(float t, State& state, int depth, bool valuesOnly = false) const;
    void fireRange(float from, float to, bool includeStart, TimelineSink& sink, int depth) const;
    void apply(float t, TimelineSink& sink, bool force, bool seeking);

    std::vector<TimelineTrack> m_tracks;
    std::vector<TimelineEvent> m_events;
    std::vector<TimelineMarker> m_markers;
    std::vector<TimelineSpan> m_spans;
    std::vector<TimelineClip> m_clips;
    std::vector<TimelineSub> m_subs;
    float m_duration = 0.0f;

    float m_time = 0.0f;
    bool m_playing = false, m_finished = false;
    bool m_fireAtStart = true; // events at exactly the time playback starts from
    bool m_force = true;       // the next apply sends everything
    int m_dir = 1;             // ping-pong direction
    int m_loops = 0;
    std::unique_ptr<State> m_last;
};

} // namespace kke
