// kke::AnimeRenderer's shared declarations (docs/ANIME.md): the push
// constants and the material's MToon settings (set 2), mirrored by
// AnimeRenderer.cpp's AnimePush and ToonGpu.

layout(push_constant) uniform AnimePush {
    mat4 model;
    vec4 head;    // xyz: the head's centre (world), w: how far the face's normals bend to its sphere
    vec4 forward; // xyz: the face's forward (world), w: outline width scale
    vec4 look;    // x: least shade hardness, y: angel ring, z: rim, w: the face's outline share
} pc;

layout(set = 2, binding = 0) uniform sampler2D mainTexture;
layout(set = 2, binding = 1) uniform sampler2D shadeTexture;
layout(set = 2, binding = 2) uniform sampler2D normalTexture;
layout(set = 2, binding = 3) uniform sampler2D matcapTexture;
layout(set = 2, binding = 4) uniform sampler2D emissionTexture;
layout(set = 2, binding = 5) uniform sampler2D outlineWidthTexture;
layout(set = 2, binding = 6) uniform ToonParams {
    vec4 color;     // rgb: lit colour (sRGB), a: alpha
    vec4 shade;     // rgb: shade colour (sRGB), w: shade shift
    vec4 params;    // x: shade toony, y: receive shadow, z: sky light, w: alpha cutoff (-1 = none)
    vec4 rim;       // rgb: rim colour, w: fresnel power
    vec4 rim2;      // x: rim lift, y: rim lighting mix, z: normal map scale, w: kind (0 other, 1 face, 2 hair, 3 eye, 4 face part)
    vec4 emission;  // rgb: emission, w: light colour attenuation
    vec4 outline;   // rgb: outline colour (sRGB), w: width in cm (0 = none)
    vec4 outline2;  // x: fixed colour (1) or the surface's, y: lighting mix, z: width in screen percent, w: alpha mode (0 opaque, 1 cutout, 2 blend)
} toon;

const float KIND_FACE = 1.0, KIND_HAIR = 2.0, KIND_EYE = 3.0, KIND_FACE_PART = 4.0;
