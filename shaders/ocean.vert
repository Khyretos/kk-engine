#version 450
// Gerstner ocean (kke::OceanWaves / kke::OceanRenderer). Same formulas as
// Ocean.cpp so floating objects ride exactly the waves drawn here. The
// grid is a flat patch in kke::Vertex position.xz; it follows the camera,
// snapped to whole grid cells so it doesn't swim.
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec2 inUV;

layout(push_constant) uniform Waves {
    vec4 wave[4];   // dir.xy, k, amplitude
    vec4 q;         // Gerstner Q per wave
    vec4 omega;     // angular frequency per wave
    vec4 phase;     // phase per wave
    vec4 misc;      // x time, y origin.x, z origin.z, w sea level
} pc;

struct GPULight { vec4 directionOrPosition; vec4 colorIntensity; };
layout(set = 0, binding = 0) uniform LightingUBO {
    GPULight lights[4];
    vec4 ambient;
    vec4 cameraPos;
    mat4 lightViewProj;
    mat4 viewProj;
} lighting;

layout(location = 0) out vec3 fragPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out float fragCrest;

void main() {
    vec2 p = inPosition.xz + pc.misc.yz;
    vec3 pos = vec3(p.x, pc.misc.w, p.y);
    vec3 n = vec3(0.0, 1.0, 0.0);
    float crest = 0.0, ampSum = 0.0;
    for (int i = 0; i < 4; ++i) {
        vec2 d = pc.wave[i].xy;
        float k = pc.wave[i].z, a = pc.wave[i].w;
        float theta = k * dot(d, p) - pc.omega[i] * pc.misc.x + pc.phase[i];
        float c = cos(theta), s = sin(theta);
        pos.x += pc.q[i] * a * d.x * c;
        pos.z += pc.q[i] * a * d.y * c;
        pos.y += a * s;
        // Analytic Gerstner normal (GPU Gems 1, ch. 1).
        float wa = k * a;
        n.x -= d.x * wa * c;
        n.z -= d.y * wa * c;
        n.y -= pc.q[i] * wa * s;
        crest += a * s;
        ampSum += a;
    }
    fragPos = pos;
    fragNormal = normalize(n);
    fragCrest = ampSum > 0.0 ? crest / ampSum : 0.0;
    gl_Position = lighting.viewProj * vec4(pos, 1.0);
}
