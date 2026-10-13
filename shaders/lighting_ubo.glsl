#ifndef KKE_LIGHTING_UBO_GLSL
#define KKE_LIGHTING_UBO_GLSL
// The lighting uniform buffer (set 0, binding 0), shared by every lit
// shader, and the helpers that read it: ambient light from the sky, the
// gradient sky, height fog and the tone curve with its look.
// Mirrors kke::LightingBuffer's LightingUBOData (engine/src/LightingBuffer.cpp)
// byte for byte; kke::Lighting (Application.h) is what fills it in.
// Every field is a vec4 so std140 adds no hidden padding.

#define SHADOW_CASCADES 4 // kke::ShadowMap::kCascades
#define POINT_SHADOW_FACES 24 // kke::ShadowMap::kMaxPointShadows * kke::ShadowMap::kPointFaces
#define MAX_LIGHTS 16 // kke::Lighting::kMaxLights

struct GPULight {
    vec4 directionOrPosition; // xyz = direction or position; w = 1.0 if positional (point), 0.0 if directional
    vec4 colorIntensity;      // rgb = color; a = intensity (<=0 means disabled)
    vec4 params;              // x = range in metres (0 = no falloff), y = point-shadow slot (0..3, -1 = none)
};

layout(set = 0, binding = 0) uniform LightingUBO {
    GPULight lights[MAX_LIGHTS];
    vec4 ambient;   // rgb = average ambient colour; a = shadows on (1) / off (0)
    vec4 cameraPos; // rgb = world-space camera position, for specular
    mat4 lightViewProj;
    mat4 viewProj; // see cube.vert's own comment on why this is here
    vec4 toneParams; // x = tone mapper (tonemap.glsl), y = exposure
    vec4 lookSlope;  // rgb = ASC CDL slope, a = saturation
    vec4 lookOffset; // rgb = offset, a = 1 when a look is set
    vec4 lookPower;  // rgb = power
    vec4 skyZenith;  // rgb, a = sky kind (0 none, 1 gradient, 2 image)
    vec4 skyHorizon; // rgb, a = horizon falloff
    vec4 skyGround;  // rgb, a = image yaw (radians)
    vec4 sunDisc;    // rgb = sun colour x disc intensity, a = cos(sun radius)
    vec4 sunGlow;    // rgb = sun colour x glow strength, a = image sky scale
    vec4 fogColor;   // rgb = fog colour (already the sky's when it follows it), a = density (0 = no fog)
    vec4 fogParams;  // x = height falloff, y = fog height, z = max opacity, w = sun scatter
    vec4 ambientSH[9]; // ambient light, order-2 spherical harmonics (kke::SkySH)
    vec4 shadowTiles[SHADOW_CASCADES];        // each cascade's tile of the shadow map: xy = top left, zw = size (0..1)
    vec4 cascadeScaleOffset[SHADOW_CASCADES]; // cascade c's clip xy = lightViewProj's (the far cascade's) xy * .xy + .zw

    mat4 pointShadowViewProj[POINT_SHADOW_FACES]; // index light * 6 + face: that face's view-projection, kke::ShadowMap::pointFaceViewProj
    vec4 pointShadowTiles[POINT_SHADOW_FACES]; // index light * 6 + face: its tile of the point map, xy = top left, zw = size, 0..1
    vec4 envMap; // x = the reflections' top mip (kke::EnvMap levels - 1), 0 = no map: reflect the ambient SH
} lighting;

// The sky prefiltered for reflections (kke::EnvMap, kke::LightingBuffer::setEnvironmentMap):
// equirectangular, one mip per roughness level. A black 1x1 while there is none.
layout(set = 0, binding = 1) uniform sampler2D skyReflections;

#include "tonemap.glsl"

// Ambient light reaching a surface facing n: kke::SkySH::irradiance.
vec3 ambientIrradiance(vec3 n) {
    vec3 r = lighting.ambientSH[0].rgb * 0.282095;
    r += lighting.ambientSH[1].rgb * (0.488603 * n.y) + lighting.ambientSH[2].rgb * (0.488603 * n.z)
       + lighting.ambientSH[3].rgb * (0.488603 * n.x);
    r += lighting.ambientSH[4].rgb * (1.092548 * n.x * n.y) + lighting.ambientSH[5].rgb * (1.092548 * n.y * n.z);
    r += lighting.ambientSH[6].rgb * (0.315392 * (3.0 * n.z * n.z - 1.0));
    r += lighting.ambientSH[7].rgb * (1.092548 * n.x * n.z) + lighting.ambientSH[8].rgb * (0.546274 * (n.x * n.x - n.y * n.y));
    return max(r, vec3(0.0));
}

