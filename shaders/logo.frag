#version 450

// Stylised "enamel on polished metal" look for the intro logo: a warm key
// light, a cool fill, a rim that picks out the bevels, a tight highlight
// and a diagonal light sweep (params.y) that crosses the emblem once.

layout(location = 0) in vec3 inColor;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inLogoPos;

layout(push_constant) uniform LogoPush {
    mat4 mvp;
    vec4 normalRows[3];
    vec4 params;
} pc;

layout(location = 0) out vec4 outColor;

vec3 srgbToLinear(vec3 c) {
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(0.04045, c));
}

void main() {
    vec3 n = normalize(inNormal);
    vec3 v = vec3(0.0, 0.0, 1.0);
    vec3 base = srgbToLinear(inColor);

    vec3 keyDir = normalize(vec3(-0.45, 0.65, 0.75));
    vec3 fillDir = normalize(vec3(0.7, -0.35, 0.6));
    float key = max(dot(n, keyDir), 0.0);
    float fill = max(dot(n, fillDir), 0.0);
    float ambient = 0.22 + 0.12 * n.y;

    vec3 color = base * (ambient + 0.95 * key + 0.30 * fill * vec3(0.75, 0.8, 1.0));

    // Glossy highlights: the key light's reflection, strongest on bevels.
    vec3 h = normalize(keyDir + v);
    float spec = pow(max(dot(n, h), 0.0), 60.0);
    color += vec3(1.0, 0.92, 0.82) * spec * 0.55;

    // Rim: edges turned away from the viewer glow in a lighter tint.
    float rim = pow(1.0 - max(n.z, 0.0), 3.0);
    color += mix(base, vec3(1.0), 0.5) * rim * 0.35;

    // The light sweep: a soft diagonal band moving across the logo.
    float band = inLogoPos.x + 0.45 * inLogoPos.y - pc.params.y;
    float sweep = exp(-band * band * 18.0);
    color += vec3(1.0, 0.95, 0.9) * sweep * (0.35 + 0.65 * max(n.z, 0.0)) * 0.9;

    outColor = vec4(color * pc.params.z, 1.0);
}
