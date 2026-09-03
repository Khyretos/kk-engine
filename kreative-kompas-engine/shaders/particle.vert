#version 450

struct Particle {
    vec4 position;
    vec4 velocity;
};

layout(std430, binding = 0) readonly buffer ParticleBuffer {
    Particle particles[];
};

layout(push_constant) uniform ParticlePushConstants {
    mat4 viewProj;
} pc;

layout(location = 0) out float outLifeFraction;

void main() {
    Particle p = particles[gl_VertexIndex];
    float lifeFraction = clamp(p.position.w / max(p.velocity.w, 0.0001), 0.0, 1.0);
    outLifeFraction = lifeFraction;

    gl_Position = pc.viewProj * vec4(p.position.xyz, 1.0);
    gl_PointSize = mix(2.0, 12.0, lifeFraction);
}
