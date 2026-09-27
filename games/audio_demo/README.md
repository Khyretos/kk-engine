# Audio demo

Ten listening stations stand in a row on a long dirt field, one for each
case the audio engine handles: an open field, a small stone room, a great
hall, a padded room, the same knock behind wood, glass and stone walls, a
sound that reaches you round through a door, crates of six materials
falling on stone, footsteps over six grounds, a tick circling your head,
and navigation pings. You stand at the station's listening spot (a small
red block). Turning the camera turns your head, so you can hear a sound
move from one ear to the other. A panel on the right says what to listen
for, shows what the engine measured, and has switches to turn reverb,
wall muffling and sound through openings on and off. Wear headphones.

Nothing in the demo plays a recorded sound. Every knock, bounce, step and
ping is synthesized from the materials involved, and every room effect
comes from rays cast into the Jolt bodies the stations are built from.
The demo is the starting point for any game where sound carries
information: a stealth game (you hear a guard through a door), a horror
game (a big empty hall that rings), a game for blind players (pings and
footsteps on different floors), or any physics game where things should
sound like what they are made of. The engine side is explained in
[docs/AUDIO.md](../../docs/AUDIO.md); this page explains the demo.

There is no screenshot of this demo in `website/static/media/` yet.

## Run it

The executable is `audio_demo` (`kke_add_game(audio_demo ...)` in
[CMakeLists.txt](CMakeLists.txt), which is `add_executable` on desktop).
The root `CMakeLists.txt` only adds it when `KKE_ENABLE_JOLT` is on, which
is the default, because the walls and floors the sound rays hit are Jolt
bodies.

```bash
cmake --build build --target audio_demo
cd build/bin
./audio_demo
KKE_SKIP_INTRO=1 ./audio_demo                              # skip the logo intro
KKE_AUDIO_DEMO_TOUR=1 ./audio_demo                         # visit every station, log what was measured
KKE_AUDIO_DEMO_TOUR=1 KKE_AUDIO_DEMO_EXIT=1 KKE_AUDIO_RECORD=tour.wav ./audio_demo   # record the tour, then close
```

| Variable | Effect | Read in |
|---|---|---|
| `KKE_AUDIO_DEMO_TOUR=1` | visits every station in turn (7 s each; the hall 9 s, footsteps 10 s, crates 12 s) and logs one line per station | `AudioDemoModule::init` |
| `KKE_AUDIO_DEMO_EXIT=1` | closes the window when the tour is done | `AudioDemoModule::init` |
| `KKE_AUDIO_RECORD=file.wav` | saves the whole mix to a WAV when the game closes (any game) | `AudioModule` |
| `KKE_AUDIO_BINAURAL=1` | starts in the built-in headphone model | `AudioModule` |
| `KKE_AUDIO_HRTF=1` or `=my_ears.sofa` | starts in Steam Audio's HRTF (only in a Steam Audio build) | `AudioModule` |
| `KKE_AUDIO=off` | no output device; the mix still runs silently | `AudioModule` |

A build with `-DKKE_ENABLE_STEAM_AUDIO=ON` (off by default; it downloads
the SDK once, x64 Linux and Windows only) adds a "Steam Audio HRTF"
choice to the panel. See [docs/AUDIO.md](../../docs/AUDIO.md) "Steam Audio".

Rebindings are saved to `audio_demo_input.json` (the name passed to
`InputModule` in [main.cpp](main.cpp)).

## Controls

The demo's own actions are made in `AudioDemoModule::defineInput`
([AudioDemoModule.cpp](AudioDemoModule.cpp)), in the "Audio demo" group.
The camera and panel actions come from the engine.

| Action | Keyboard / mouse | Controller |
|---|---|---|
| Previous station (`audio.prev`) | `[` | LB |
| Next station (`audio.next`) | `]` | RB |
| Start the station again (`audio.again`) | R | X (west) |
| Ping your surroundings (`audio.ping`) | Q | A (south) |
| Turn your head (the camera) | left-drag | right stick (`camera.orbit`) |
| Closer / further | mouse wheel | d-pad up / down (`camera.zoom`) |
| Move the camera's target | right-drag | no controller binding |
| Give the panel the controls (`panel.toggle`) | F3 or Esc, or click the panel | View (Back) |
| In the panel: choose a row / change it / press / leave | arrows, Enter, Esc | d-pad or left stick, A, B |
| Developer panels (ImGui) | F1 (developer builds only) | no controller binding |

