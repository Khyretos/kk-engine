#version 450
#extension GL_GOOGLE_include_directive : require
// Screen-space fluid, pass 3: rebuild the liquid surface from the blurred
// depth — position from depth + inverse projection, normal from depth
// differences (the smaller of the two one-sided differences, so edges
// don't smear) — then light it: diffuse, sun glint, Fresnel sky reflection
// and incandescent glow. Writes real depth so the scene occludes it.
layout(location = 0) in vec2 ndc;
layout(location = 0) out vec4 outColor;

struct GPULight { vec4 directionOrPosition; vec4 colorIntensity; };
layout(set = 0, binding = 0) uniform LightingUBO {
    GPULight lights[4];
    vec4 ambient;
    vec4 cameraPos;
    mat4 lightViewProj;
    mat4 viewProj;
} lighting;
layout(set = 1, binding = 0) uniform sampler2D fluidDepth;
layout(set = 1, binding = 1) uniform sampler2D fluidColor;
layout(push_constant) uniform PC { mat4 invProj; mat4 invView; } pc;

#include "glow.glsl"

vec3 viewPos(vec2 uv) {
    float z = texture(fluidDepth, uv).r;
    vec4 v = pc.invProj * vec4(uv * 2.0 - 1.0, 1.0, 1.0);
    vec3 dir = v.xyz / v.w;
    return dir / -dir.z * z;          // point at linear depth z along the pixel's ray
}
vec3 srgbToLinear(vec3 c) { return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(0.04045, c)); }

void main() {
    vec2 uv = ndc * 0.5 + 0.5;
    float z = texture(fluidDepth, uv).r;
    if (z <= 0.0) discard;
    vec2 texel = 1.0 / vec2(textureSize(fluidDepth, 0));
    vec3 P = viewPos(uv);
    vec3 ddx = viewPos(uv + vec2(texel.x, 0)) - P, ddx2 = P - viewPos(uv - vec2(texel.x, 0));
    vec3 ddy = viewPos(uv + vec2(0, texel.y)) - P, ddy2 = P - viewPos(uv - vec2(0, texel.y));
    if (abs(ddx2.z) < abs(ddx.z)) ddx = ddx2;
    if (abs(ddy2.z) < abs(ddy.z)) ddy = ddy2;
    vec3 Nview = normalize(cross(ddx, ddy));
    if (Nview.z < 0.0) Nview = -Nview;           // face the camera (view space looks down -z)
    vec3 N = normalize(mat3(pc.invView) * Nview);
    vec3 Pw = (pc.invView * vec4(P, 1.0)).xyz;
    vec4 clip = lighting.viewProj * vec4(Pw, 1.0);
    gl_FragDepth = clip.z / clip.w;

    vec4 c = texture(fluidColor, uv);
    vec3 albedo = srgbToLinear(c.rgb);
    vec3 V = normalize(lighting.cameraPos.xyz - Pw);
    vec3 L = normalize(-lighting.lights[0].directionOrPosition.xyz);
    vec3 sun = lighting.lights[0].colorIntensity.rgb * lighting.lights[0].colorIntensity.a;
    float ndl = max(dot(N, L), 0.0);
    vec3 H = normalize(L + V);
    float spec = pow(max(dot(N, H), 0.0), 120.0);
    float fresnel = 0.03 + 0.97 * pow(1.0 - max(dot(N, V), 0.0), 5.0);
    vec3 sky = mix(vec3(0.35, 0.4, 0.45), vec3(0.6, 0.7, 0.8), clamp(reflect(-V, N).y * 0.5 + 0.5, 0.0, 1.0));
    vec3 color = albedo * (lighting.ambient.rgb + sun * ndl * 0.8) + sun * spec * 0.6;
    color = mix(color, sky, fresnel * 0.5);
    color = color / (color + vec3(1.0));
    outColor = vec4(color + glowColor(c.a), 1.0);
}
