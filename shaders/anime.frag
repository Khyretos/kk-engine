#version 450
#extension GL_GOOGLE_include_directive : require
// kke::AnimeRenderer: MToon (VRM's toon material) pushed towards the
// gacha look (docs/ANIME.md). Light is a lit and a shade colour split at
// a crisp line; the face is lit as one smooth shape; the hair carries an
// angel ring; a rim of light on the silhouette; MToon's matcap and
// emission on top. The sun (light 0) and its shadow, the sky's light.
layout(location = 0) in vec3 fragPosWorld;
layout(location = 1) in vec3 fragNormalWorld;
layout(location = 2) in vec2 fragUV;
layout(location = 3) in vec4 fragPosLightSpace;
layout(location = 0) out vec4 outColor;

#include "pbr_common.glsl"
#include "anime_common.glsl"

// The normal map, in a frame made from the screen-space derivatives
// (the avatar's vertices carry no tangents).
vec3 mapNormal(vec3 N, float scale) {
    vec3 t = texture(normalTexture, fragUV).xyz * 2.0 - 1.0;
    t.xy *= scale;
    vec3 dp1 = dFdx(fragPosWorld), dp2 = dFdy(fragPosWorld);
    vec2 duv1 = dFdx(fragUV), duv2 = dFdy(fragUV);
    vec3 dp2perp = cross(dp2, N), dp1perp = cross(N, dp1);
    vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
    vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;
    float inv = inversesqrt(max(max(dot(T, T), dot(B, B)), 1e-20));
    return normalize(T * inv * t.x + B * inv * t.y + N * t.z);
}

void main() {
    vec4 texel = texture(mainTexture, fragUV);
    float alpha = texel.a * toon.color.a;
    if (toon.params.w >= 0.0 && alpha < toon.params.w) discard;
    float kind = toon.rim2.w;
    vec3 lit = srgbToLinear(toon.color.rgb) * texel.rgb;
    vec3 shadeCol = srgbToLinear(toon.shade.rgb) * texture(shadeTexture, fragUV).rgb;

    vec3 V = normalize(lighting.cameraPos.xyz - fragPosWorld);
    vec3 N = normalize(fragNormalWorld);
    if (!gl_FrontFacing) N = -N;
    if (kind != KIND_EYE && toon.rim2.z > 0.0) N = mapNormal(N, toon.rim2.z);
    // The face (and what is painted on it) lit as one smooth shape: its
    // normals bent towards a sphere round the head.
    if (kind == KIND_FACE || kind == KIND_FACE_PART) {
        vec3 sphere = normalize(fragPosWorld - pc.head.xyz + pc.forward.xyz * 0.04);
        N = normalize(mix(N, sphere, pc.head.w));
    }

    float intensity = lighting.lights[0].colorIntensity.a;
    vec3 sun = intensity > 0.0 ? lighting.lights[0].colorIntensity.rgb * intensity : vec3(0.0);
    vec3 L = sunDirection();
    float shadow = lighting.ambient.a > 0.5 ? computeShadow(fragPosLightSpace) : 1.0;
    // The face takes no cast shadows from its own nose or fringe: in this
    // style those read as dirt. Everything else takes them half way.
    float receive = (kind == KIND_FACE || kind == KIND_FACE_PART || kind == KIND_EYE) ? 0.0 : toon.params.y * 0.6;

    // MToon's light: half Lambert, cast shadow, then the shade shift and
    // toony band; at least as hard as the look asks.
    float toony = max(toon.params.x, pc.look.x);
    float shift = toon.shade.w;
    float x = dot(N, L) * 0.5 + 0.5;
    x *= mix(1.0, shadow, receive);
    x = x * 2.0 - 1.0;
    float hi = mix(1.0, shift, toony);
    float k = clamp((x - shift) / max(hi - shift, 1e-4), 0.0, 1.0);
    if (kind == KIND_EYE) k = max(k, 0.85); // eyes stay bright

    // The sun's colour, toned down by MToon's light colour attenuation.
    float lum = dot(sun, vec3(0.2126, 0.7152, 0.0722));
    vec3 light = mix(sun, vec3(lum), toon.emission.w) / PI;
    vec3 sky = mix(ambientIrradiance(N), ambientIrradiance(vec3(0.0, 1.0, 0.0)), 0.7);
    vec3 colour = mix(shadeCol, lit, k) * (light + sky * (0.6 + toon.params.z * 2.0));

    // Rim light on the silhouette, mostly where the sun reaches.
    float fresnel = 1.0 - max(dot(N, V), 0.0);
    float rim = pow(clamp(fresnel + toon.rim2.x, 0.0, 1.0), max(toon.rim.w, 0.5));
    vec3 rimLight = srgbToLinear(toon.rim.rgb) + vec3(pc.look.z) * smoothstep(0.55, 0.75, fresnel);
    colour += lit * rimLight * rim * mix(vec3(1.0), light + sky, toon.rim2.y) * mix(0.35, 1.0, k);

    // The angel ring: a band of light round the hair, facing the camera.
    if (kind == KIND_HAIR && pc.look.y > 0.0) {
        vec3 up = normalize(vec3(0.0, 1.0, 0.0) - V * V.y);
        float v = dot(N, up);
        float band = smoothstep(0.22, 0.30, v) * (1.0 - smoothstep(0.42, 0.52, v)) * smoothstep(0.1, 0.4, dot(N, V));
        colour += lit * (light + sky) * band * pc.look.y * mix(0.4, 1.0, k);
    }

    // MToon's matcap (additive) and emission.
    vec3 right = normalize(cross(vec3(0.0, 1.0, 0.0), V));
    vec3 upV = cross(V, right);
    vec2 cap = vec2(dot(N, right), dot(N, upV)) * 0.5 + 0.5;
    colour += srgbToLinear(texture(matcapTexture, vec2(cap.x, 1.0 - cap.y)).rgb) * (light + sky) * 0.5;
    colour += srgbToLinear(toon.emission.rgb) * texture(emissionTexture, fragUV).rgb;

    outColor = vec4(applyFog(colour, fragPosWorld), toon.outline2.w > 1.5 ? alpha : reflectionMark(1.0));
}
