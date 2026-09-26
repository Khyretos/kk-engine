# AUDIO.md — the audio engine

ACTION_PLAN.md 2.2. What exists, how it works, why it's built this way,
and what's next. Code: `kke/AudioMixer.h`, `kke/ImpactSynth.h`,
`kke/Footsteps.h`, `kke/RoomAcoustics.h`, `kke/modules/AudioModule.h`,
`kke/modules/SoundVisualizerModule.h`, `kke/WavFile.h`; tests:
`tests/test_audio.cpp`, `tests/test_footsteps.cpp`,
`tests/test_room_acoustics.cpp`, `tests/test_wav_file.cpp`; listen:
`kke_audio_preview` and the audio demo (`games/audio_demo`).

## What you get today

- **Physics makes sound.** Jolt contacts (crates, props, anything with a
  `BodyDesc::material`), FEMFX objects hitting each other or the floor
  (`PhysicsModule::frameImpacts`) and FEMFX breaks
  (`PhysicsModule::frameBreaks`) play impacts of the materials involved: a wood crate on stone is a
  knock *and* a thud; glass breaking is a hard glass hit plus a few
  smaller ones for the pieces.
- **Materials sound different without any sample files.** Impacts are
  synthesized (modal synthesis, below): Default/Plastic, Stone, Wood,
  Metal, Glass, Rubber, Dirt. Harder hits are louder *and* brighter;
  bigger objects ring lower; the same seed gives the same sound (replays,
  networking).
- **3D.** Constant-power stereo pan, inverse-distance fall-off, a fade
  near the maximum distance, and a gentle high cut for sounds behind you
  (the cheapest front/back cue there is).
