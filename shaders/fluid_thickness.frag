#version 450
// Screen-space fluid, clear water (kke::FluidSurfaceRenderer, Settings::clearWater):
// every particle's sphere added up, front and back alike: how much water
// each pixel looks through (r, metres) and how much of it is foam (g).
layout(location = 0) in vec3 fragCenter;
layout(location = 1) in vec3 fragColor;
layout(location = 2) in vec3 fragParams;   // radius, glow, foam
layout(location = 3) in vec2 fragCorner;
layout(location = 0) out vec2 outThickness;
layout(push_constant) uniform PC { vec4 params; } pc; // x = water volume a particle stands for (0 = its sphere's)

void main() {
    float r2 = dot(fragCorner, fragCorner);
    if (r2 > 1.0) discard;
    float radius = fragParams.x;
    // The sphere's chord here, scaled so the spheres together hold the
    // particles' water (they overlap: drawn bigger than the spacing).
    float sphere = 4.18879 * radius * radius * radius;
    float scale = pc.params.x > 0.0 ? pc.params.x / sphere : 1.0;
    float t = 2.0 * radius * sqrt(1.0 - r2) * scale;
    outThickness = vec2(t, t * clamp(fragParams.z, 0.0, 1.0));
}
