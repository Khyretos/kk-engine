# Moods: sky, light, air and sound

A **mood** sets how a scene feels in one line: the sky behind everything,
the sun, the fill light, the haze in the air, the colour look and a
background sound. Every demo has one.

```cpp
app.setMood("golden_hour");   // C++, before app.run() or any time after
```

```lua
mood.set("night")             -- Lua, any time: the world changes at once
```

Try any mood on any game without editing it:

```sh
KKE_MOOD=night ./climb_race
```

## The moods that come with the engine

| Mood | What it looks like | Sky | Sound |
|---|---|---|---|
| `clear_day` | Bright sun, blue sky with fair-weather clouds, a little haze far away | Kloofendal 48d Partly Cloudy | birds |
| `morning` | Low sun, clean pale sky, cool shadows | Qwantani Mid Morning | birds |
| `noon` | The sun almost straight up, a deep blue sky | Qwantani Noon | birds |
| `golden_hour` | Low warm sun, long shadows, soft golden haze | Qwantani Late Afternoon | soft wind |
| `cinematic` | The golden hour through a teal-and-orange film grade (a colour LUT) | Qwantani Late Afternoon | soft wind |
| `sunset` | Orange sun on the horizon, purple shadows | Qwantani Sunset | soft wind |
| `dusk` | Blue hour: the sun just down, a warm band at the horizon | Qwantani Dusk 2 | crickets |
| `night` | Stars, cool moonlight, dark but readable | Qwantani Night | crickets |
| `arena_night` | A night sky with a warm lamp high overhead (Duel's ring) | Qwantani Night | crickets |
| `overcast` | Thick cloud, soft light from everywhere, no hard shadows | Kloofendal Overcast | soft wind |
| `misty_morning` | Fog lying in the low ground, a pale sun breaking through | Kloofendal Misty Morning | birds |
| `stormy` | Heavy dark clouds rolling in, a cold wind | Wasteland Clouds | strong wind |
| `playful` | A clean cartoon-blue gradient sky, crisp colours (the starter game's) | gradient | none |
| `studio` | A calm grey-blue backdrop, true colours: tools and viewers | gradient | none |
| `cave` | Inside or underground: dark air, one warm light | gradient | cave drips |

The sky pictures are CC0 "pure skies" from [Poly Haven](https://polyhaven.com)
(2K HDR). The build downloads them once and checks each against its
SHA-256 (`cmake/skies.cmake`); with `-DKKE_FETCH_SKIES=OFF`, or offline,
the moods fall back to a gradient sky and say so once in the log. The
sounds are CC0 loops from [OpenGameArt](https://opengameart.org) in
`assets/ambience/`. Credits: [DEPENDENCIES.md](DEPENDENCIES.md).

Which demo uses which mood: [SCENES.md](SCENES.md#moods).

## Make your own

A mood is a small YAML (or JSON) file. Put it in a `moods/` folder next to
your game (or in `assets/moods/`) and use its file name:
`app.setMood("my_mood")`, `mood.set("my_mood")`, `KKE_MOOD=my_mood`.

```yaml
title: Haunted evening
description: A green-tinged dusk with fog creeping over the ground.
sky:
  image: dusk            # a sky from the table above, or a path to your own .hdr
  intensity: 0.5         # how bright the picture is (1 = a normal day)
  ambientStrength: 0.8   # how much the sky lights the shadows
sun:
  azimuth: 135           # compass degrees: 0 north (-z), 90 east (+x), 180 south
  elevation: 8           # degrees above the horizon (an image sky's own sun is used when left out)
  color: "#ffb27a"
  intensity: 1.2
fill:                    # a soft light from the opposite side
  color: "#6f86c8"
  intensity: 0.35
fog:
  density: 0.02          # per metre at the fog's height
  heightFalloff: 0.2     # larger = the fog hugs the ground
  maxOpacity: 0.9
  color: sky             # or a colour
look:
  preset: cool
  slope: [0.95, 1.05, 0.95]
exposure: 1.4
ambience: night_crickets # a loop in assets/ambience (.flac, .wav or .mp3)
```

Colours are `"#rrggbb"` (what a colour picker gives you) or `[r, g, b]`
numbers in linear light, which may go above 1 for bright things. A typo
in a field name or a value out of range is refused with the field named,
and the lighting stays as it was.

### Sky

- **An image** (`image:`): an equirectangular HDR picture, the standard
  format for skies (Poly Haven and ambientCG publish hundreds of them
  under CC0). The engine measures it when it loads: where its sun is, how
  bright and what colour the sky is near the horizon and overhead. It then
  turns the picture so its sun sits at `sun.azimuth`, points the scene's
  sun where the picture's is, and scales it to a sensible brightness
  (`intensity` scales from there: a night sky uses a small one). A sky can
  be cropped below the horizon to halve its size
  (`tools/skies/crop_hdr.py`).
- **A gradient** (no `image:`): `zenith`, `horizon` and `ground` colours,
  `horizonFalloff` (larger = a thinner glow at the horizon), and a sun disc
  (`sunSize` in degrees, `sunDisc` brightness, `sunGlow` for the halo).
  Good for stylised games, and costs nothing.

Either way the sky also lights the scene (`lightsScene: false` turns that
off): shadows facing up take the sky's colour, faces looking down the
ground's. `ambientSaturation` (0 to 1, 0.6 by default) says how much of
the sky's colour the fill keeps; the rest stands in for light bounced off
the ground and walls, which a sky alone leaves out.

### Fog

Height fog: thickest at `height` (world y), thinning out upward, towards
the sky's colour a little above the horizon, glowing where you look into
the sun (`sunScatter`). It's worked out exactly per pixel (Inigo Quilez's
analytic height fog), so it never flickers or shows noise.

### Look

The colour grade, applied inside the tone curve: the standard ASC CDL
(`slope`, `offset`, `power`, each one number or `[r, g, b]`) plus
`saturation`. Presets: `punchy` (more contrast and colour), `golden`
(warm, soft), `cool` (blue-green shadows), `faded` (lifted blacks, low
contrast), `none`. With AgX (the default tone curve) the look sits where
AgX's own "punchy" and "golden" looks do. `exposure` brightens or darkens
everything before the tone curve.

`lut: name` grades the finished picture with a colour LUT, a `.cube` file
as DaVinci Resolve, Photoshop and other grading tools export it (3D,
any size up to 65): `name.cube` next to the mood, in a `luts/` folder
beside the moods folder, or in `assets/luts/`. It applies after the tone
curve and the look, to the colours on screen, read with tetrahedral
interpolation (greys stay grey). The engine ships `teal_orange` and
`bleach_bypass` (made by `tools/luts/make_luts.py`).

## Time of day

A mood is one moment. The clock moves through all of them: the sun rises
in the east, crosses the south and sets in the west. The sky, the sun's
colour and strength, and the exposure follow it, through golden hour and
dusk to a moonlit night and back.

```lua
clock.start{ hour = 7, dayLength = 1200 }   -- 7 in the morning; a whole day takes 20 real minutes
clock.set(18.5)                             -- jump to half past six in the evening
print(clock.time(), clock.isDay())          -- "18:30"  true
local towardsSun, elevation = clock.sun()   -- for a sundial, or a vampire
clock.pause(); clock.resume(); clock.stop()

hook.Add("Dusk", "lamps", function() light.point{ pos = Vec(0, 3, 0), color = Vec(1, 0.8, 0.5) } end)
hook.Add("Dawn", "rooster", function() print("cock-a-doodle-doo") end)
hook.Add("Hour", "bell", function(e) if e.hour == 12 then print("noon") end end)
```

- `clock.start{...}` also takes `noon` (how high the sun climbs in
  degrees, default 62; lower for a northern winter), and `sunrise` and
  `sunset` hours (6 and 18). `clock.set(hour)` alone shows that time and
  keeps it there. `clock.dayLength(seconds)` changes the speed.
- By night the moon (opposite the sun, at least 25 degrees up) lights the
  scene in a pale blue, and the exposure rises so a night level stays
  playable.
- The clock turns the scene's fill light (a mood's `fill`) off and lets
  the sky fill the shadows. Choosing a mood (`mood.set`, or a scene that
  has one) stops the clock. So does `clock.stop()`, which leaves the
  light as it is. Unloading the script that started it stops it too.
- The sun moves every frame. The sky follows when it has changed enough
  to see, at most twice a second, because each new sky is analysed again
  for ambient light and reflections (a few milliseconds on the CPU). At a
  real game speed that's every few seconds around dusk. Even at an hour
  a second, the clock cost about 10% of a frame in our software-rendered
  check.
- In C++: `kke::DayNight` (`kke/DayNight.h`) gives the look at any hour
  (`lookAt`), moves the clock (`advance`, which returns the hours, dawns
  and dusks it passed) and puts the look into a `Lighting` (`apply`).
  `ScriptModule::clock()` is the one Lua uses.

Not yet: stars and the moon drawn in the sky, clouds that move, weather
(ENG-F81), and keeping every player's clock together in multiplayer.

## In C++

`app.setMood(name)` loads and applies a mood; `app.mood()` is the last one
set. Everything it sets is plain data on `app.lighting()` you can change
yourself, every frame if you like (a day-night cycle is a sun direction
and a few colours changing over time):

```cpp
kke::Lighting& light = app.lighting();
light.sky.kind = kke::Sky::Kind::Gradient;   // None, Gradient or Image
light.sky.zenith = {0.05f, 0.2f, 0.7f};
light.fog.enabled = true;
light.fog.density = 0.01f;
kke::ColorGrade::preset("golden", light.grade);
light.exposure = 1.1f;
```

`kke::loadMood(name)` returns the `kke::Mood` without applying it, to
change a value first; `kke::listMoods()` lists the names it can find.
The types are in `engine/include/kke/Sky.h` and `Mood.h`.

## How it's drawn

- The sky is one full-screen triangle at the far plane, drawn first in
  every view (`kke::SkyRenderer`, `shaders/sky.frag`), then fogged and
  tone mapped like every lit surface so the horizon meets the land's haze
  in the same colour. An image sky is uploaded once as half floats and
  sampled at full resolution with no mip levels (no seam where it wraps).
- The sky's light is projected to order-2 spherical harmonics (nine
  colours; Ramamoorthi and Hanrahan, 2001) once when the sky changes, with
  the sun left out (it's the key light already). Lit shaders evaluate it
  per pixel for their ambient. With no sky it is exactly the old flat
  `ambientColor`.
- All of it lives in the lighting buffer every lit shader already reads
  (`shaders/lighting_ubo.glsl`): no extra passes, no temporal
  accumulation, nothing dithered ([RENDERING_PRINCIPLES.md](RENDERING_PRINCIPLES.md)).
- Shiny surfaces reflect the sky (`kke::EnvMap`): the same fill light,
  sun left out, prefiltered once per sky into six levels from mirror to
  fully rough (GGX, Karis 2013) and sampled along the reflection,
  weighted by Karis's analytic environment BRDF. Metals take the sky's
  colour and every surface brightens towards it at glancing angles.
  `ambientStrength` and `ambientSaturation` apply to it as to the fill.
  Only the sky is reflected, not the scene around it.
- Not yet: clouds that move, and a physically simulated atmosphere for
  skies at any time of day.
