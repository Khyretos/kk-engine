#version 450
#extension GL_GOOGLE_include_directive : require
// The sky behind the scene (kke::SkyRenderer): a gradient with a sun
// disc and halo, or an equirectangular HDR image. Drawn at the far plane
// before anything else, then fogged and tone mapped like every lit
// surface so the horizon meets the ground's haze in the same colour.
layout(location = 0) in vec2 ndc;
layout(location = 0) out vec4 outColor;
layout(push_constant) uniform Sky { mat4 invViewProj; } pc;

#include "lighting_ubo.glsl"

layout(set = 1, binding = 0) uniform sampler2D skyImage;

vec3 imageSky(vec3 d) {
    // Turn the lookup by -yaw so the picture turns by +yaw (kke::Sky::yawDegrees,
    // kke::rotateYaw).
    float yaw = lighting.skyGround.a;
    float c = cos(yaw), s = -sin(yaw);
    vec3 r = vec3(c * d.x + s * d.z, d.y, -s * d.x + c * d.z);
    // kke::equirectUv. One level, sampled at its own size: no mips, so
    // there's no seam where u wraps from 1 back to 0.
    vec2 uv = vec2(0.5 + atan(r.x, -r.z) / (2.0 * 3.14159265), acos(clamp(r.y, -1.0, 1.0)) / 3.14159265);
    return textureLod(skyImage, uv, 0.0).rgb * lighting.sunGlow.a;
}

void main() {
    vec4 a = pc.invViewProj * vec4(ndc, 0.0, 1.0), b = pc.invViewProj * vec4(ndc, 1.0, 1.0);
    vec3 dir = normalize(b.xyz / b.w - a.xyz / a.w);
    vec3 c;
    if (skyKind() == 2) {
        c = imageSky(dir);
    } else {
        c = skyGradient(dir);
        float cosSun = dot(dir, sunDirection());
        // The disc, with an edge one pixel-ish soft so it isn't jagged.
        float edge = fwidth(cosSun) * 1.5 + 1e-6;
        c += lighting.sunDisc.rgb * smoothstep(lighting.sunDisc.a - edge, lighting.sunDisc.a, cosSun) * step(-0.02, dir.y);
        c += lighting.sunGlow.rgb * phaseHG(cosSun, 0.76) * smoothstep(-0.1, 0.05, dir.y); // the halo, gone below the horizon
    }
    c = mix(c, fogInscatter(dir), fogAmount(lighting.cameraPos.xyz, dir, -1.0));
    outColor = vec4(displayColor(c), 1.0);
}
