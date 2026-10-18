# RENDERING_PRINCIPLES.md — a stable, sharp image, earned per frame

This is the rendering doctrine for KKE, written after reviewing the
criticisms of modern real-time rendering made by the YouTube channel
[Threat Interactive](https://www.youtube.com/@threatinteractive)
(2026-09-26). The channel is harsh in tone; this file ignores the tone and
the personal attacks and keeps only the technical claims, weighed against
what this engine actually does today.

It sits next to [OPTIMIZATION.md](OPTIMIZATION.md) (performance doctrine,
measured log, backlog). Anything here that ships gets a row in that log.

## The one hard rule: no dithering

**KKE never uses dithering to render anything.** Not ordered/Bayer or
blue-noise dither, not screen-door or stochastic transparency, not
checkerboard or interleaved rendering, not any effect whose raw output is
a noisy pattern that only looks right after a temporal filter or a blur.
This is the project owner's decision (Kees, 2026-09-26): it makes a game
look bad, and no performance gain changes that. It is also in
[AI_GUIDE.md](../AI_GUIDE.md) as a non-negotiable rule.

What to use instead, by problem:

| Problem that dithering is usually used for | KKE's answer |
|---|---|
| Foliage, fences, hair cards (cutout alpha) | Alpha test + coverage-preserving mips (done, below); MSAA on the edges when MSAA lands |
| See-through objects, fades, LOD cross-fades | Real alpha blending, sorted; or a short opacity fade with blending, not a screen-door pattern |
| Banding in dark gradients (sky, fog) | More precision: an HDR scene target and a 10-bit swapchain where the device has one (both done, ENG-G17) |
| Soft shadow edges | Filtered shadow lookups (PCF, hardware compare), not random taps |
| Cheap volumetrics / AO / reflections | Lower resolution with an edge-aware (bilateral) upsample, or fewer, stable samples; never noise left for TAA |

One caveat to know about: MSAA **alpha-to-coverage** is implemented by
some drivers with a dithered per-pixel sample mask. If we ever use it, it
must be verified on the target drivers to produce no pattern; otherwise we
stay with alpha test.

## How this review was done (and its limits)

- All 33 videos on the channel were listed (titles, lengths).
- Transcripts could **not** be downloaded: YouTube currently answers this
  cloud server with "sign in to confirm you're not a bot" (HTTP 429 /
  `LOGIN_REQUIRED` on every client yt-dlp offers), and the public mirrors
  that proxy captions are blocked the same way. A retry is scheduled; if
  it succeeds this file will be revised.
- What *was* read: each video's full description from the channel feed,
  which for the technical videos includes the **chapter list** and the
  **papers and talks they cite** (SIGGRAPH/GDC material from Crytek,
  Frostbite, Guerrilla, Polyphony, Activision, Valve, AMD). Their argument
  is largely "do what these papers did", so the cited sources were used to
  pin down what each claim means technically. Where the channel's own
  public posts were findable (e.g. their clarification about Nanite and
  quad overdraw), they were used too.
- So the summaries below are the technical positions as they come across
  from chapter structure, cited sources and public statements, **in our
  own words**, not quotes. Treat nuance inside a single video as possibly
  missed.

Videos weighted most (technical, with chapters or sources): 33 *Tutorial:
How to optimize almost every step in modern game rendering*, 28/29
(Crysis 3's deferred MSAA), 26/27 (Fox Engine / MGSV), 25 (textures and
GPU utilisation), 24 (Dead Space menu), 21/22 (The Callisto Protocol
frame breakdown), 32 (vendor-agnostic ray tracing), 19 (top 10 neglected
areas), and the TAA / Nanite / "fake optimization" videos.

## Where KKE stands today (the baseline)

Read from the code on 2026-09-26:

- **Forward renderer**: one scene pass, lit in the fragment shader
  (`shaders/pbr_common.glsl`: GGX, up to 16 lights, flat ambient, shadow on
  light 0). No G-buffer.
- **No anti-aliasing of any kind**: every attachment is
  `VK_SAMPLE_COUNT_1_BIT`, no post AA, nothing temporal.
- **Output straight to an 8-bit sRGB swapchain**, with Reinhard tone
  mapping per channel inside every surface shader. No HDR intermediate.
- **One 2D shadow map**, manual 3x3 PCF (9 single taps), fixed bias.
- **Textures**: RGBA8 uncompressed, CPU-built mips in linear light,
  trilinear; until today no anisotropic filtering.
- **Culling / batching**: frustum culling and instancing done
  (OPTIMIZATION.md #23, #25); no LODs, no depth prepass, no draw sorting.
- **Render scale** 0.5-1.0 as an opt-in min-spec knob (bilinear, in the tone-map pass since ENG-G17),
  default 1.0 (OPTIMIZATION.md #26).
- **Nothing temporal and nothing dithered anywhere.** That is a strong
  starting point for everything below: we don't have to unwind a TAA
  dependency, we just must not grow one.

## The criticisms, one by one

Each entry: what they argue (our words) → is it right → what it means here.

### 1. Temporal AA and upscalers used as a crutch

**Argument.** Modern pipelines render many effects deliberately
under-sampled (half resolution, checkerboard, a few noisy rays per pixel,
dithered transparency) and rely on TAA / TSR / DLSS to accumulate over
frames and hide it. The result is a soft image, ghosting and smearing in
motion, and effects that fall apart when the camera moves. Then upscaling
is sold as "optimization" when it's really a lower resolution.

**Weighing.** The artifacts are real and well documented (Epic's own TAA
talk, which they cite, describes the history-rejection trade-offs). The
counterpoint is also real: TAA was adopted because it cheaply fixes
specular and sub-pixel aliasing that deferred renderers can't fix with
MSAA, and a well-tuned TAA looks better than no AA. Where they are most
clearly right is the *dependency*: once effects are authored to be noisy,
TAA becomes mandatory and can't be turned off.

**For KKE.** Adopt the principle: **every frame must be correct on its
own.** No effect may need temporal accumulation to look right. If a
temporal filter is ever added, it's an optional setting on top of an
already clean image, never something an effect depends on. Our render
scale stays an opt-in min-spec knob, never a budget assumption for
content.

### 2. Non-temporal anti-aliasing: MSAA, SMAA, specular AA

**Argument.** (Videos 28, 29, 33 "AA non-temporal aspect".) MSAA was
dropped because of deferred shading's memory and bandwidth cost, but
Crysis 3 shipped MSAA in a deferred renderer in 2013, and the real cost
drivers (per-sample shading everywhere, full-size MSAA G-buffers) can be
contained: detect edge pixels, shade per-sample only there, and add a
morphological filter (SMAA) for what MSAA misses. Also: aliasing on
shiny surfaces should be treated at the material (specular AA), not
blurred away afterwards.

**Weighing.** Technically sound; Crytek's, Valve's and Jimenez's papers
back every piece. The honest limits: MSAA does nothing for shader
aliasing (specular highlights, alpha-tested textures) by itself, and it
costs memory and bandwidth, which matters on our min-spec (software
rasteriser). So it must be a setting, not a requirement.

**For KKE.** This is the biggest easy win, because **we are forward**:
MSAA in a forward renderer needs no G-buffer tricks at all.

- Specular AA — **done today** (below).
- Coverage-preserving alpha mips — **done today** (below).
- MSAA 2x/4x as a setting, off on min-spec, plus SMAA 1x as a spatial
  post filter: **done** (ENG-F05, below), on by default.
  [#36](https://github.com/Khyretos/kk-engine/issues/36).

### 3. Overdraw, quad utilisation, LODs and Nanite

**Argument.** GPUs shade pixels in 2x2 quads; triangles that cover only
a few pixels waste most of the lanes, so dense meshes cost far more than
their pixel count. Classic LODs keep triangles large on screen. Nanite
avoids quad waste with its own rasteriser but carries a large fixed cost,
and in typical scenes well-authored LODs are faster. (Their later
clarification: they never claimed Nanite itself uses quads; the point is
that a frame of well-sized triangles beats it.)

**Weighing.** The quad-efficiency facts are standard GPU behaviour. The
Nanite comparison depends heavily on content density; for kitbash-heavy
film-quality scenes Nanite wins. For KKE's content (Synty low-poly packs,
play-to-make levels) classic LODs are clearly the right tool.

**For KKE.** LOD selection by screen size, using the LODs Synty packs
already ship, belongs to world streaming/LOD work
([#23](https://github.com/Khyretos/kk-engine/issues/23)); a
triangle-density / quad-overdraw debug view goes with the draw-order work
in [#37](https://github.com/Khyretos/kk-engine/issues/37).

### 4. Pass order, prepass, sorting and resource clears

**Argument.** (Video 33 chapters: resource clearing, "how to properly
prepass content", "how to properly sort basepass content"; video 22's
pipeline breakdown.) Many frames waste time on clears nothing needed,
prepasses that include geometry that never occludes anything, and base
passes drawn in an order that defeats early-Z.

**Weighing.** Correct and uncontroversial; it's the kind of thing only a
GPU capture shows. The payoff depends on overdraw, which we haven't
measured.

**For KKE.** Sort opaque draws front to back (by instance batch), try a
depth prepass for large occluders only and keep it if it measures
faster, and add an overdraw view to see it:
[#37](https://github.com/Khyretos/kk-engine/issues/37). Our depth
attachments already use `STORE_OP_DONT_CARE`; the colour clear is kept
because it's the cheapest way to start a tile on tiled GPUs.

### 5. Shadows: optimized shadow maps beat ray-traced shadows

**Argument.** (Videos 21, 22, 33 "perspective shadow maps".) Ray-traced
shadows were chosen where well-optimized shadow maps would have looked as
good for a fraction of the cost. Shadow maps are fast when their
resolution is spent where the camera looks (perspective / cascaded
projections), when static casters are cached, and when softening is done
by filtering rather than noise.

**Weighing.** Agreed for our scale. RT shadows win for area-light
softness and huge draw distances, which isn't where we are.

**For KKE.** Our single shadow map is fine for small scenes; improve it
in this order: hardware depth-compare sampler (bilinear PCF for free),
slope-scaled bias, texel-snapped light projection (no shimmer when the
camera moves), then cascades as already planned in #20:
[#38](https://github.com/Khyretos/kk-engine/issues/38).

**Ray-traced shadows (ENG-F07, 2026-10-18).** The setting `shadowRays`
(off by default; `KKE_SHADOW_RAYS`) is `kke` or `classic`, on GPUs with
ray query. KKE keeps the argument above: the shadow maps stay, and a ray
towards the sun is cast only where they don't reach (past 75 m, up to
600 m), through the stand-ins, so a far grandstand or hill shades the land
behind it again instead of everything past 80 m being lit. `classic`
casts a ray from every pixel through the full geometry and uses it instead
of the map: hard shadows everywhere, the textbook ray-traced look, for
comparison. Both are cast after the frame from its depth (one compute
dispatch, sharing the frame's `RayScene` with reflections and refraction)
and read by the next frame's lit shaders, reprojected; where a pixel showed
something else last frame, the shadow map answers. Measured on the RX
9070 XT, the racing demo at 1019 x 1384 with 4x MSAA: 0.716 ms off, 0.98
kke (+0.26, mostly building the ray scene of moving cars every frame),
1.16 classic (+0.44).

**Many lights without a deferred path (ENG-F08, 2026-10-18).** The gap
asked for a deferred path; a deferred renderer pays for many lights with
a G-buffer: no MSAA (section 2), no glass, a fat target read back per
light. KKE stays forward and clusters instead (Olsson et al. 2012): the
16 key lights (with shadows) are untouched, and the small lights past
them (`Lighting::manyLights`, up to 1024; script lights spill over there
when the key slots are full) go into a storage buffer. A compute pass
lists per cell of the view (16 x 9 cells, 24 slices spaced exponentially
in depth) the lights whose reach touches it, and each lit pixel walks
only its cell's list. Measured on the RX 9070 XT, anim_demo at night at
1019 x 1384 with 4x MSAA (`KKE_ANIM_LIGHTS`; `KKE_CLUSTERS=0` walks every
light, which is what forward rendering without clusters costs): no lamps
0.27 ms; 256 lamps 0.52 clustered against 1.16; 1024 lamps 0.75 against
2.70. Hair, cloth and see-through surfaces take the 16 key lights only.

### 6. Lighting and GI: stable beats noisy

**Argument.** (Video "Dynamic lighting was better nine years ago",
Fox Engine videos, 33 "WIP GI & hybrid reflections", 32.) Games from
~2015 had convincing dynamic lighting from precomputed or probe-based
indirect light and cheap light volumes; today's per-frame traced GI is
noisy, needs denoising and temporal accumulation, and costs far more. Ray
tracing, when used, should run on any vendor's hardware (compute or
standard ray queries), not be tied to one vendor's features.

**Weighing.** The cost/stability argument is right for most games; the
flexibility of fully dynamic GI (destructible or player-built worlds) is
the real reason engines moved on. Our play-to-make worlds change at
runtime, so fully baked lighting is not enough on its own.

**For KKE.** Prefer probe-based ambient (spherical harmonics or a small
irradiance cubemap, re-baked in the background when the level changes)
over per-frame traced GI. This is already planned in
[#20](https://github.com/Khyretos/kk-engine/issues/20) (image-based
ambient) and [#21](https://github.com/Khyretos/kk-engine/issues/21)
(cheap GI); the principle "stable, no noise, no temporal dependency"
applies to both. Any future ray tracing uses Vulkan's vendor-neutral
`VK_KHR_ray_query`/compute, with a raster fallback.

**Screen-space GI (ENG-F01, 2026-10-17).** An optional setting
(`globalIllumination`, off by default; `KKE_GI=1`) for light bounced
between nearby surfaces and shadow in corners and under things, kept to
this rule: a visibility bitmask (Therrien et al. 2023) over 4 fixed
directions and 8 fixed distances per pixel, the same for every pixel and
every frame, so there is no noise to hide and nothing is blended over
frames. It reads the finished frame and is used by the next one (light
arrives one frame late, reprojected; where that frame didn't see a
surface, the sky alone lights it, as without GI): the one temporal
dependency, accepted for it (Kees, 2026-10-17). Half resolution, read
back with depth-aware weights so it never bleeds across edges. Cost on
the RX 9070 XT at 844 x 1384 with 4x MSAA: 0.29 ms (synty_demo, 722 to
611 fps); on lavapipe it halves the frame rate, which is why it is off by
default. `KKE_GI_VIEW=1` shows what it sees: lit surfaces turn grey by how
much sky they see.

**Reflections (ENG-F02, 2026-10-17).** The setting `reflections` (off by
default; `KKE_REFLECTIONS`) is `screen`, `kke` or `classic`. Screen: lit
opaque surfaces write 2 + smoothness in the scene's alpha (translucents,
particles and the sky write 1 or less, so no extra target or G-buffer is
needed); after the scene a half-resolution pass marches each smooth
surface's reflected ray along its line on screen in 48 equal steps, then
four halvings, and keeps the light where it meets something; the next
frame's lit shaders mix it over the prefiltered sky by smoothness, one
frame late like the GI. One ray, fixed steps: nothing noisy, nothing
blended over frames. Cost on the RX 9070 XT at 844 x 1384 with 4x MSAA:
0.10 ms (anim_demo's polished floor, 420 to 407 fps); lavapipe 13.6 to
12.3 fps. `kke` adds rays through simple stand-ins for what is off
screen, within a budget, and `classic` rays through the full geometry
(both on GPUs with VK_KHR_ray_query; screen elsewhere).

**KKE and classic ray tracing (ENG-F02).** Both trace inline ray queries
(any vendor, lavapipe too) through a `kke::RayScene` that what is drawn
adds itself to each frame: models (ModelModule) and meshes
(DynamicMeshRenderer), skinned parts refitted from their skinned
vertices every frame, so a reflection moves as the character does. A hit
is shaded with its interpolated normal and vertex colour under the sky
and the sun. `classic`: every triangle, every smooth pixel traced
directly, the sun's shadow traced too. `kke` (Kees's ask: keep the look,
cut the cost): screen first, rays only for reflections that leave the
screen, through stand-ins with about a tenth of the triangles over the
same vertices (meshoptimizer), within a ray budget per frame, no shadow
ray. Measured on the RX 9070 XT at 844 x 1384 with 4x MSAA: anim_demo's
polished floor 0.26 ms off, 0.34 screen, 0.58 kke, 0.64 classic; the Synty
demo with every surface reflecting (`KKE_REFLECT_ROUGHNESS=1`) 0.33 off,
0.44 screen, 0.68 kke, 0.72 classic; lavapipe there 33.5 fps off, 22.6
kke, 21.8 classic. On this GPU classic is only a little dearer: KKE's
gains are the budget (a frame never pays more than it allows) and that it
falls back to screen space where there is no ray tracing.

**Refraction (ENG-F02 part 4, 2026-10-18).** With reflections on (one
view; `KKE_REFRACTIONS=0` turns it off for comparisons), see-through
surfaces drawn with `DynamicMeshRenderer::drawTranslucent` bend what is
behind them by their index of refraction (`ior`: water 1.33, glass 1.5).
Before the translucent draws the 3D pass pauses: one compute dispatch
copies the scene drawn so far, with each pixel's distance from the camera
in alpha, and a pass that loads the same colour and depth goes on. The
first translucent pass then reads that copy where the refracted ray
leads, taken as solid for up to a metre behind the surface, times the
transmittance. What a plain grab pass gets wrong: a sample that lands on
something nearer than the glass (in front of it) is not used, so nothing
in front smears into the glass; the straight-through pixel is read
instead. Measured on the RX 9070 XT with 4x MSAA, anim_demo's glass block
(`KKE_ANIM_GLASS=1`): +0.02 to +0.03 ms (905 x 682: 0.209 to 0.237 ms;
1020 x 1384: 0.319 to 0.341 ms); lavapipe at 960 x 540, 39.8 to 38.1 fps.
Translucents seen through other translucents show only the opaque scene
behind both.

With `reflections = kke` or `classic`, refraction traces rays too,
through the same ray scene as reflections, built once a frame while the
3D pass is paused (outside a render pass, as acceleration structures
need). `kke` traces a refracted ray only where the screen could not show
what is behind the surface: the bent spot is off screen or something in
front of the glass covers it. `classic` traces every refracting pixel.
A hit is shaded as reflections' hits are (normal and vertex colour under
the sky and the sun; classic adds the sun's shadow), so classic shows
textured models in their vertex and material colours only. Measured on
the RX 9070 XT, 1288 x 682 with 4x MSAA, anim_demo's glass block: `kke`
0.473 ms without refraction and 0.519 with (+0.05), `classic` 0.503 and
0.795 (+0.29): KKE refraction costs a sixth of classic's. (Lavapipe was
not measured: the build machine was busy with CI, load 24.)

**Volumetric fog (ENG-F03, 2026-10-18).** The graphics setting
`volumetricFog` (off by default; `KKE_VOLUMETRIC_FOG=1`) lights the height
fog for real: a grid of 128 x 72 cells over the view, in 48 slices spaced
exponentially from 0.5 m to 96 m, is lit after the shadow pass (one
compute dispatch: the fog's thickness there, the sky's light, the sun's
where its shadow map says it reaches, the point lights' halos) and
summed per column from the camera outwards (a second dispatch, Hillaire's
energy-conserving step). Every lit shader and the sky read it through
the same `fogAlong()`; past 96 m the analytic fog takes over, so far
hills are fogged exactly as before. Fully lit, a cell sends exactly what
the analytic fog does (`fogInscatter`): turning it on only darkens fog in
the sun's shadow (light shafts) and adds lamp halos. What the usual
froxel fog (Frostbite, Unreal) does differently: it jitters the cells and
relies on TAA to blend the noise over frames, which ghosts behind moving
things. Ours has no jitter and nothing carried between frames: stable on
its own (section 1). Measured on the RX 9070 XT, the Synty demo in the
misty-morning mood, 1328 x 682 with 4x MSAA: 0.331 ms off, 0.377 on
(+0.05; the grid's cost does not grow with the resolution). On lavapipe it
costs too much for a PC without a GPU, so it stays off there.

### 7. Tone mapping, exposure and colour management

**Argument.** (Fox Engine videos, 33 "tone mapping & exposure
mechanics", sources from Polyphony (GT7) and Frostbite.) Much of what
makes an older game look "right" is careful colour handling: HDR
lighting, a tone curve that doesn't shift hues or crush saturation,
proper exposure. Cheap per-channel curves make bright colours go
yellow/white and look fake.

**Weighing.** Right, and it's a quality issue more than a speed one.

**For KKE.** Our per-channel Reinhard inside each surface shader is the
weakest part of the current look: it desaturates highlights, it's
duplicated in `translucent_light.frag`, it happens before blending (so
blended surfaces mix tone-mapped values), and with an 8-bit target there
is no room for bloom or exposure. Move to an HDR scene target, one tone
map + exposure pass, and a 10-bit swapchain where available (which is
also our no-dither answer to banding):
[#35](https://github.com/Khyretos/kk-engine/issues/35). Changing the
curve changes the look of every scene, so it lands as its own reviewed
change, not a silent tweak.

**Chosen (Kees, 2026-09-26): AgX by default, ACES and Reinhard on
request.** The curve now lives once, in `shaders/tonemap.glsl`, and every
lit shader calls it (`pbr_common.glsl`, `translucent_light.frag`,
`ocean.frag`, `fluid_composite.frag`). A game picks it on its lighting:

```cpp
app.lighting().toneMapper = kke::ToneMapper::ACES; // AgX (default), ACES, Reinhard
app.lighting().exposure = 1.2f;                    // scene light is multiplied by this first
```

`KKE_TONEMAP=agx|aces|reinhard|none` forces one for every scene, to compare
them. AgX keeps hues as things get bright (a lit orange wall stays
orange instead of going saturated yellow-orange); ACES has more contrast
and saturation; Reinhard is the old look; `None` shows finished display
colours as they are (the logo intro uses it).

**Done (ENG-G17): one tone-map pass on an HDR scene.** The 3D pass draws
scene light into an RGBA16F image (`Renderer::kSceneColorFormat`); lit,
unlit (grid, particles, debug lines) and additive shaders all write
unbounded light and blend in it. The curve, exposure and look then run
once per pixel in `shaders/tonemap_post.frag`, the first draw of the UI
pass: one full-screen triangle that samples the scene image (a linear
sampler does the render-scale upscale, so there is no blit and no LDR
copy), before the UI draws on top. `Renderer::setClearColor` keeps its
meaning: the scene is cleared to the light the curve shows as that sRGB
colour (`kke::sceneClearColor`, an inverse of the CPU mirror
`kke::toneMapCpu`, which thumbnails use too). Compared with the usual
design (a separate tone-map render pass into an LDR image, then the UI
pass) it saves a render pass, an image and its bandwidth, and the curve
no longer runs once per shaded fragment (overdraw).

**Done (ENG-G17): 10-bit output.** The swapchain is A2B10G10R10 or
A2R10G10B10 (sRGB colour space) wherever the surface offers it
(`kke::chooseSurfaceFormat`; `KKE_SWAPCHAIN_8BIT=1` keeps 8-bit). No sRGB
10-bit format exists, so on such a UNORM swapchain the tone-map pass
encodes sRGB itself, and the UI writes sRGB-encoded colour and blends in
that space (`kTargetEncoded` in the RmlUi shaders, raw ImGui colours), as
a browser does; filters and the thumbnail atlas follow. A 30-bit X
display, which offers nothing but A2R10G10B10, used to get the 8-bit
pipeline's linear colour and showed everything far too dark.

### 8. Textures and GPU utilisation

**Argument.** (Video 25.) Texture bandwidth is often the real
bottleneck. Block compression (BC1/BC4/BC5/BC7) cuts memory and
bandwidth 4-8x at little visual cost, and a frame should balance
arithmetic against memory traffic instead of piling on one.

**Weighing.** Correct and standard practice.

**For KKE.** Anisotropic filtering — **done today**. Compression at asset
cooking (BC7 for colour, BC5 for normals, ASTC on mobile) was already
backlog item 7 in OPTIMIZATION.md; now tracked in
[#39](https://github.com/Khyretos/kk-engine/issues/39).

### 9. Rendering what nobody can see: menus

**Argument.** (Video 24, Dead Space remake.) The full 3D scene kept
rendering at full cost behind an opaque menu, so the menu itself had bad
frame rates.

**For KKE.** When a full-screen opaque UI (marketplace, pause menu) is
open, skip the 3D scene (or reuse the last frame) and draw only the UI:
[#40](https://github.com/Khyretos/kk-engine/issues/40).

### 10. "Fake realism" and post effects

**Argument.** Heavy film grain, chromatic aberration, lens dirt, motion
blur and general blur are used to make images feel "cinematic" but mostly
hide artifacts and make the image harder to read.

**For KKE.** We have none of these. Rule: they may exist later only as
optional look settings, off by default, and never to hide an artifact.

### Where we don't follow them (or can't yet)

- **Velocity compensation, TAA tuning** (video 33's longest chapters):
  not applicable while we have nothing temporal.
- **Deferred-specific MSAA tricks**: we are forward; we get MSAA the easy
  way.
- **Screen-space subsurface scattering, decals**: no current need.
- **Vendor-agnostic ray tracing (video 32)**: agreed in principle, not on
  our roadmap soon.

## Done in this change (2026-09-26)

All three are spatial and per-frame: no temporal filter, no dithering.

1. **Anisotropic filtering** (`kke::Texture`, `VulkanDevice`): the
   `samplerAnisotropy` feature is enabled where the device has it, and
   material samplers use up to 8x. Floors and walls at glancing angles
   stay sharp instead of dropping to a blurry mip.
2. **Specular anti-aliasing** (`pbr_common.glsl`: `specularAAKernel`,
   `shadeSurfaceAA`): the spread of the normal within a pixel (its screen
   derivatives) is added to GGX roughness (Kaplanyan/Tokuyoshi, with
   Filament's constants), so distant or tightly curved shiny surfaces
   don't sparkle. Shaders that `discard` take the kernel before the
   discard, since derivatives after it are undefined.
3. **Coverage-preserving alpha mips** (`kke::TextureMips`): for any
   texture with transparent texels, each mip's alpha is scaled so the
   0.5 alpha test keeps the same fraction of texels as the full-size
   image (Castaño 2010). Without it, box-filtered alpha drifts below the
   cutoff and foliage thins out and vanishes with distance. Unit-tested
   (`tests/test_texture_mips.cpp`).

**Also:** `Application::setSceneCovered(true)` for full-screen opaque
menus (#40): the shadow pass and every module's `render()` are skipped,
so a menu frame costs a clear plus the UI. `prepass()` still runs (the
marketplace's thumbnails are rendered there) with
`PrepassContext::sceneCovered` set, and scene-only prepass work (fluid
surfaces in the showcase and melt demo) returns early. Checked headless
in the Synty demo: 0 draw calls, UI intact, validation clean.

**Also:** opaque model draws are sorted nearest first
(`kke::frontToBackOrder`), inside instanced draws and across them (#37).
The measured depth prepass and the overdraw view remain open in #37.

**Also:** MSAA (#36). `graphics.msaa` in settings.json (1, 2, 4 or 8;
default 4) is read at startup, because every scene pipeline bakes the
sample count in; `KKE_MSAA=n` overrides it, and software rasterisers
(the min-spec reference) run at 1x. With MSAA the 3D pass renders into a
multisampled offscreen target that the render pass resolves, then the
tone-map pass reads the single-sample image (ENG-G17), and UI draws on
top at full resolution. `kke::Pipeline` takes the
sample count from the render pass it's built for
(`VulkanDevice::renderPassSamples`), so modules need no changes; RmlUi
and ImGui pipelines use `Renderer::overlayRenderPass()`. Thumbnails use
`Renderer::createSceneCompatiblePass`, so they're anti-aliased too.
Checked headless at 4x (Synty demo, sandbox with thumbnails, UI
showcase): smoother edges, validation clean. SMAA 1x is still open (#36).

**SMAA (ENG-F05, 2026-10-18).** The graphics setting `smaa` (on by
default, Kees; `KKE_SMAA=0` or `1` wins) runs three compute steps over the
finished scene before the tone map reads it: edges where the perceived
brightness of two neighbours differs by more than 0.1; along each edge,
the run of edge pixels followed both ways (up to 16) and the shape its
two ends stand for (a step of a slanted line, an L corner, a U notch);
then each pixel mixes in the neighbour across the edge by the share of it
the shape's smooth line gives that side. What we do differently from
SMAA 1x: the area comes straight from the line (`kke::smaaLineArea`), not
from a precomputed area texture, so there is no data file to ship and
the CPU copy (`kke/Smaa.h`) is the tests' reference. Tested against 16 x
16 supersampled coverage: slanted edges come out 40% or more closer to
the true picture than aliased ones, and the image's mean brightness
moves by under 2%. One frame, no history: nothing ghosts. Measured on
the RX 9070 XT, the Synty demo at 836 x 1384 with 4x MSAA: 0.356 ms off,
0.422 on (+0.07; the machine was busy with CI, best runs compared). The
software-GPU target keeps it off until it is measured there.

**TAA, an option (ENG-F05 part 2, 2026-10-18).** Kees wanted both: SMAA by
default and TAA beside it. The graphics setting `taa` (off by default;
`KKE_TAA=1` or `0` wins) follows rule 1 above: nothing needs it, it only
adds on top of the clean image. The projection moves by under a pixel each
frame (Halton 2, 3 over 8 frames); after SMAA, one compute dispatch mixes
the frame into the history of earlier ones (10% new), found through the
depth with last frame's camera and read between pixels by a Catmull-Rom
curve (a bilinear read blurred the floor lines a little more each frame);
the history is clipped to the mean and spread of the colours around the
pixel now (YCoCg), so a moving arm leaves no trail. Honest limits: there
are no motion vectors, so on moving things the history is clipped rather
than followed (a little softer in motion, no ghost), and split screen
draws without it. Measured on the RX 9070 XT, the Synty demo at 1019 x 1384
with 4x MSAA and SMAA: 0.393 ms off, 0.448 on (+0.055).

**Later the same day:** shadow map quality (#38). The shadow map is
sampled with a depth-compare sampler (each of the 9 PCF taps is a
hardware bilinear comparison), casters get slope-scaled depth bias from
`ShadowMap::casterConfig()` so the shader bias shrank from 0.003 to
0.0005, and the 15 m shadow region now follows the camera's target,
snapped to whole shadow texels so edges don't shimmer. Cascades remain
in #20.

## Adopt list (short)

1. No dithering, ever. (rule, done)
2. Every frame correct on its own; nothing depends on temporal
   accumulation. (rule)
3. Specular AA, coverage-preserving alpha mips, anisotropic filtering.
   (done)
4. MSAA as a setting (done) + SMAA 1x (done, ENG-F05, on by default). (#36)
5. AgX tone curve with exposure, selectable per game (done); HDR
   target, one tone map pass and 10-bit output (done, ENG-G17).
6. Front-to-back sorting (done), measured prepass, overdraw view. (#37)
7. Shadow map quality: compare sampler, slope bias, stable projection. (done, #38)
8. Texture compression at asset cooking. (#39)
9. Don't render the 3D scene behind opaque menus. (done, #40)
10. LODs by screen size (#23); probe-based, stable ambient and GI
    (#20, #21).
