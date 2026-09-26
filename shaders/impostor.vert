#version 450
// Sphere impostors (kke::SphereImpostorRenderer): each sphere is 6
// vertices of one camera-facing quad; impostor.frag shades it as a real
// lit sphere. Packed into kke::Vertex: position = centre, color = albedo
// (sRGB), normal = (radius, glow 0..1, roughness), uv = quad corner (-1..1).
layout(location = 0) in vec3 inCenter;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec3 inParams;
layout(location = 3) in vec2 inCorner;

struct GPULight { vec4 directionOrPosition; vec4 colorIntensity; };
layout(set = 0, binding = 0) uniform LightingUBO {
    GPULight lights[4];
    vec4 ambient;
    vec4 cameraPos;
    mat4 lightViewProj;
    mat4 viewProj;
} lighting;

layout(location = 0) out vec3 fragCenter;
layout(location = 1) out vec3 fragColor;
layout(location = 2) out vec3 fragParams;
layout(location = 3) out vec2 fragCorner;

void main() {
    vec3 toCam = normalize(lighting.cameraPos.xyz - inCenter);
    vec3 right = normalize(cross(abs(toCam.y) > 0.99 ? vec3(1, 0, 0) : vec3(0, 1, 0), toCam));
    vec3 up = cross(toCam, right);
    // 1.15x: perspective makes a sphere's silhouette slightly larger than
    // its radius; the fragment shader discards what's outside it.
    vec3 world = inCenter + (right * inCorner.x + up * inCorner.y) * inParams.x * 1.15;
    gl_Position = lighting.viewProj * vec4(world, 1.0);
    fragCenter = inCenter;
    fragColor = inColor;
    fragParams = inParams;
    fragCorner = inCorner * 1.15;
}
