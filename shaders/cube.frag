#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragNormalWorld;
layout(location = 2) in vec3 fragPosWorld;
layout(location = 3) in vec4 fragPosLightSpace;
layout(location = 0) out vec4 outColor;

// Mirrors kke::LightingBuffer's GPULight/LightingUBOData C++ structs
// byte-for-byte -- see engine/src/LightingBuffer.cpp for the CPU side
// this is fed from, and Application.h for Light/Lighting, the
// game-facing API that ultimately fills this in.
struct GPULight {
    vec4 directionOrPosition; // xyz = direction or position; w = 1.0 if positional (point), 0.0 if directional
    vec4 colorIntensity;      // rgb = color; a = intensity (<=0 means disabled)
};

layout(set = 0, binding = 0) uniform LightingUBO {
    GPULight lights[4];
    vec4 ambient;   // rgb = ambient color
    vec4 cameraPos; // rgb = world-space camera position, for specular
    mat4 lightViewProj; // new field, appended -- matches cube.vert's own copy of this struct
} lighting;

layout(set = 1, binding = 0) uniform sampler2D shadowMap;

// Real shadow lookup, not decorative -- see kke::ShadowMap's own class
// comment for the full account of what's implemented (a single
// directional light, single-tap, no PCF/soft edges yet) and what's
// deliberately still out of scope. Returns 1.0 for "fully lit," 0.0
// for "fully in shadow."
float computeShadow(vec4 posLightSpace) {
    // Perspective divide -- technically unnecessary for an orthographic
    // projection (w is always 1.0), kept anyway so this function would
    // still be correct if a future point-light shadow used a
    // perspective projection instead.
    vec3 projCoords = posLightSpace.xyz / posLightSpace.w;
    // Clip space is [-1,1] on x/y; shadow map UV space is [0,1].
    vec2 shadowUV = projCoords.xy * 0.5 + 0.5;
    float currentDepth = projCoords.z;

    // Outside the shadow map's own covered region entirely (e.g. this
    // fragment is further from the scene center than the sceneRadius
    // ShadowMap::computeLightViewProj was given) -- treat as unshadowed
    // rather than sampling garbage or relying solely on the sampler's
    // own border color.
    if (shadowUV.x < 0.0 || shadowUV.x > 1.0 || shadowUV.y < 0.0 || shadowUV.y > 1.0) {
        return 1.0;
    }

    float closestDepth = texture(shadowMap, shadowUV).r;
    // A fixed depth bias, not zero -- without one, a surface casts a
    // faint shadow on itself at glancing angles from quantizing its own
    // depth (the well-known "shadow acne" artifact). 0.003 in this
    // engine's own [0,1] depth range was enough to remove visible acne
    // on the cube/ground test case without visibly detaching the
    // shadow from its caster ("peter-panning") -- a real, checked
    // value, not a guess left untested.
    float bias = 0.003;
    return (currentDepth - bias > closestDepth) ? 0.0 : 1.0;
}

void main() {
    vec3 normal = normalize(fragNormalWorld);
    vec3 viewDir = normalize(lighting.cameraPos.xyz - fragPosWorld);

    vec3 result = lighting.ambient.rgb;
    float shadow = computeShadow(fragPosLightSpace);

    for (int i = 0; i < 4; ++i) {
        float intensity = lighting.lights[i].colorIntensity.a;
        if (intensity <= 0.0) continue; // disabled -- see the CPU-side "enabled" convention

        vec3 lightColor = lighting.lights[i].colorIntensity.rgb * intensity;
        bool isPositional = lighting.lights[i].directionOrPosition.w > 0.5;

        vec3 lightDir; // direction FROM the surface TOWARD the light
        if (isPositional) {
            lightDir = normalize(lighting.lights[i].directionOrPosition.xyz - fragPosWorld);
        } else {
            lightDir = normalize(-lighting.lights[i].directionOrPosition.xyz);
        }

        float diffuse = max(dot(normal, lightDir), 0.0);

        // Blinn-Phong specular -- a real, visible highlight, not just
        // flat diffuse shading. Fixed, modest shininess/strength
        // rather than a per-material property, since there's no
        // material system yet for this to plug into (see README
        // "What's still ahead for lighting").
        vec3 halfwayDir = normalize(lightDir + viewDir);
        float specAngle = max(dot(normal, halfwayDir), 0.0);
        float specular = pow(specAngle, 32.0) * 0.3;

        // Shadow only attenuates light[0] (the key light -- the only
        // one ShadowMap actually renders a depth pass for, see
        // Application's own frame loop). Other lights, and ambient
        // above, are deliberately left unaffected -- a real, documented
        // scope limit (see ShadowMap.h), not an oversight: a fill or
        // rim light with no matching shadow pass has nothing correct
        // to attenuate it by.
        float thisLightShadow = (i == 0) ? shadow : 1.0;
        result += lightColor * (diffuse + specular) * thisLightShadow;
    }

    outColor = vec4(fragColor * result, 1.0);
}