// What a surface of perceptual roughness `roughness` reflects of the sky
// towards r: the prefiltered map at that roughness's mip (kke::EnvMap level
// i is roughness i / (levels - 1)), or the ambient SH when there is no map.
vec3 skyReflection(vec3 r, float roughness) {
    float top = lighting.envMap.x;
    if (top <= 0.0) return ambientIrradiance(r);
    vec2 uv = vec2(0.5 + atan(r.x, -r.z) / (2.0 * 3.14159265), acos(clamp(r.y, -1.0, 1.0)) / 3.14159265); // kke::equirectUv
    return textureLod(skyReflections, uv, roughness * top).rgb;
}

// The share of the environment a surface reflects (the split sum's second
// factor), Karis's analytic fit: kke::envBRDFApprox.
vec3 envBRDFApprox(vec3 specularColor, float roughness, float NdotV) {
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
    const vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
    vec4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * NdotV)) * r.x + r.y;
    vec2 ab = vec2(-1.04, 1.04) * a004 + r.zw;
    return specularColor * ab.x + ab.y;
}

int skyKind() { return int(lighting.skyZenith.a + 0.5); }

// How much of light i reaches worldPos: 1 for the sun and for point lights with no range, params.x <= 0; otherwise (1 - (d / range)^4)^2, 1 at the light and 0 at range, as kke::Light::falloffAt.
float lightFalloff(int i, vec3 worldPos) {
    float range = lighting.lights[i].params.x;
    if (lighting.lights[i].directionOrPosition.w < 0.5 || range <= 0.0) return 1.0;
    float t = length(lighting.lights[i].directionOrPosition.xyz - worldPos) / range;
    float w = clamp(1.0 - t * t * t * t, 0.0, 1.0);
    return w * w;
}

// The gradient part of the sky: kke::gradientSkyRadiance.
vec3 skyGradient(vec3 d) {
    if (d.y >= 0.0) {
        float t = pow(clamp(d.y, 0.0, 1.0), 1.0 / max(lighting.skyHorizon.a, 0.05));
        return mix(lighting.skyHorizon.rgb, lighting.skyZenith.rgb, t);
    }
    float t = 1.0 - pow(1.0 - clamp(-d.y, 0.0, 1.0), 8.0);
    return mix(lighting.skyHorizon.rgb, lighting.skyGround.rgb, t);
}

vec3 sunDirection() { return normalize(-lighting.lights[0].directionOrPosition.xyz); }

// Henyey-Greenstein phase function: the standard single-lobe model of
// light scattered forward by haze (the sun's halo).
float phaseHG(float cosTheta, float g) {
    float g2 = g * g;
    return (1.0 - g2) / (4.0 * 3.14159265 * pow(max(1.0 + g2 - 2.0 * g * cosTheta, 1e-4), 1.5));
}

// Colour of the air looking along `rd`: the fog colour, glowing towards the sun.
vec3 fogInscatter(vec3 rd) {
    vec3 base = lighting.fogColor.rgb;
    float s = pow(max(dot(rd, sunDirection()), 0.0), 8.0) * lighting.fogParams.w;
    vec3 sunTint = lighting.lights[0].colorIntensity.rgb;
    return base + sunTint * s * dot(base, vec3(0.2126, 0.7152, 0.0722));
}

// How much fog lies between the camera and a point `dist` away along
// `rd` (normalised): exponential height fog integrated analytically
// (Inigo Quilez, "Better Fog"). dist < 0 means "to infinity" (the sky).
float fogAmount(vec3 ro, vec3 rd, float dist) {
    float a = lighting.fogColor.a;
    if (a <= 0.0) return 0.0;
    float b = max(lighting.fogParams.x, 1e-4);
    float h = max(ro.y - lighting.fogParams.y, -40.0 / b); // keeps exp() finite deep under the fog height
    float base = a * exp(-h * b);
    float depth;
    if (dist < 0.0) {
        // Upward rays leave the fog; level or downward rays never do.
        depth = rd.y > 1e-3 ? base / (b * rd.y) : 1e6;
    } else {
        float k = dist * rd.y * b;
        depth = abs(k) < 1e-4 ? base * dist : base * (1.0 - exp(-k)) / (b * rd.y);
    }
    return clamp(1.0 - exp(-depth), 0.0, lighting.fogParams.z);
}

vec3 applyFog(vec3 color, vec3 worldPos) {
    if (lighting.fogColor.a <= 0.0) return color;
    vec3 ro = lighting.cameraPos.xyz;
    vec3 d = worldPos - ro;
    float dist = length(d);
    if (dist < 1e-4) return color;
    vec3 rd = d / dist;
    return mix(color, fogInscatter(rd), fogAmount(ro, rd, dist));
}

// Scene light -> display: exposure, tone curve and the look.
vec3 displayColor(vec3 c) {
    return toneMapLook(c, lighting.toneParams, lighting.lookSlope, lighting.lookOffset, lighting.lookPower);
}

#endif
