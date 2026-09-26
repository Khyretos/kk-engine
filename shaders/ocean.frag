#version 450
// Stylised-but-physical sea: Fresnel mix of a sky reflection and the
// water's own deep colour, sun glint, and foam on the highest crests.
// Opaque on purpose (no refraction pass): cheap on every GPU, and what
// many shipped games do for open ocean.
layout(location = 0) in vec3 fragPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in float fragCrest;
layout(location = 0) out vec4 outColor;

struct GPULight { vec4 directionOrPosition; vec4 colorIntensity; };
layout(set = 0, binding = 0) uniform LightingUBO {
    GPULight lights[4];
    vec4 ambient;
    vec4 cameraPos;
    mat4 lightViewProj;
    mat4 viewProj;
} lighting;

vec3 skyColor(vec3 dir) {
    float h = clamp(dir.y, 0.0, 1.0);
    return mix(vec3(0.62, 0.74, 0.86), vec3(0.18, 0.38, 0.72), pow(h, 0.5)); // linear
}

void main() {
    vec3 N = normalize(fragNormal);
    vec3 V = normalize(lighting.cameraPos.xyz - fragPos);
    vec3 L = normalize(-lighting.lights[0].directionOrPosition.xyz);
    vec3 sunColor = lighting.lights[0].colorIntensity.rgb * lighting.lights[0].colorIntensity.a;
    float cosTheta = max(dot(N, V), 0.0);
    float fresnel = 0.02 + 0.98 * pow(1.0 - cosTheta, 5.0);
    vec3 R = reflect(-V, N);
    R.y = abs(R.y);
    vec3 deep = vec3(0.004, 0.035, 0.07);
    vec3 scatter = vec3(0.02, 0.16, 0.18) * max(0.0, fragCrest + 0.3) * max(dot(L, vec3(0, 1, 0)), 0.0); // light through wave tops
    vec3 water = deep + scatter;
    vec3 color = mix(water, skyColor(R), fresnel);
    // Sun glint (Blinn-Phong, very tight).
    vec3 H = normalize(L + V);
    color += sunColor * pow(max(dot(N, H), 0.0), 600.0) * 1.5;
    // Foam where the surface is highest and steepest.
    float foam = smoothstep(0.55, 0.9, fragCrest) * (1.0 - N.y) * 6.0;
    color = mix(color, vec3(0.85, 0.9, 0.92), clamp(foam, 0.0, 0.8));
    // Distance haze into the horizon colour.
    float dist = length(lighting.cameraPos.xyz - fragPos);
    color = mix(color, skyColor(vec3(0.0, 0.02, 0.0)), smoothstep(40.0, 140.0, dist));
    color = color / (color + vec3(1.0)); // same Reinhard as the lit meshes
    outColor = vec4(color, 1.0);
}