- **Footsteps.** The showcase character's feet make steps on whatever
  they land on (a ray under each foot finds the ground's material): soft
  when walking, harder when sprinting, one loud step per foot on landing.
- **Rooms sound like rooms.** A few dozen rays around the listener every
  0.25 s measure the space: a stone hall rings for seconds, a padded room
  is dry (soft walls send little back), a field has no reverb at all.
  Every spatial sound goes through that room's reverb.
- **Echoes.** The walls around you each send back a delayed copy from
  their side: a slap off a hall's far wall 80 ms later, the ceiling above,
  nothing outdoors.
- **Sound comes through doors.** A sound behind a wall that has a way
  round (a door or window the room rays found, to the outside or into the
  next room) is heard from the opening, as far away as the path through
  it, duller the sharper it had to bend, instead of straight through the
  wall.
- **Headphones mode.** Audio panel > Spatial > Binaural (or
  `KKE_AUDIO_BINAURAL=1`): each ear hears a sound slightly later and darker
  when it faces away, which gives real left/right and helps front/back.
- **Walls muffle.** Every wall between you and a sound counts, by its
  material and its thickness: a thin wood partition lets more through
  than a thick one, two walls less than one. Worked out when the sound
  starts (not even its first milliseconds come through the wall) and
  again every 0.1 s. Walls eat the highs first.
- **Air.** Far sounds lose their highs (~22 kHz up close, ~5 kHz at 50 m).
- **Sound you can see** (`SoundVisualizerModule`): every sound is a mark
  on a ring around the screen centre in the direction it comes from (top
  = ahead), sized by loudness, coloured by category, thinner and labelled
  "(muffled)" through a wall, with captions like "Metal Impact". Marks
  linger (0.8 s default) so short impacts can be seen. Everything is
  adjustable and saved to `accessibility.json`: on/off, ring and mark
  size, opacity, quietest sound shown, how long marks stay, per-category
  visibility and colour, captions. On by default in kke_demo, physics_demo
  and sandbox.
- **Works with no sound card.** miniaudio falls back to its null device;
  failing that the module mixes "silently" on the game thread, so the
  visualizer, logs and tests behave the same in CI and on servers.
- **Budgets** (OPTIMIZATION.md rule 5): 32 voices; a new sound steals the
  quietest voice or is dropped if it would be the quietest; at most 8
  impacts per frame (strongest first) and 80 ms between two sounds from
  the same pair of bodies, so a collapsing pile doesn't machine-gun. At
  most `maxRaysPerFrame` (160) occlusion rays per frame for rechecks: with
  many sounds playing, the most overdue are rechecked first and the rest
  keep their last value a frame longer. The cost stays flat.
- **Menus and pings, by ear.** Moving focus, pressing buttons, toggling
  checkboxes and dragging sliders in any RmlUi menu play short earcons.
  **Q** (d-pad down on a controller, rebindable) pings your surroundings:
  one ping per direction, clockwise from straight ahead, higher the closer
  the wall, an airy "open" sound where there's nothing in range.
- **Try it:** `KKE_AUDIO_TOUR=1 ./kke_demo` plays every material in turn,
  walking around you (front, right, back, left). The Audio panel (F1) has
  volumes per category, "hear each material" buttons, and the visualizer
  settings. `KKE_AUDIO=off` disables the output device.
- **The audio demo** (`./audio_demo`): ten stations side by side, one
  per case: open field, small stone room, great hall, padded room, the
  same knock behind wood, glass and stone, a sound round through a door,
  falling crates of six materials, footsteps on six grounds, a tick
  circling your head (binaural) and pings. Turning the camera turns your
  ears; the panel says what to listen for and shows what the engine
  measured, with switches for reverb, muffling, openings and binaural.
  `KKE_AUDIO_DEMO_TOUR=1` visits every station and logs the numbers,
  `KKE_AUDIO_DEMO_EXIT=1` closes it after the tour.
- **Record what played:** `KKE_AUDIO_RECORD=out.wav` (any game) saves the
  whole mix as a WAV when the game closes, with or without a sound card;
  `AudioModule::startRecording/stopRecording` from code.
- **Listen without the engine:** `./kke_audio_preview [dir]` writes a WAV
  per material (soft, medium, hard) and two stereo files: a knock walking
  around the listener, and the same behind a wood wall.

## How it works

### Output: miniaudio, mixing: ours

miniaudio (public domain / MIT-0, one .c/.h) opens the device on every
platform (PulseAudio/PipeWire/ALSA/JACK, WASAPI, CoreAudio, AAudio/
OpenSL, Web Audio) and decodes WAV/FLAC/MP3 (`AudioModule::loadSound`).
Its engine/node-graph/resource-manager parts are compiled out.

The mixer is our own (`kke::AudioMixer`, ~250 lines) because the mix is
what the accessibility layer, the occlusion rays and the benchmarks need
to see into, and because it has to give the same result with or without
a device, for tests. Per voice per block: gain (distance, category,
master, occlusion), constant-power pan, a one-pole low-pass (occlusion +
behind), gains ramped across the block (no clicks when something moves),
16.16 fixed-point read position with linear interpolation (any sample
rate). A soft knee above 0.9 instead of hard clipping.

Threading: the device thread calls `mix()`; the game thread calls
`play/setListener/setTransmission`. One mutex, held for one ~10 ms block.
If that ever shows in a profile, the next step is a lock-free command
queue (SPSC ring) from game thread to mixer.

### Impacts: modal synthesis

A struck object rings at a few resonant frequencies (modes), each a
decaying sine; the contact itself adds a short burst of noise. What
makes a material recognizable is the *ratio* between modes, how fast they
die, and the colour of the noise:

| Material | Modes (ratios) | Ring | Noise | Why |
|---|---|---|---|---|
| Metal | 1, 2.76, 5.40, 8.93, 13.34 | 1.4 s | little | free-bar ratios: inharmonic, bell-like |
| Glass | 1, 2.32, 4.25, 6.63, 9.38 @ 1.6 kHz | 0.7 s | bright tick | thin plate: high, long |
| Wood | 1, 2.57, 4.2, 6.1 @ 190 Hz | 0.12 s | mid | damped, hollow knock |
| Stone | 1, 1.74, 2.9, 4.3 @ 260 Hz | 0.06 s | a lot | mostly the crunch |
| Rubber | 1, 1.5 @ 90 Hz | 0.05 s | dark | a thud |
| Dirt | 1 @ 70 Hz | 0.03 s | dark, long | a soft thump |

Each mode is a recursive oscillator (`y[n] = 2cos(w)·y[n-1] - y[n-2]`):
one multiply-add per sample instead of a `sin()`. The noise goes through
two one-pole low-passes (-12 dB/octave). Harder hits raise the upper
modes and the noise cutoff (brighter, like real knocks); `size` scales
frequency by 1/size. A seed jitters mode frequencies (±3%), amplitudes
and phases.

`ImpactBank` caches 4 intensity levels × 4 variants per material, made
on first use (0.1-1 ms each). Fully cached, every default material is
up to ~10 MB of float PCM, most of it metal's 1.4 s ring; a scene
usually touches a fraction. Storing int16 would halve it (backlog). Repeated hits cost a lookup.

The numbers were tuned by ear and by `ImpactSynth.MaterialsAreDistinguishable`
(metal and glass ring 4x longer than wood; brightness order glass >
metal > wood > rubber, stone > dirt), not taken from measurements of
real objects. A game changes or adds materials through
`AudioModule::materials().set(id, ...)`.

### Material ids

`AudioMaterialTable` ids: 0 Default, 1 Stone, 2 Wood, 3 Metal, 4 Glass,
5 Rubber, 6 Dirt, 7 Plastic. They're the same numbers as
`RigidWorld::BodyDesc::material` (kke_demo's level is 1 = stone, its
crates 2 = wood). FEMFX objects use `Material::audioMaterial`; when it's
-1 the engine guesses from the physical numbers (metallic → metal,
smooth and stiff → glass, soft → rubber, dense → stone, light → wood).

### Occlusion

`AudioModule::soundPath(listener, source)` is what a sound hears through:
`occlusionQuery(listener, source) -> 0..1` and, when that is muffled, a
way round (below). The default query is Jolt: a ray to the sound; each
wall it enters is measured by a second ray back from the sound restricted
to that wall (its thickness), and lets through its material's
`transmission` (stone 0.1, wood 0.35, glass 0.5, all for ~30 cm) to the
power thickness / 0.3 m (clamped to 0.5..3). Then the ray carries on past
that wall, up to four walls. Two rays per wall; replace the query for
anything else.

The path is worked out in `AudioModule::play` (and `playImpact`,
`playFootstep`) before the voice starts: `VoiceDesc::transmission` and
`via` go in with it, so the first block is already right. Every 0.1 s
after that it is rechecked, within the frame's ray budget.

The mixer turns transmission into gain and a one-pole low-pass
(400 Hz + 17.6 kHz x t^2): walls eat highs first. Distance adds air
absorption, a cutoff of 22 kHz / (1 + d / 15 m).

### Reverb, echoes and openings

`kke::probeRoom` casts two sets of rays through `AudioModule::roomRay`
(Jolt by default):

- a **sphere** of `rays` (32) directions (a Fibonacci sphere): the
  **mean distance** of the hits (room size), average **absorption** (each
  material's `AudioMaterial::absorption`; an escaping ray counts as 1),
  **ceiling** (share of the upward rays that hit) and its height;
- a **level ring** of `ringRays` (24) at ear height: **walls** and
  **openings**. A level ray never ends on a flat floor, so a big hall's
  floor can't pass for a way out. A ray is an opening when it escapes, or
  when it goes more than 0.5 m past the wall its neighbours hit (a door
  into the next room: the opening remembers where it passes the wall,
  `through`, and how far it stays free, `reach`).

From those: **RT60** from Sabine's formula for a room of that "radius"
(0.0537 r / a); **wet** from walls and roof (a field 0, an alley a little,
a hall a lot) times how reflective the surfaces are (a padded room is dry
even closed); damping from absorption; pre-delay from the distance.

`kke::RoomTracker` keeps the result steady: each probe is turned by the
golden angle from the last (`RoomProbeSettings::rotation`), so over a few
probes the rays cover the gaps between each other and a door narrower
than the spacing is found; the numbers blend (40% per probe) so the
reverb doesn't wobble; openings are remembered for 6 probes (1.5 s) or
until a ray finds a wall there; moving more than 2 m at once (a teleport)
starts over, and the module then probes four times at once, so the room is
right before the first sound.

The reverb (`kke::Reverb`) is Freeverb's structure (Jezar, public domain):
8 damped combs and 4 all-passes per ear, each comb's feedback set so the
tail falls 60 dB in the room's RT60 (g = 10^(-3 d / RT60)). It is one send
bus in the mixer: every spatial voice feeds it by `VoiceDesc::reverbSend`
and its distance gain's square root (far sounds are mostly room). Changes
glide over a block, so walking through a door doesn't click.

**Echoes** (early reflections): the tracker keeps the wall distance in 24
directions around you; `RoomTracker::echoes` makes one tap per wall (bins
within 4 ms merge) plus the ceiling: delayed by the way there and back
(2d / 343 m/s), quieter by that extra path and the wall's absorption,
from the wall's side. The mixer reads them off a 0.2 s delay line of the
reverb send (`AudioMixer::setEchoes`, up to 8): a tap that moves fades
out and back in at its new delay, so nothing clicks. Walls within a metre
are left out (they fold into the direct sound). Cost: 8 multiply-adds per
sample, whatever is playing.

When a sound is muffled, `findOpening` tries the three openings most in
its direction at two points each (just past the gap and 3 m further; 3 and
6 m out for open air): if the listener sees that point and the point sees
the sound, the mixer places the sound at the opening
(`AudioMixer::setVia`) at the full path length. It is duller the sharper
it bends round the edge (transmission 0.95 straight through down to 0.4
for a U-turn: diffraction loses highs first), and never quieter than
straight through the wall. This is the "ambient rays" idea of
WhoStoleMyCoffee/raytraced-audio.

**Cost** (kke_bench, 10 ms of output, debug build on the CI VM):
`audio_mix_32_voices` 0.42 ms; `audio_mix_32_voices_full` (binaural, 8
echoes, every voice occluded, air absorption) 0.83 ms. Rays: the room
probe is 56 per 0.25 s, an occluded sound 2 per wall plus up to 12 for
its way round, per 0.1 s, capped per frame.

### Binaural

A spherical head of radius 8.75 cm. The far ear hears a sound later by
Woodworth's interaural time difference, (a/c)(θ + sin θ) for lateral angle
θ (up to ~0.66 ms), read from a 64-sample delay line per voice with a
fractional, ramped delay. Each ear then has Brown & Duda's (1998) head
shadow: a one-pole shelf, H(s) = (α s + β)/(s + β) with β = 2c/a, lifting
highs up to +6 dB for the ear facing the sound and cutting them ~20 dB for
the one behind the head. No HRTF data sets, no licences.

### Footsteps

`kke::FootstepDetector` finds steps in any animation: a foot that rose
clear of the ground and comes back down is a step. It watches each foot
bone's height above the ground under it and learns the rig's resting
ankle height by itself, so it works on slopes, stairs and any character
without per-clip markers. `kke::CharacterFootsteps` binds the foot bones
by name (UE, Mixamo, Synty), casts a ray under each foot for the ground's
material and calls `AudioModule::playFootstep`. The sound
(`synthesizeFootstep`) is the ground's own modes struck softly (heel, then
toe) plus a scuff of its noise: long for dirt, a tick on stone.

### FEMFX impacts

FEMFX's collision report (one contact per object pair per step, approach
faster than 1 m/s) gives the tet and barycentric point of each contact;
the contact's velocity comes from `FmGetInterpolatedVelocity`. FEMFX's
floor is a plane it handles itself and doesn't report, so a landing is
found from a piece's centre of mass: falling fast one step, stopped the
next, with its lowest point at the floor. It sounds like stone.

