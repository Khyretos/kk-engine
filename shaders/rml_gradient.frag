#version 450

// CSS gradients for RmlUi (linear-/radial-/conic-gradient and their
// repeating- variants). The math mirrors RmlUi's own GL3 backend
// (Backends/RmlUi_Renderer_GL3.cpp, shader_frag_gradient). Instead of
// uploading the color-stop arrays per draw, CompileShader() bakes the
// stops into a 256x1 ramp texture spanning the first..last stop
// position, so this shader only computes t and does one lookup.

layout(location = 0) in vec4 inColor;
layout(location = 1) in vec2 inTexCoord; // RmlUi puts the gradient-space position here
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D uRamp;

layout(push_constant) uniform PushConstants {
    layout(offset = 80) vec2 p;   // linear: start point, radial: center, conic: center
    vec2 v;                       // linear: vector to end, radial: inverse radius, conic: angle unit vector
    float t0;                     // first stop position
    float t1;                     // last stop position
    int func;                     // 0 linear, 1 radial, 2 conic, +3 = repeating
} pc;

const float PI = 3.14159265;

vec3 srgbToLinear(vec3 c) {
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(0.04045, c));
}

void main() {
    int base = pc.func % 3;
    float t = 0.0;
    vec2 V = inTexCoord - pc.p;
    if (base == 0) {
        t = dot(pc.v, V) / max(dot(pc.v, pc.v), 1e-8);
    } else if (base == 1) {
        t = length(pc.v * V);
    } else {
        mat2 R = mat2(pc.v.x, -pc.v.y, pc.v.y, pc.v.x);
        vec2 W = R * V;
        t = 0.5 + atan(-W.x, W.y) / (2.0 * PI);
    }
    float span = max(pc.t1 - pc.t0, 1e-6);
    if (pc.func >= 3) {
        t = pc.t0 + mod(t - pc.t0, span);
    }
    float u = clamp((t - pc.t0) / span, 0.0, 1.0);
    // Sample texel centers only: u in [0,1] -> [0.5/256, 255.5/256].
    vec4 c = texture(uRamp, vec2((u * 255.0 + 0.5) / 256.0, 0.5)) * inColor;
    // Premultiplied in/out, linearized for the sRGB swapchain — same as rml_ui.frag.
    vec3 straight = c.a > 0.0 ? c.rgb / c.a : vec3(0.0);
    outColor = vec4(srgbToLinear(straight) * c.a, c.a);
}
