// Bloom (ENG-F10, kke::BloomPass; kke/Bloom.h): halvings with a 13-tap
// filter (the first weighting its boxes by 1 / (1 + brightness)), then the
// build back up with a 3x3 tent. The CPU copy and the tests' reference is
// engine/src/Bloom.cpp. #include after #version; each step defines STEP_*.
layout(local_size_x = 8, local_size_y = 8) in;

#ifdef STEP_DOWN
layout(set = 0, binding = 0) uniform sampler2D source; // bilinear, clamped
layout(set = 0, binding = 1, rgba16f) uniform writeonly image2D target;
layout(push_constant) uniform Push {
    vec4 params; // x = 1: weight the boxes (the first halving)
} pc;

float luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

void main() {
    ivec2 p = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(target);
    if (any(greaterThanEqual(p, size))) return;
    vec2 texel = 1.0 / vec2(textureSize(source, 0));
    vec2 c = (2.0 * vec2(p) + 1.0) * texel; // the corner shared by the 2x2 source block this pixel covers
#define S(dx, dy) texture(source, c + vec2(dx, dy) * texel).rgb
    vec3 a = S(-2, -2), b = S(0, -2), cc = S(2, -2), d = S(-1, -1), e = S(1, -1), f = S(-2, 0), g = S(0, 0), h = S(2, 0);
    vec3 i = S(-1, 1), j = S(1, 1), k = S(-2, 2), l = S(0, 2), m = S(2, 2);
    vec3 boxes[5] = vec3[5]((d + e + i + j) * 0.25, (a + b + f + g) * 0.25, (b + cc + g + h) * 0.25, (f + g + k + l) * 0.25, (g + h + l + m) * 0.25);
    float weights[5] = float[5](0.5, 0.125, 0.125, 0.125, 0.125);
    vec3 sum = vec3(0.0);
    float total = 0.0;
    for (int q = 0; q < 5; ++q) {
        float w = weights[q] * (pc.params.x > 0.5 ? 1.0 / (1.0 + luma(boxes[q])) : 1.0);
        sum += boxes[q] * w;
        total += w;
    }
    imageStore(target, p, vec4(sum / total, 1.0));
}
#endif

#ifdef STEP_UP
layout(set = 0, binding = 0) uniform sampler2D coarse; // the level below, built up already (bilinear)
layout(set = 0, binding = 1) uniform sampler2D fine;   // this level's halving
layout(set = 0, binding = 2, rgba16f) uniform writeonly image2D target;
layout(push_constant) uniform Push {
    vec4 params; // x = 1 / levels on the last step (normalises the sum), else 1
} pc;

void main() {
    ivec2 p = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(target);
    if (any(greaterThanEqual(p, size))) return;
    vec2 uv = (vec2(p) + 0.5) / vec2(size);
    vec2 texel = 1.0 / vec2(textureSize(coarse, 0));
    vec3 t = vec3(0.0);
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) t += texture(coarse, uv + vec2(dx, dy) * texel).rgb * float((2 - abs(dx)) * (2 - abs(dy)));
    imageStore(target, p, vec4((texelFetch(fine, p, 0).rgb + t / 16.0) * pc.params.x, 1.0));
}
#endif
