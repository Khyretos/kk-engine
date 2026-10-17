#ifndef KKE_EFFECT_QUAD_GLSL
#define KKE_EFFECT_QUAD_GLSL
// One particle's quad corner (kke::ParticleEffects), shared by effect.vert
// (particles the CPU moves) and effect_gpu.vert (ones the GPU moves, ENG-G18
// part 4): a smoke puff or a glow is a round sprite turned by its own spin;
// a spark is a streak drawn from where it is back along its velocity
// (motion blur), so a shower of them reads as lines; a flake is a card
// squashed across as it turns over (extra.x, 0..1); a ring lies flat on the
// ground when extra.x is -1. Needs lighting_ubo.glsl and these outputs.

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec4 fragParams;
layout(location = 2) out vec2 fragUV;    // smoke: the sprite turned by spin; sparks: across (x) and along (y)
layout(location = 3) out vec3 fragWorld;
layout(location = 4) out vec2 fragExtra;

// The quad's two triangles, -1..1.
const vec2 kCorners[6] = vec2[6](vec2(-1, -1), vec2(1, -1), vec2(1, 1), vec2(-1, -1), vec2(1, 1), vec2(-1, 1));

// center: where it is; color: smoke sRGB albedo, light linear (HDR);
// params: x = radius (m), y = opacity 0..1, z = spin (rad), w = seed;
// streak: sparks, velocity x stretch seconds (m), else 0;
// extra: flake (squash 0..1, shape), ring (-1 flat / -2 facing, band width).
void effectQuad(vec3 center, vec3 color, vec4 params, vec3 streak, vec2 extra, vec2 corner) {
    vec3 toCam = normalize(lighting.cameraPos.xyz - center);
    vec3 world;
    float streakLength = length(streak);
    if (streakLength > 1e-4) {
        // Sparks: a thin quad from the head back along the streak, facing the camera.
        vec3 along = streak / streakLength;
        vec3 side = cross(along, toCam);
        float sl = length(side);
        side = sl > 1e-4 ? side / sl : normalize(cross(abs(toCam.y) > 0.99 ? vec3(1, 0, 0) : vec3(0, 1, 0), toCam));
        float t = corner.y * 0.5 + 0.5; // 0 = tail, 1 = head
        world = center - streak * (1.0 - t) + side * corner.x * params.x + along * corner.y * params.x;
        fragUV = corner;
    } else if (extra.x == -1.0) {
        // A flat ring on the ground.
        world = center + vec3(corner.x, 0.0, corner.y) * params.x;
        fragUV = corner;
    } else if (extra.x > 0.0) {
        // A flake: the card turned by its spin, squashed across as it turns over.
        vec3 right = normalize(cross(abs(toCam.y) > 0.99 ? vec3(1, 0, 0) : vec3(0, 1, 0), toCam));
        vec3 up = cross(toCam, right);
        float c = cos(params.z), s = sin(params.z);
        vec2 k = mat2(c, s, -s, c) * vec2(corner.x * extra.x, corner.y);
        world = center + (right * k.x + up * k.y) * params.x;
        fragUV = corner;
    } else {
        vec3 right = normalize(cross(abs(toCam.y) > 0.99 ? vec3(1, 0, 0) : vec3(0, 1, 0), toCam));
        vec3 up = cross(toCam, right);
        world = center + (right * corner.x + up * corner.y) * params.x;
        float c = cos(params.z), s = sin(params.z);
        fragUV = mat2(c, s, -s, c) * corner;
    }
    gl_Position = lighting.viewProj * vec4(world, 1.0);
    fragColor = color;
    fragParams = params;
    fragWorld = world;
    fragExtra = extra;
}

#endif
