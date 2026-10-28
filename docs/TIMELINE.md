# Timelines

Cutscenes, scripted sequences, animated set pieces and boss intros are the same
thing: things that change over time, on a clock you can play, pause, scrub and
reverse. `kke::Timeline` (`kke/Timeline.h`, ENG-F27) is that clock with tracks on
it: Unreal's Sequencer and Unity's Timeline as plain data, with Lua as
`timeline.*`. (The camera has its own director for flying through shots:
[cameras](cookbook/cameras.md#cutscenes-in-any-game); a timeline can drive it.)

A timeline holds:

| Part | Is |
|---|---|
| **Tracks** | keyframes that drive one property of one target: `door.height`, `lamp.color`, `hero.rotation`. Kinds: `scalar`, `vec3`, `quat` (slerped the short way) and `color`. Each key has an ease for the segment after it: `linear`, `step`, `smooth`, `in`, `out`, `inout`. An `additive` track adds to what earlier tracks give (a wobble on a path; rotations multiply). A track can be muted. |
| **Events** | a name (and some data) at a time: `{t = 1.5, name = "door_opened"}` |
| **Markers** | named times to jump to: `act2`, `boss_phase_3`; they also fire when playback crosses them |
| **Spans** | a target that is on from `start` to `end` (a torch, a crate, a fog volume) |
| **Clips** | an animation clip played by an actor from `start` to `end`, with `loop`, `speed` and a `blend` time |
| **Subs** | whole timelines nested inside, started at a time and played at their own speed |

It plays `once`, `loop`s or `pingpong`s, forwards, backwards (negative speed) and
at any speed, and it can be scrubbed to any time. The state at a time is a
function of the time alone (it is evaluated from scratch, then compared with
what was last reported), so scrubbing, reversing and looping need no special
cases. Events fire once when playback crosses them going forwards; scrubbing and
reversing fire none unless asked.

A nested timeline that has finished keeps its last values (like a track holds its
last key); its spans, clips and events are over.

## From Lua

```lua
local door = timeline.create{
  duration = 6,
  wrap = "once",                              -- or "loop", "pingpong"
  tracks = {
    { target = "door", property = "height", keys = {
        { t = 0, v = 0 }, { t = 2, v = 3, ease = "smooth" } } },
    { target = "lamp", property = "color", kind = "color", keys = {
        { t = 0, v = { 1, 0.9, 0.7 } }, { t = 4, v = { 1, 0.2, 0.1, 1 } } } },
    { target = "hero", property = "position", kind = "vec3", keys = {
        { t = 0, v = Vec(0, 0, 0) }, { t = 6, v = Vec(0, 0, 10), ease = "inout" } } },
  },
  events  = { { t = 2, name = "door_opened" } },
  markers = { { t = 4, name = "alarm" } },
  spans   = { { target = "torch", start = 1, ["end"] = 5 } },
  clips   = { { target = "hero", clip = "Walk_Loop", start = 0, ["end"] = 6, loop = true } },
}

-- Values go to the function you bind to a target (or to one of its properties).
timeline.bind(door, "door", function(property, value) models.move(doorModel, Vec(0, value, 0)) end)
timeline.bind(door, "hero", "position", function(_, pos) models.move(hero, pos) end)

hook.Add("TimelineEvent", "cutscene", function(e) if e.name == "door_opened" then audio.play(creak) end end)
hook.Add("TimelineActive", "cutscene", function(e) models.visible(torch, e.on) end)   -- e.target = "torch"
hook.Add("TimelineClip", "cutscene", function(e)                                      -- e.kind: "start", "stop", "seek"
  if e.kind ~= "stop" then models.play(hero, e.clip, e.loop, e.speed) end
end)
hook.Add("TimelineDone", "cutscene", function(e) print("over") end)

timeline.play(door)                         -- {from = 1, speed = 2, wrap = "loop"} are options
```

| Function | Does |
|---|---|
| `create(table)` | builds a timeline (the fields above) → id. A mistake names its path: `tracks[0].keys[0]: needs t` |
| `load(file)` | the same from a JSON file inside the game's folder → id, or nil and why |
| `play(tl [, {from, speed, wrap}])`, `pause`, `resume`, `stop` | playback; a timeline that has finished starts over |
| `seek(tl, seconds [, fireEvents])`, `seekToMarker(tl, name [, fireEvents])` | jump (everything is re-sent; events only when asked and forwards) |
| `speed(tl [, v])` | get or set; negative plays backwards |
| `time`, `duration`, `playing`, `finished`, `loops` | what it is doing |
| `bind(tl, target [, property], fn)`, `unbind(tl, target [, property])` | the value callback `fn(property, value)`: a number, a `Vec`, a quaternion `{x, y, z, w}` or a colour `{r, g, b, a}` |
| `json(tl)` | the timeline as JSON text |
| `destroy(tl)`, `exists(tl)`, `count()` | housekeeping; a script's timelines go when it is unloaded (256 at most) |

Hooks: `TimelineEvent {timeline, name, data, time}`, `TimelineMarker {timeline,
name, time}`, `TimelineActive {timeline, target, on}`, `TimelineClip {timeline,
target, clip, kind, time, loop, speed, blend}` (`time` is seconds into the clip,
already times its speed), `TimelineDone {timeline}`, `TimelineLoop {timeline,
count}`.

What a timeline reports waits in a queue and reaches your functions at the start
of the next frame's update, so a script calling `seek` or `play` is never called
back from inside its own call. Values are only sent when they change (all of them
after a seek).

## A file

The same table is a JSON file, so a cutscene can be authored and saved outside
the script:

```json
{
  "duration": 6,
  "tracks": [ { "target": "door", "property": "height", "kind": "scalar",
                "keys": [ { "t": 0, "v": 0 }, { "t": 2, "v": 3, "ease": "smooth" } ] } ],
  "events": [ { "t": 2, "name": "door_opened" } ],
  "subs":   [ { "start": 4, "speed": 2, "timeline": { "events": [ { "t": 1, "name": "inner" } ] } } ]
}
```

`Timeline::fromJson` and `toJson` round-trip it; a wrong file is refused with the
path to the mistake. Limits: 4,096 tracks, 200,000 keys, 100,000 events, nesting 8
deep (a timeline cannot contain itself).

## From C++

```cpp
kke::Timeline t;
size_t k = t.addTrack("door", "height");
t.addScalarKey(k, 0.0f, 0.0f);
t.addScalarKey(k, 2.0f, 3.0f, kke::Ease::SmoothStep);
t.addEvent(2.0f, "door_opened");
struct Sink : kke::TimelineSink {                       // override what you care about
    void value(const std::string& target, const std::string& property, kke::ValueKind, const glm::vec4& v) override { ... }
    void event(const kke::TimelineEvent& e) override { ... }
} sink;
t.play();
t.update(dt, sink);                                     // every frame
```

`evaluate(t, sink)` reads a time without playing.

## Not here yet

- An editor: a timeline is written as a table or a file.
- Built-in binding to engine objects (a model's transform, the camera, a light): the
  game binds a function to a target. Camera shots stay in `camera.path`.
- Curves with tangent handles: eases are the six above.

Tests: `tests/test_timeline.cpp`, `tests/test_script_timeline.cpp`.
