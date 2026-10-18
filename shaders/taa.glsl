// Temporal anti-aliasing, an option over the clean image (ENG-F05 part 2,
// kke::TaaPass; the CPU copy and the tests' reference is engine/src/Taa.cpp).
// This frame (drawn with a sub-pixel shift) is mixed into the history of
// earlier frames, found through the depth with last frame's camera; the
// history is clipped to the colours around the pixel now, so a moving
// thing leaves no trail. #include after #version and the DEPTH_MS define.
layout(local_size_x = 8, local_size_y = 8) in;

layout(set = 0, binding = 0) uniform sampler2D current; // this frame's scene light (after SMAA when it is on)
#ifdef DEPTH_MS
layout(set = 0, binding = 1) uniform sampler2DMS sceneDepth;
float depthAt(ivec2 p) { return texelFetch(sceneDepth, p, 0).r; }
#else
layout(set = 0, binding = 1) uniform sampler2D sceneDepth;
float depthAt(ivec2 p) { return texelFetch(sceneDepth, p, 0).r; }
#endif
layout(set = 0, binding = 2) uniform sampler2D history;                // last frame's result (bilinear)
layout(set = 0, binding = 3, rgba16f) uniform writeonly image2D result; // this frame's result, next frame's history

layout(push_constant) uniform Push {
    mat4 reproject; // last frame's viewProj x this frame's inverse viewProj
    vec4 params;    // x = the new frame's share, y = clip spread, z = 1 when there is a history
} pc;

// The history between pixels by a Catmull-Rom curve, in five bilinear taps
// (Jimenez 2016): a plain bilinear read blurs a little more every frame.
vec3 historyAt(vec2 uv) {
    vec2 size = vec2(textureSize(history, 0));
    vec2 p = uv * size, c = floor(p - 0.5) + 0.5, f = p - c;
    vec2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f)), w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    vec2 w2 = f * (0.5 + f * (2.0 - 1.5 * f)), w3 = f * f * (-0.5 + 0.5 * f);
    vec2 w12 = w1 + w2, t12 = (c + w2 / w12) / size, t0 = (c - 1.0) / size, t3 = (c + 2.0) / size;
    vec3 r = texture(history, vec2(t12.x, t0.y)).rgb * w12.x * w0.y + texture(history, vec2(t0.x, t12.y)).rgb * w0.x * w12.y
           + texture(history, t12).rgb * w12.x * w12.y + texture(history, vec2(t3.x, t12.y)).rgb * w3.x * w12.y
           + texture(history, vec2(t12.x, t3.y)).rgb * w12.x * w3.y;
    float total = w12.x * w0.y + w0.x * w12.y + w12.x * w12.y + w3.x * w12.y + w12.x * w3.y;
    return max(r / total, vec3(0.0));
}

vec3 toYCoCg(vec3 c) { return vec3(0.25 * c.r + 0.5 * c.g + 0.25 * c.b, 0.5 * c.r - 0.5 * c.b, -0.25 * c.r + 0.5 * c.g - 0.25 * c.b); }
vec3 fromYCoCg(vec3 c) { return vec3(c.x + c.y - c.z, c.x + c.z, c.x - c.y - c.z); }

void main() {
    ivec2 p = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(result);
    if (any(greaterThanEqual(p, size))) return;
    vec3 c = texelFetch(current, p, 0).rgb;
    // The colours around the pixel now: their mean and spread.
    vec3 m1 = vec3(0.0), m2 = vec3(0.0);
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
            vec3 v = toYCoCg(texelFetch(current, clamp(p + ivec2(dx, dy), ivec2(0), size - 1), 0).rgb);
            m1 += v;
            m2 += v * v;
        }
    vec3 mean = m1 / 9.0, deviation = sqrt(max(m2 / 9.0 - mean * mean, vec3(0.0)));
    vec3 outColor = c;
    if (pc.params.z > 0.5) {
        vec2 ndc = (vec2(p) + 0.5) / vec2(size) * 2.0 - 1.0;
        vec4 prev = pc.reproject * vec4(ndc, depthAt(p), 1.0);
        vec2 uv = prev.xy / prev.w * 0.5 + 0.5;
        if (all(greaterThanEqual(uv, vec2(0.0))) && all(lessThanEqual(uv, vec2(1.0)))) {
            // kke::taaClip: pulled inside the box along the line to the mean.
            vec3 h = toYCoCg(historyAt(uv)) - mean;
            vec3 units = abs(h / max(deviation * pc.params.y, vec3(1e-4)));
            float most = max(units.x, max(units.y, units.z));
            if (most > 1.0) h /= most;
            outColor = mix(fromYCoCg(mean + h), c, pc.params.x);
        }
    }
    imageStore(result, p, vec4(outColor, 1.0));
}
