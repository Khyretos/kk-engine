# AUDIO.md — the audio engine

ACTION_PLAN.md 2.2. What exists, how it works, why it's built this way,
and what's next. Code: `kke/AudioMixer.h`, `kke/ImpactSynth.h`,
`kke/modules/AudioModule.h`, `kke/modules/SoundVisualizerModule.h`;
tests: `tests/test_audio.cpp`; listen: `kke_audio_preview`.

## What you get today

- **Physics makes sound.** Jolt contacts (crates, props, anything with a
  `BodyDesc::material`) and FEMFX breaks (`PhysicsModule::frameBreaks`)
  play impacts of the materials involved: a wood crate on stone is a
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
- **Walls muffle.** One ray per playing sound from the listener (Jolt),
  re-cast every 0.1 s. A hit lets through the wall material's
  `transmission` and lowers a low-pass cutoff: walls eat highs first.
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
  the same pair of bodies, so a collapsing pile doesn't machine-gun.
- **Try it:** `KKE_AUDIO_TOUR=1 ./kke_demo` plays every material in turn,
  walking around you (front, right, back, left). The Audio panel (F1) has
  volumes per category, "hear each material" buttons, and the visualizer
  settings. `KKE_AUDIO=off` disables the output device.
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

`AudioModule::occlusionQuery(listener, source) -> 0..1`, by default a
Jolt ray cast returning the first wall's `transmission` (stone 0.1, wood
0.35, glass 0.5). One ray per sound per 0.1 s. Replace it for anything
smarter.

## Accessibility

- **Deaf / hard of hearing:** the visualizer above, plus captions. Every
  sound carries a category and a material so a game can filter or
  describe it.
- **Blind / low vision:** materials are made to be told apart by ear
  (different mode ratios, ring times and noise), front/back gets a tone
  cue, walls audibly muffle. Next: navigation pings, UI earcons, and a
  "describe what I hear" mode.

## Research notes (the links from the original brief)

- **WhoStoleMyCoffee/raytraced-audio** (MIT, Godot): rays from the
  listener measure the room for reverb, per-source rays muffle sounds
  behind walls, "ambient" rays find openings so outside sound pans
  toward doors. Our occlusion is its per-source ray; reverb from room
  rays and the openings trick are next.
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
  (HRTF, reflections, transmission through geometry).

## Next, in order

1. Footsteps from the character (`Locomotion` foot plants × ground
   material) and sliding/rolling loops from resting contacts.
2. FEMFX impacts (not just breaks); faster than once-a-second reporting
   for the old fracture path (breakables are already immediate).
3. Reverb from a few listener rays (room size and absorption), and the
   "openings" trick from raytraced-audio.
4. HRTF (Steam Audio as optional backend), Doppler.
5. Streaming music/ambience from files, per-category ducking.
6. Lock-free command queue if the mixer lock ever shows up.
