#version 450
#extension GL_GOOGLE_include_directive : require
// Particle effects (kke::ParticleEffects): one quad each (6 vertices),
// facing the camera. A smoke puff or a glow is a round sprite turned by
// its own spin; a spark is a streak drawn from where it is back along its
// velocity (motion blur), so a shower of them reads as lines; a flake is a
// card squashed across as it turns over (inExtra.x, 0..1); a ring lies
// flat on the ground when inExtra.x is -1.
layout(location = 0) in vec3 inCenter;
layout(location = 1) in vec3 inColor;   // smoke: sRGB albedo; spark: linear light (HDR)
layout(location = 2) in vec4 inParams;  // x = radius (m), y = opacity 0..1, z = spin (rad), w = seed
layout(location = 3) in vec3 inVelocity; // sparks: streak = velocity x stretch seconds (m)
layout(location = 4) in vec2 inCorner;  // -1..1
layout(location = 5) in vec2 inExtra;   // flake: (squash 0..1, shape); ring: (-1 flat / -2 facing, band width)

#include "lighting_ubo.glsl"

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec4 fragParams;
layout(location = 2) out vec2 fragUV;    // smoke: the sprite turned by spin; sparks: across (x) and along (y)
layout(location = 3) out vec3 fragWorld;
layout(location = 4) out vec2 fragExtra;

void main() {
    vec3 toCam = normalize(lighting.cameraPos.xyz - inCenter);
    vec3 world;
    float streak = length(inVelocity);
    if (streak > 1e-4) {
        // Sparks: a thin quad from the head back along the streak, facing the camera.
        vec3 along = inVelocity / streak;
        vec3 side = cross(along, toCam);
        float sl = length(side);
        side = sl > 1e-4 ? side / sl : normalize(cross(abs(toCam.y) > 0.99 ? vec3(1, 0, 0) : vec3(0, 1, 0), toCam));
        float t = inCorner.y * 0.5 + 0.5; // 0 = tail, 1 = head
        world = inCenter - inVelocity * (1.0 - t) + side * inCorner.x * inParams.x + along * inCorner.y * inParams.x;
        fragUV = inCorner;
    } else if (inExtra.x == -1.0) {
        // A flat ring on the ground.
        world = inCenter + vec3(inCorner.x, 0.0, inCorner.y) * inParams.x;
        fragUV = inCorner;
    } else if (inExtra.x > 0.0) {
        // A flake: the card turned by its spin, squashed across as it turns over.
        vec3 right = normalize(cross(abs(toCam.y) > 0.99 ? vec3(1, 0, 0) : vec3(0, 1, 0), toCam));
        vec3 up = cross(toCam, right);
        float c = cos(inParams.z), s = sin(inParams.z);
        vec2 k = mat2(c, s, -s, c) * vec2(inCorner.x * inExtra.x, inCorner.y);
        world = inCenter + (right * k.x + up * k.y) * inParams.x;
        fragUV = inCorner;
    } else {
        vec3 right = normalize(cross(abs(toCam.y) > 0.99 ? vec3(1, 0, 0) : vec3(0, 1, 0), toCam));
        vec3 up = cross(toCam, right);
        world = inCenter + (right * inCorner.x + up * inCorner.y) * inParams.x;
        float c = cos(inParams.z), s = sin(inParams.z);
        fragUV = mat2(c, s, -s, c) * inCorner;
    }
    gl_Position = lighting.viewProj * vec4(world, 1.0);
    fragColor = inColor;
    fragParams = inParams;
    fragWorld = world;
    fragExtra = inExtra;
}