## Accessibility

- **Deaf / hard of hearing:** the visualizer above, plus captions. Every
  sound carries a category and a material so a game can filter or
  describe it.
- **Blind / low vision:** materials are made to be told apart by ear
  (different mode ratios, ring times and noise), front/back gets a tone
  cue, walls audibly muffle, sounds come through the doorway they really
  come through, and Binaural mode gives headphone users a real left/right.
  UI earcons (`AudioModule::playEarcon`, fired by UiModule for every
  document) make menus usable by ear; navigation pings (`audio.ping`, Q)
  tell you where the walls and the open ways are. Next: a "describe what I
  hear" mode.

## Research notes (the links from the original brief)

- **WhoStoleMyCoffee/raytraced-audio** (MIT, Godot): rays from the
  listener measure the room for reverb, per-source rays muffle sounds
  behind walls, "ambient" rays find openings so outside sound pans
  toward doors. Our occlusion is its per-source ray; room rays and the
  openings trick are built the same way.
- **JustGoscha/ray-tracing-audio** (MIT, JS): ray-traced reflections,
  binaural output, a live ray visualizer; reference for reflections and
  for the visualizer.
- **Vercidium Audio** (read 2026-09-26): a ray-traced audio SDK (C, C#,
  JS; Godot first) with occlusion, permeation, EAX reverb and air
  absorption. **Not FOSS**: free for non-commercial use only, A$300 per
  commercial game (Indie), source code only with the Studio licence.
  Every game made with KKE would need its own licence, so it can't be a
  dependency. The same techniques are being built here on Jolt instead.
- **Steam Audio** (Apache-2.0): the optional high-end backend later
  (measured HRTFs, reflections, transmission through geometry). The
  built-in binaural mode covers headphones without it; Steam Audio would
  add measured HRTFs and elevation.

## Next, in order

1. Steam Audio as an optional backend (measured HRTFs, elevation,
   reflections).
2. Sliding/rolling loops from resting contacts; Doppler; a sound in
   another room getting that room's reverb (per-source rooms).
3. Streaming music/ambience from files, per-category ducking.
4. A "describe what I hear" mode (spoken captions).
5. Lock-free command queue if the mixer lock ever shows up.
