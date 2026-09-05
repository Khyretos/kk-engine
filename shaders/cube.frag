#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragNormalWorld;
layout(location = 2) in vec3 fragPosWorld;
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
} lighting;

void main() {
    vec3 normal = normalize(fragNormalWorld);
    vec3 viewDir = normalize(lighting.cameraPos.xyz - fragPosWorld);

    vec3 result = lighting.ambient.rgb;

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

        result += lightColor * (diffuse + specular);
    }

    outColor = vec4(fragColor * result, 1.0);
}
