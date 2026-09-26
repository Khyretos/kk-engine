#version 450

// The intro's title: "KREATIVE KOMPAS" in white shading to lavender (as on
// the banner) over an orange, letter-spaced "ENGINE". The texture holds
// glyph coverage in alpha; a soft highlight sweeps across once.

layout(location = 0) in vec2 inUV;

layout(set = 0, binding = 0) uniform sampler2D title;

layout(push_constant) uniform TextPush {
    vec4 rect;
    vec4 params; // x alpha, y line split (v), z sheen position (u)
} pc;

layout(location = 0) out vec4 outColor;

vec3 srgbToLinear(vec3 c) {
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(0.04045, c));
}

void main() {
    float coverage = texture(title, inUV).a;
    vec3 color;
    if (inUV.y < pc.params.y) {
        float v = inUV.y / pc.params.y;
        color = mix(vec3(1.0), vec3(0.78, 0.66, 1.0), smoothstep(0.35, 0.95, v));
    } else {
        color = vec3(0.949, 0.573, 0.118); // #F2921E
    }
    float band = inUV.x - pc.params.z + (inUV.y - 0.5) * 0.15;
    color = mix(color, vec3(1.0), exp(-band * band * 120.0) * 0.6);
    float a = coverage * pc.params.x;
    outColor = vec4(srgbToLinear(color) * a, a); // premultiplied
}
