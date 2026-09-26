#version 450
// Screen-space fluid, pass 1 (kke::FluidSurfaceRenderer): each particle
// as a sphere, writing its linear view depth (R32F) and colour + glow.
layout(location = 0) in vec3 fragCenter;
layout(location = 1) in vec3 fragColor;
layout(location = 2) in vec3 fragParams;   // radius, glow, roughness
layout(location = 3) in vec2 fragCorner;
layout(location = 0) out float outDepth;
layout(location = 1) out vec4 outColor;

struct GPULight { vec4 directionOrPosition; vec4 colorIntensity; };
layout(set = 0, binding = 0) uniform LightingUBO {
    GPULight lights[4];
    vec4 ambient;
    vec4 cameraPos;
    mat4 lightViewProj;
    mat4 viewProj;
} lighting;
layout(push_constant) uniform PC { vec4 camForward; } pc;

void main() {
    float r2 = dot(fragCorner, fragCorner);
    if (r2 > 1.0) discard;
    vec3 toCam = normalize(lighting.cameraPos.xyz - fragCenter);
    vec3 right = normalize(cross(abs(toCam.y) > 0.99 ? vec3(1, 0, 0) : vec3(0, 1, 0), toCam));
    vec3 up = cross(toCam, right);
    vec3 N = normalize(right * fragCorner.x + up * fragCorner.y + toCam * sqrt(1.0 - r2));
    vec3 P = fragCenter + N * fragParams.x;
    vec4 clip = lighting.viewProj * vec4(P, 1.0);
    gl_FragDepth = clip.z / clip.w;
    outDepth = dot(P - lighting.cameraPos.xyz, pc.camForward.xyz);
    outColor = vec4(fragColor, fragParams.y);
}