Notes from the code:

- Button names are positions (SDL's `WEST`, `SOUTH`), so on a
  PlayStation pad X is Square and A is Cross. The panel's help line shows
  the glyph of the device you last touched (`{action}` prompts,
  `kke::ButtonPrompts`).
- The right stick and d-pad camera controls come from
  `OrbitCameraModule::setPadControls(true)` in [main.cpp](main.cpp). Both
  actions are in the `game` input context, as are the demo's four
  actions, so they all rest while the panel has the controller (Active).
  That is why the d-pad can move the panel's rows without also zooming.
- The demo only defines `audio.ping`; it does not read it.
  `AudioModule::update` pings whenever an action with that id exists and
  is pressed, so one press gives one sweep. `readInput` has a comment
  saying so.
- The mouse only turns the camera when it is not over the panel
  (`OrbitCameraModule` checks `Application::uiCapturesMouse`).

## How it plays

There is no goal. You pick a station, listen, and compare.

| # | Station | What is there | Listen for (the panel's text, shortened) |
|---|---|---|---|
| 1 | Open field | wood knocks 6 m ahead and about 20 m away | dry, no echo; the far knock is quieter and later |
| 2 | Small stone room | 5 x 3 x 5 m stone room, one door; a wood knock inside | a short, bright ring after each knock |
| 3 | Great hall | 24 x 12 x 24 m stone hall; a metal hit every 2.8 s | a long tail of seconds, and a gap before it |
| 4 | Padded room | the stone room again, in rubber | the same knock, dead dry |
| 5 | Through walls | a closed stone room whose front wall is three panels: wood, glass, stone; a metal hit behind each | muffled behind wood, clearer behind glass, barely there behind stone |
| 6 | Round through a door | a stone room with a door in its right wall; the sound is outside | it comes from the door's direction, clearer than through stone |
| 7 | Falling crates | a stone pad; crates of wood, metal, plastic, rubber, glass, stone dropped in turn | every contact made from both materials; bounces get quieter |
| 8 | Footsteps | six 2 m strips: stone, wood, metal, dirt, glass, rubber; a walker goes back and forth | each ground sounds different |
| 9 | Around your head | a tick circling you at 3 m | it moves behind you, not only left and right |
| 10 | Navigation pings | a corridor: walls left and right, a dead end behind, a way out ahead, a side opening on the right | close walls ping high, open ways sound open |

The panel ("Audio demo", on the right edge) has these rows, top to bottom:

- The controls line (`hint()`): on a keyboard it names the keys and says
  "left-drag turns your head, the wheel zooms"; on a controller it shows
  the button prompts, including the right stick and the d-pad.
- **Station**: a choice of the ten; changing it moves you there.
- **Again**: restarts the station (same as R).
- **Tour**: a toggle for the automatic tour.
- **Listen for:** the station's text.
- **Ping now** and the ping prompt, shown only at the Pings station.
- The emitters: each label with its material name.
- **What the engine hears here**: room RT60, reverb and openness in
  percent, how many sounds played, the least that got through a wall, and
  how many came via a door.
- **Switches**: Reverb, Walls muffle, Openings (these are
  `AudioModule::settings.reverb`, `.occlusion` and `.openings`), and
  **Sound for**: Speakers, Binaural, and Steam Audio HRTF when the build
  has it.

Two things to try that show a system working: at "Round through a door",
untick Openings and the sound jumps from the door to straight through
the wall, duller. At "Small stone room", untick Reverb and the ring is
gone.

## How it works

### Startup and the frame

[main.cpp](main.cpp) creates the `Application` (1280 x 720), sets the
mood `clear_day` (`assets/moods/clear_day.yaml`: a bright sky and the
`meadow_day` ambience loop), sets the camera's far plane to 200 m, and
adds the modules in this order:

1. `InputModule` (`audio_demo_input.json`)
2. `UiModule` (RmlUi, draws the panel)
3. `OrbitCameraModule` (distance 9, pitch -0.55, yaw 0.4, target at
   ear height), with `setPadControls(true)`
4. `RigidBodyModule` (Jolt: every wall, floor and crate)
5. `ModelModule` (draws the boxes)
6. `AudioModule` (mixer, synthesis, rays)
7. `SoundVisualizerModule`, with `settings.enabled = true`
8. `kke_audio_demo::AudioDemoModule`, the demo
9. `DemoPanelModule("Audio demo", Side::Right)`, 370 dp wide
10. `DebugControlModule`, its panel hidden (`setUiVisible(false)`)
11. `StatsModule`

`AudioDemoModule::dependencies()` declares Audio, RigidBodies, Models and
OrbitCamera as required, each with the reason (for example "turning the
camera turns your ears"). Input and the panel are optional: `defineInput`
and `buildPanel` return early without them.

`AudioDemoModule::init`:

1. Finds the four modules and limits the camera to 2..60 m.
2. Sets `AudioModule::listenerOverride` to the demo's `listener()`, so
   the engine hears from the station, not from the camera.
3. `buildWorld()`: the field, the ten stations, the emitter markers.
4. Reads the two tour variables and enters station 0.
5. `defineInput()` and `buildPanel()`.

`AudioDemoModule::update`, every frame:

```cpp
readInput();
m_stationIndex = m_current;
m_time += ctx.dt;
tickStation(ctx.dt);
measure();
if (m_tour && (m_tourLeft -= ctx.dt) <= 0.0f) { /* next station, or end the tour */ }
```

`AudioModule` runs before it (it was added first), so the listener,
contacts, room probe and occlusion rechecks for this frame are done with
the demo's listener.

### Building the stations from boxes

Everything is a box. `AudioDemoModule::box(min, max, material, visible)`
does two things with one call:

```cpp
kke::RigidWorld::BodyDesc d;
d.shape = kke::RigidWorld::Shape::Box;
d.motion = kke::RigidWorld::Motion::Static;
d.halfExtents = size * 0.5f;
d.position = centre;
d.material = material;
m_bodies->world().add(d);
if (visible) m_models->spawn(cubeModel(material), glm::scale(glm::translate(glm::mat4(1.0f), centre), size));
```

The `material` is an audio material id (`kke::AudioMaterialTable`: 0
Default, 1 Stone, 2 Wood, 3 Metal, 4 Glass, 5 Rubber, 6 Dirt, 7
Plastic). The Jolt body carries it, so any ray the audio casts knows what
it hit, and any crate that lands on it knows what it landed on. That one
number is how a wall becomes "stone" for muffling, reverb, echoes and
impacts at once.

`cubeModel(material)` makes one unit cube model per material, coloured by
`colourOf` (stone grey, wood brown, metal light and a little metallic,
glass pale blue, rubber near black, dirt dark brown, plastic red), and
caches it in `m_cubes`. Every box is an instance of one of those eight
models, scaled.

`room(floorCentre, inner, wall, material, doors, ceiling)` builds four
walls of thickness `wall` around an inner size. Each wall is cut around
its doors: the pieces between the gaps, plus a lintel over each door
from the door's height to the ceiling. A `Door` is
`{side, offset, width, height}`, with side 0 = +z, 1 = -z, 2 = +x,
3 = -x. The ceiling is a box that blocks sound but is not drawn:

```cpp
// The ceiling blocks sound but isn't drawn: the camera looks in from above.
if (ceiling) box({...}, {...}, material, false);
```

The stations sit along +x, about 35-40 m apart (x = 0, 35, 75, 115, 150,
185, 220, 255, 290, 325), on one dirt box 380 m long. Each `Station`
holds a title, the "listen for" text, the listening spot `ears`, a
camera distance, yaw and pitch, a tour time, and its `Emitter`s. An
emitter is a spot that plays an impact of one material every `period`
seconds, starting `phase` seconds after you arrive, at an `intensity`.
Each emitter gets a small cube marker in its material's colour. Rooms,
the walls station, the door station and the pings corridor use a camera
pitch of -1.0 rad so the view looks down over the walls.

The station data, as built in `buildWorld`:

| Station | Room / geometry | Ears | Emitters (material, period, phase) |
|---|---|---|---|
| Open field | none | (0, 1.6, 0) | wood 1.6 s at 0.2 s, 6 m ahead; wood 1.6 s at 1.0 s at (9, 1, 18) |
| Small stone room | inner 5 x 3 x 5, 0.3 m stone, door 1.2 x 2.2 in +z | (34, 1.6, -1) | wood 1.4 s |
| Great hall | inner 24 x 12 x 24, 0.6 m stone, door 2 x 3 in +z | (70, 1.6, -4) | metal 2.8 s, intensity 0.8 |
| Padded room | as the stone room, rubber | (114, 1.6, -1) | wood 1.4 s |
| Through walls | inner 8 x 3 x 4 stone, the +z wall replaced by 0.3 m panels of wood, glass, stone | (150, 1.6, -0.8) | metal 3 s behind each panel, phases 0.2, 1.2, 2.2 s |
| Round through a door | inner 6 x 3 x 6 stone, a 2 x 2.4 door in the +x wall | (183, 1.6, -1) | metal 1.6 s outside at (190, 1.2, -2.5) |
| Falling crates | a 6 x 6 m stone pad | (220, 1.6, -4) | none: the crates are the sound |
| Footsteps | six strips 2 x 1.2 m, 3 cm high | (255, 1.6, -3.5) | none: the walker |
| Around your head | none | (290, 1.6, 0) | none: the circling tick |
| Navigation pings | two stone walls 2 m apart, a gap in the right one, an end wall, an invisible roof | (325, 1.6, -5) | none: pings |

At the walls station the three emitters are staggered by a second so you
hear them one at a time, left to right. At the door station the emitter
is at z = -2.5 and the door is at z = 0.5..2.5, so the straight line from
you to the sound goes through stone, and the way round goes through the
door.

### The listener: turning the camera turns your ears

```cpp
kke::Listener l;
l.position = m_stations[size_t(m_current)].ears;
// Where the camera looks, level: turning the view turns your head.
glm::vec3 f = cam.target - cam.position;
f.y = 0.0f;
l.forward = glm::length(f) > 1e-4f ? glm::normalize(f) : glm::vec3(0, 0, 1);
l.up = {0.0f, 1.0f, 0.0f};
```

The position is always the station's `ears`, whatever the camera does.
Zooming out to 26 m to see the hall does not make you hear from 26 m
away. Only the direction comes from the camera, flattened to level, so
looking down at a room does not tip your head. `AudioModule` calls this
function every frame instead of using the camera
(`listenerOverride`), and `shutdown` clears it.

### Entering a station

`enter(index)` resets everything so each station is heard on its own:

1. On a tour, it logs what was measured at the station you are leaving.
2. If the circle station switched the spatial mode for you, it puts the
   old mode back.
3. Removes any crates (body and model).
4. Stops every playing sound (`mixer().stopAll()`). That includes the
   mood's ambience loop; `AudioModule` starts it again on its next update.
5. Wraps the index (so `[` on the first station goes to the last), and
   resets the station clock, each emitter's next hit (its `phase`), the
   measurements, and the crate, step, tick and ping timers.
6. At "Around your head", if the mode is Speakers (`SpatialMode::Stereo`)
   it switches to Steam Audio's HRTF when `AudioModule::hrtfAvailable()`,
   else to the built-in Binaural model, and remembers to switch back.
7. `OrbitCameraModule::setView(ears, distance, pitch, yaw)`, moves the
   red ears marker, and shows the walker or the circling tick only at
   their stations.

Because the listener jumps 35 m or more, the engine's room tracker
treats it as a teleport (more than 2 m at once) and probes four times at
once, so the room is right before the first knock
([docs/AUDIO.md](../../docs/AUDIO.md) "Reverb, echoes and openings").

### Emitters and the special stations

`tickStation(dt)` first plays every emitter that is due:

```cpp
if (const uint32_t id = m_audio->playImpact(e.position, e.material, e.intensity)) m_emitterOf[id] = int(i);
m_nextHit[i] += e.period;
```

`playImpact` returns the voice id, and the demo remembers which emitter
it came from, so the measurements can be split per emitter. The engine
works out, before the voice starts, how much of it gets through walls
and whether it comes through an opening, then rechecks every 0.1 s.

Then one of four special cases:

- **Crates.** Every 1.8 s, while fewer than 6 are alive, a 0.5 m dynamic
  Jolt box is dropped from 3.2 m, 4 m in front of you, alternately 0.8 m
  left and right, spinning (angular velocity 1.5, 0.4, -1.0). Materials go
  in the order wood, metal, plastic, rubber, glass, stone. Density is
  3000 for metal, 900 for rubber and 700 for the rest; restitution is 0.6
  for rubber, 0.2 for the rest. Each crate's model follows its body every
  frame and is removed after 5 s. The demo plays no sound for the crates
  itself: `AudioModule` hears the Jolt contacts and plays an impact of
  both materials (the crate's and the stone pad's), louder for faster
  contacts, with a 0.08 s cooldown per pair so a settling crate does not
  rattle.
- **Footsteps.** The walker (a 0.4 x 1.8 x 0.3 box) moves back and forth
  over 11.2 m at 1.4 m/s. Every 0.55 s a ray goes 1 m down from 0.5 m
  above its feet; the material it hits is the ground (dirt when it hits
  nothing). The step is played with `playFootstep`, 12 cm left or right
  of centre in turn, like two feet.
- **Around your head.** A plastic tick every 0.35 s on a 3 m circle,
  going round at 0.9 rad/s (a lap about every 7 s).
- **Pings.** `AudioModule::ping()` every 2.5 s, the first 0.8 s after
  you arrive. A ping casts `pingRays` (8) level rays from the listener,
  clockwise from ahead, up to `pingRange` (12 m), and plays one short
  tone per direction from where the wall is: higher the closer, an
  "open" sound where nothing was hit.

### Measuring what the engine did

`measure()` runs every frame and reads the mixer back:

```cpp
for (const kke::ActiveSound& a : m_audio->mixer().activeSounds()) {
    const bool first = m_seen.insert(a.id).second;
    if (first) { ++m_measured.sounds; if (a.viaOpening) ++m_measured.throughDoor; }
    m_measured.minTransmission = std::min(m_measured.minTransmission, a.transmission);
    m_measured.peak = std::max(m_measured.peak, a.loudness);
    ...
}
```

Each voice is counted once. `transmission` (0..1) is how much got
through walls; `viaOpening` says the mixer placed the sound at a door.
From the room it copies RT60, wet (reverb level) and openness, the
number of openings, the number of echo taps
(`roomTracker().echoes(kMaxEchoes, materials)`), and the most occlusion
rays cast in one frame (`raysLastFrame`). Because `enter` stops every
sound, whatever is playing belongs to this station.

On a tour, `logMeasured` writes one line per station: the counts above
plus, for each emitter, the least that got through and "(via opening)"
when it came round. This makes the demo a regression check: run the tour
headless and compare the numbers.

### The panel

The panel is `kke::DemoPanelModule` ([docs/DEMO_PANEL.md](../../docs/DEMO_PANEL.md)),
drawn with RmlUi. `buildPanel` adds one section. The values stay in the
demo; the panel reads them every frame and calls a row's callback when
the player changes it. Some rows worth reading:

```cpp
s.choice("Station", &m_stationIndex, titles, [this] { enter(m_stationIndex); });
s.button("Ping now", [this] { m_audio->ping(); }).showIf(pings);
s.toggle("Reverb", &set.reverb);
s.toggle("Walls muffle", &set.occlusion);
s.toggle("Openings", &set.openings);
```

The three toggles point straight at `AudioModule::Settings`, so the
engine reacts on the next frame. "Sound for" uses a `Ref<int>` (a
function returning a pointer) so the choice always shows the engine's
current mode, including when the circle station changed it. Choosing a
mode clears `m_forcedBinaural`: your choice is kept when you leave the
station.

The panel has three states (Open, Active, Collapsed). View on a pad or
F3 gives it the controller and keyboard; B or Esc gives them back; the
mouse can click it in any state. With a DemoPanelModule present, the
engine's ImGui windows start hidden and F1 shows them (developer builds
only).
Esc works like a pause menu: it opens the panel with the keyboard on
it, and Esc again goes back to the game. So Esc does not close the
window; the panel's "Quit" row, just above "Hide panel", does.
`setEscapeMenu(false)` gives Esc back to a game that needs it.

### The developer panels (F1)

`AudioDemoModule::renderUi` runs once, on the second frame, when every
ImGui window exists: it collapses "Performance", "Audio", "Rigid bodies
(Jolt)" and "Camera" and stacks them at the top left. When you press F1
you see four title bars, not four open windows over the stations. The
"Audio" window has volumes per category, a button per material, and the
sound visualizer's settings.

### The sound visualizer

`SoundVisualizerModule` draws every playing sound as a mark on a ring
around the screen centre, in the direction it comes from (top = ahead),
sized by loudness, thinner and labelled "(muffled)" through a wall, with
a caption. The demo turns it on in [main.cpp](main.cpp). Players who
cannot hear can see the same cases. The ring and captions are RmlUi,
drawn every frame from `SoundVisualizerModule::frameStart` (it needs the
`UiModule`), so they show with the F1 panels hidden and in shipping
builds. Only its settings window is ImGui ("Customize..." in the Audio
panel).

## Design decisions

- **One station per case, side by side.** Each case is set up so that
  one thing differs: the stone room and the padded room are the same room
  with the same knock in different materials; the three wall panels have
  the same metal hit behind them. The panel's "listen for" text says what
  that one difference should sound like. The header comment calls it
  "Every case the audio engine handles, one station each".
- **Entering stops every sound.** `enter` calls `mixer().stopAll()`, and
  the comment in `measure` says why that matters: "Everything playing is
  this station's: entering one stops the rest." The measurements would
  otherwise mix two stations. The mood's ambience loop is the one sound
  that is not the station's: `measure` skips `AudioModule::ambienceVoice()`,
  so it is not counted in "Sounds" or in the peak.
- **The ears stay at the station; the camera only turns them.** See
  `listener()`: the view can zoom out to show a whole hall while you still
  hear from inside it. The alternative, hearing from the camera (the
  engine's default), would make every zoom change the sound.
- **Head direction is level.** `f.y = 0` in `listener()`. The room
  stations look down at -1.0 rad; without flattening, your ears would
  point at the floor.
- **Everything is boxes built in code, no assets.** The
  [CMakeLists.txt](CMakeLists.txt) comment: "Everything it shows is built
  from boxes in code; no assets needed." A box is exact for Jolt, and a
  wall's thickness (which changes how much it muffles) is exactly what
  you wrote.
- **The ceiling is solid but invisible.** "The ceiling blocks sound but
  isn't drawn: the camera looks in from above." A room without a roof
  would let the reverb rays escape upward and sound like a yard.
- **Physics makes the crate sounds, not the demo.** Crates are ordinary
  Jolt bodies with a `material`; `AudioModule` turns their contacts into
  sounds. That is the same path any game's props take, so the station
  tests the real path.
- **Footsteps find their ground with a ray.** The walker does not know
  which strip it is on; a ray down reads the body's material, the way
  `kke::CharacterFootsteps` does for animated characters
  ([docs/AUDIO.md](../../docs/AUDIO.md) "Footsteps").
- **The circle station switches to headphone sound for you, and puts it
  back.** Speakers only give left and right, so a sound going behind you
  would not be heard as behind. If you pick a mode yourself in the panel,
  the demo stops restoring (`m_forcedBinaural = false`, with the comment
  "your choice now; leaving the station keeps it").
- **A scripted tour that logs numbers.** `KKE_AUDIO_DEMO_TOUR` and
  `KKE_AUDIO_DEMO_EXIT` make the demo usable headless: the commit that
  added it (bcc4849) pairs it with `KKE_AUDIO_RECORD` so a run can be
  listened to and its numbers compared.
- **The panel is RmlUi; the engine's ImGui windows are for developers.**
  Commit 944d596 moved the demo's settings from ImGui onto
  `DemoPanelModule`, following the rule in
  [docs/DEMO_PANEL.md](../../docs/DEMO_PANEL.md): F1 panels are ImGui for
  developers, everything a player sees is RmlUi, so a player with only a
  controller can change everything.
- **The sound ring is RmlUi, not ImGui.** Players who cannot hear need
  it in the finished game, and the ImGui panels are hidden by default and
  missing from shipping builds (BUG-078 in BUGS.md, fixed). So the ring
  and captions are drawn from `frameStart` with RmlUi; only the
  visualizer's settings window, a developer-style panel, stays ImGui.
- **The controls line has a keyboard version.** `{camera.orbit}` and
  `{camera.zoom}` are bound only on a pad, so on a keyboard they would
  be blank. `hint()` shows a line that says "left-drag" and "the wheel"
  in words instead (BUG-082 in BUGS.md, fixed for this demo).

## Tuning

| What | Where | Effect |
|---|---|---|
| Station layout, room sizes, wall thickness, doors | `buildWorld` | a thicker wall muffles more (transmission to the power thickness / 0.3 m); a bigger room rings longer |
| Emitter `period`, `phase`, `intensity`, material | each station's `emitters` in `buildWorld` | how often and how hard; material changes the whole character |
| `tourSeconds` | `Station` (7 s default; hall 9, footsteps 10, crates 12) | time per station on the tour |
| `cameraDistance`, `cameraYaw`, `cameraPitch` | `station(...)` and the pitch loop in `buildWorld` | the view on arrival |
| Crate interval 1.8 s, at most 6, lifetime 5 s, size 0.5 m, drop height 3.2 m | `tickStation`, `Kind::Crates` | more crates overlap more sounds; density and restitution change how they bounce |
| Walk speed 1.4 m/s, step every 0.55 s | `tickStation`, `Kind::Footsteps` | a faster pace |
| Circle radius 3 m, 0.9 rad/s, tick 0.35 s | `tickStation`, `Kind::Circle` | a wider or faster circle |
| Auto ping every 2.5 s | `tickStation`, `Kind::Pings` | |
| Camera distance limits 2..60 m | `init`, `setDistanceLimits` | |
| `pingRays` (8), `pingRange` (12 m) | `AudioModule::Settings` | more directions per sweep, a longer reach |
| `impactThreshold` (0.8 m/s), `impactFullSpeed` (9 m/s), `pairCooldown` (0.08 s), `maxImpactsPerFrame` (8) | `AudioModule::Settings` | how crates and props sound |
| `roomProbeInterval` (0.25 s), `maxRaysPerFrame` (160), `echoes` | `AudioModule::Settings` | how fast the room follows you, and the ray budget |
| Material sounds | `AudioModule::materials().set(id, ...)` | change or add a material ([docs/AUDIO.md](../../docs/AUDIO.md) "Impacts: modal synthesis") |

## Engine features it uses

| Feature | Header / module | Doc |
|---|---|---|
| Mixer, voices, spatial modes | `kke/AudioMixer.h`, `kke/modules/AudioModule.h` | [AUDIO.md](../../docs/AUDIO.md) |
| Impact synthesis, material table | `kke/ImpactSynth.h` | [AUDIO.md](../../docs/AUDIO.md) "Impacts: modal synthesis", "Material ids" |
| Occlusion and sound through openings | `AudioModule::soundPath`, `kke/RoomAcoustics.h` | [AUDIO.md](../../docs/AUDIO.md) "Occlusion", "Reverb, echoes and openings" |
| Room reverb, echoes, room tracker | `kke/RoomAcoustics.h` | [AUDIO.md](../../docs/AUDIO.md) "Reverb, echoes and openings" |
| Footstep sounds | `kke/Footsteps.h`, `AudioModule::playFootstep` | [AUDIO.md](../../docs/AUDIO.md) "Footsteps" |
| Binaural and Steam Audio HRTF | `AudioModule::setSpatialMode`, `kke/SteamAudioSpatializer.h` | [AUDIO.md](../../docs/AUDIO.md) "Binaural", "Steam Audio" |
| Navigation pings | `AudioModule::ping` | [AUDIO.md](../../docs/AUDIO.md) "Accessibility" |
| WAV recording | `kke/WavFile.h`, `KKE_AUDIO_RECORD` | [AUDIO.md](../../docs/AUDIO.md) |
| Sound visualizer | `kke/modules/SoundVisualizerModule.h` | [AUDIO.md](../../docs/AUDIO.md) "Accessibility" |
| Static and dynamic Jolt bodies with a material | `kke/RigidWorld.h`, `kke/modules/RigidBodyModule.h` | [PHYSICS_BRIDGE.md](../../docs/PHYSICS_BRIDGE.md) |
| Box models built in code | `kke/modules/ModelModule.h` | |
| Orbit camera with pad controls | `kke/modules/OrbitCameraModule.h` | [DEMO_PANEL.md](../../docs/DEMO_PANEL.md) "A camera for the controller" |
| Settings panel (RmlUi, pad + keyboard + mouse) | `kke/modules/DemoPanelModule.h` | [DEMO_PANEL.md](../../docs/DEMO_PANEL.md) |
| Actions and bindings | `kke/modules/InputModule.h` | [INPUT.md](../../docs/INPUT.md) |
| Moods (sky, sun, ambience) | `Application::setMood` | [MOODS.md](../../docs/MOODS.md) |

## Assets

None of its own. Every shape is a box built in code and every sound is
synthesized. No Synty pack is used ([docs/SCENES.md](../../docs/SCENES.md)
lists `audio_demo` among the demos that use no packs). What it loads from
the engine's shared `assets/`:

- the `clear_day` mood: its sky image (Poly Haven "Kloofendal 48d Partly
  Cloudy (Pure Sky)", CC0, per the comment in
  `assets/moods/clear_day.yaml`) and the `meadow_day` ambience
  (`assets/ambience/meadow_day.flac`). A missing ambience file is logged
  as a warning by `AudioModule` and the game runs without it.
- the Noto Sans fonts for the panel, copied next to the game by
  `kke_use_ui` in [CMakeLists.txt](CMakeLists.txt).

## Make a game like this

1. **Copy the folder.** `cp -r games/audio_demo games/my_sound_game`,
   rename the target in [CMakeLists.txt](CMakeLists.txt), the namespace
   `kke_audio_demo`, the module name and the input file name, and add
   `add_subdirectory(games/my_sound_game)` inside an `if(KKE_ENABLE_JOLT)`
   block in the root `CMakeLists.txt`. (`tools/new_game` makes Lua-only
   games from `games/template`; a Lua game can play impacts with
   `audio.impact(pos, material, intensity)`, see
   [docs/SCRIPTING.md](../../docs/SCRIPTING.md).)
2. **Keep `box()` and `room()`.** They are the useful part: a level where
   every wall has an audio material. For a real level, give each Jolt body
   a `BodyDesc::material` the same way; Synty or other meshes can keep
   their look while an invisible box carries the material.
3. **Decide where the ears are.** A first- or third-person game usually
   wants the ears at the character's head, not at the camera. Set
   `AudioModule::listenerOverride` as `listener()` does, and clear it in
   `shutdown`.
4. **Make props sound by giving them a material.** Dynamic Jolt bodies
   with a `material` make impact sounds on their own, as the crates do.
   Call `playImpact` yourself only for things that are not physics (a
   door knock, a UI hit).
5. **Add footsteps.** For an animated character, use
   `kke::CharacterFootsteps` rather than the walker's timer
   ([docs/AUDIO.md](../../docs/AUDIO.md) "Footsteps").
6. **Offer headphone sound.** Keep the "Sound for" choice (or the Audio
   panel's Spatial list) so players can pick Binaural or HRTF.
7. **Turn on the visualizer and pings for accessibility.** Keep
   `SoundVisualizerModule` enabled and bind `audio.ping`.
8. **Read next:** [AUDIO.md](../../docs/AUDIO.md),
   [DEMO_PANEL.md](../../docs/DEMO_PANEL.md), [INPUT.md](../../docs/INPUT.md),
   [MOODS.md](../../docs/MOODS.md).

Pitfalls the code shows:

- A room with no ceiling has no reverb to speak of: the upward rays
  escape. Add a roof body even when you do not draw it.
- A door is only found if the room probe's level ring of rays hits it;
  a door narrower than the spacing between rays shows up over a few
  probes, as the tracker turns each probe by the golden angle.
- `mixer().stopAll()` stops everything, ambience and music included.
  `AudioModule` starts the mood's ambience loop again on its next update,
  but nothing restarts your music. Stop only your own voices
  (`AudioMixer::stop(id)`) in a real game.
- If you count sounds from `mixer().activeSounds()`, skip
  `AudioModule::ambienceVoice()` as `measure` does, or the mood's loop is
  counted as one of yours.
- The sound rays only see Jolt bodies. A wall that is only a model
  makes no difference to the sound.
- Actions in the `game` context are off while a DemoPanel is Active; put
  anything that must work in menus in another context.

## Files

| File | What is in it |
|---|---|
| [main.cpp](main.cpp) | The application, the mood, the camera, the module list |
| [AudioDemoModule.h](AudioDemoModule.h) | The module, `Kind` (the ten stations), `Emitter`, `Station`, `Measured`, `Crate`, `Door` |
| [AudioDemoModule.cpp](AudioDemoModule.cpp) | Box and room builders, the ten stations, the listener, entering a station, emitters, crates, footsteps, circle, pings, measuring, the tour log, controls, the RmlUi panel, folding the ImGui panels |
| [game.json](game.json) | Marketplace manifest (id, title, description, tags, modules, requirements) |
| [CMakeLists.txt](CMakeLists.txt) | The executable, the shaders it needs, the manifest copy, `kke_use_ui` for the panel's shaders and fonts |
