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
| Banding in dark gradients (sky, fog) | More precision: an HDR scene target and a 10-bit swapchain where the device has one ([#35](https://github.com/Khyretos/kk-engine/issues/35)) |
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
  (`shaders/pbr_common.glsl`: GGX, up to 4 lights, flat ambient, shadow on
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
- **Render scale** 0.5-1.0 as an opt-in min-spec knob (bilinear blit),
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
- MSAA 2x/4x as a setting, off on min-spec, plus optional SMAA 1x as a
  spatial post filter: [#36](https://github.com/Khyretos/kk-engine/issues/36).

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

## Adopt list (short)

1. No dithering, ever. (rule, done)
2. Every frame correct on its own; nothing depends on temporal
   accumulation. (rule)
3. Specular AA, coverage-preserving alpha mips, anisotropic filtering.
   (done)
4. MSAA as a setting + optional SMAA 1x. (#36)
5. HDR target, one tone map + exposure pass, 10-bit output. (#35)
6. Front-to-back sorting, measured prepass, overdraw view. (#37)
7. Shadow map quality: compare sampler, slope bias, stable projection. (#38)
8. Texture compression at asset cooking. (#39)
9. Don't render the 3D scene behind opaque menus. (#40)
10. LODs by screen size (#23); probe-based, stable ambient and GI
    (#20, #21).
