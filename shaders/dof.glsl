// Depth of field (ENG-F10 part 3, kke::DofPass; the CPU copy and the tests'
// reference is engine/src/DepthOfField.cpp). Four steps, one #define each:
// DOF_FOCUS finds the focus at the middle of the screen and follows it,
// DOF_HALF makes a half-size image with each pixel's signed circle,
// DOF_GATHER blurs it (scatter as gather, one pass, no tiles) and
// DOF_COMPOSITE mixes the blur into the sharp image by each pixel's circle.
// #include after #version, the step's define and DEPTH_MS.
layout(set = 0, binding = 0) buffer Focus { float focusNow; }; // metres; 0 until the first frame
layout(set = 0, binding = 1) uniform sampler2D scene;          // the scene light (after SMAA and TAA)
#ifdef DEPTH_MS
layout(set = 0, binding = 2) uniform sampler2DMS sceneDepth;
float depthAt(ivec2 p) { return texelFetch(sceneDepth, p, 0).r; }
vec2 depthSize() { return vec2(textureSize(sceneDepth)); }
#else
layout(set = 0, binding = 2) uniform sampler2D sceneDepth;
float depthAt(ivec2 p) { return texelFetch(sceneDepth, p, 0).r; }
vec2 depthSize() { return vec2(textureSize(sceneDepth, 0)); }
#endif
layout(set = 0, binding = 3, rgba16f) uniform image2D half_;   // half size: light, signed circle (half-size pixels)
layout(set = 0, binding = 4) uniform sampler2D halfRead;       // the same image, read
layout(set = 0, binding = 5, rgba16f) uniform image2D blurred; // half size: blurred light, how much near blur covers it
layout(set = 0, binding = 6) uniform sampler2D blurredRead;    // the same image, read (bilinear)
layout(set = 0, binding = 7, rgba16f) uniform writeonly image2D result;

layout(push_constant) uniform Push {
    mat4 invProj; // this frame's inverse projection: depth to distance
    vec4 lens;    // x = focal length (mm), y = f-number, z = image height (pixels), w = largest radius (pixels)
    vec4 focus;   // x = fixed focus (m, 0 = auto), y = follow speed (per second), z = seconds since last frame, w = gather taps
} pc;

const float kSensorHeight = 24.0; // mm

// Distance from the camera (m) of the depth at pixel p of an image `size` pixels.
float distanceAt(ivec2 p, vec2 size) {
    vec2 ndc = (vec2(p) + 0.5) / size * 2.0 - 1.0;
    vec4 v = pc.invProj * vec4(ndc, depthAt(p), 1.0);
    return min(length(v.xyz / v.w), 10000.0);
}

// The signed circle radius (full-size pixels) of a point at `d` metres: kke::dofCircle.
float circleAt(float d, float focusM) {
    float f = pc.lens.x, dm = max(d, 1e-3) * 1000.0, s = max(focusM * 1000.0, f * 1.001);
    float diameter = f * f / pc.lens.y * (dm - s) / (dm * (s - f));
    return clamp(0.5 * diameter / kSensorHeight * pc.lens.z, -pc.lens.w, pc.lens.w);
}

#ifdef DOF_FOCUS
// One group: 64 depths in a square a tenth of the screen high at the middle, averaged in log distance
// (the middle counting most), then followed smoothly: kke::dofFollow.
layout(local_size_x = 8, local_size_y = 8) in;
shared float sumLog[64], sumWeight[64];
void main() {
    vec2 size = depthSize();
    vec2 o = (vec2(gl_LocalInvocationID.xy) - 3.5) / 7.0; // -0.5..0.5
    ivec2 p = ivec2(size * 0.5 + o * vec2(0.1 * size.y));
    float w = exp(-4.0 * dot(o, o));
    uint i = gl_LocalInvocationIndex;
    sumLog[i] = log(max(distanceAt(p, size), 0.05)) * w;
    sumWeight[i] = w;
    barrier();
    for (uint step = 32u; step > 0u; step >>= 1u) {
        if (i < step) {
            sumLog[i] += sumLog[i + step];
            sumWeight[i] += sumWeight[i + step];
        }
        barrier();
    }
    if (i == 0u) {
        float target = pc.focus.x > 0.0 ? pc.focus.x : exp(sumLog[0] / sumWeight[0]);
        float now = focusNow;
        float t = 1.0 - exp(-pc.focus.y * pc.focus.z);
        focusNow = now <= 0.0 || pc.focus.x > 0.0 ? target : exp(mix(log(now), log(target), t));
    }
}
#endif

#ifdef DOF_HALF
// Each half-size pixel: the four light texels averaged, and the circle of the nearest of their depths
// (so a near edge is never missed), in half-size pixels.
layout(local_size_x = 8, local_size_y = 8) in;
void main() {
    ivec2 q = ivec2(gl_GlobalInvocationID.xy);
    ivec2 halfSize = imageSize(half_);
    if (any(greaterThanEqual(q, halfSize))) return;
    vec2 size = depthSize();
    ivec2 last = ivec2(size) - 1;
    vec3 light = vec3(0.0);
    float nearest = 1e9;
    for (int k = 0; k < 4; ++k) {
        ivec2 p = min(q * 2 + ivec2(k & 1, k >> 1), last);
        light += max(texelFetch(scene, p, 0).rgb, vec3(0.0));
        nearest = min(nearest, distanceAt(p, size));
    }
    imageStore(half_, q, vec4(light * 0.25, 0.5 * circleAt(nearest, focusNow)));
}
#endif

#ifdef DOF_GATHER
// Every pixel looks out to the largest circle; a sample counts where its own circle reaches this pixel,
// weighed by 1 / its area (a big circle spreads its light thin). A sample farther than this pixel counts
// only as far as this pixel's own circle reaches, so the background never blurs over a sharper thing
// in front of it; a nearer sample spreads freely over what is behind it.
layout(local_size_x = 8, local_size_y = 8) in;
vec2 dofTap(int index, int count) {
    float r = sqrt((float(index) + 0.5) / float(count));
    float a = float(index) * 2.39996323;
    return vec2(cos(a), sin(a)) * r;
}
void main() {
    ivec2 q = ivec2(gl_GlobalInvocationID.xy);
    ivec2 halfSize = imageSize(blurred);
    if (any(greaterThanEqual(q, halfSize))) return;
    vec4 c = texelFetch(halfRead, q, 0);
    float reach = 0.5 * pc.lens.w;
    float wc = 1.0 / max(c.a * c.a, 1.0);
    vec3 sum = c.rgb * wc;
    float total = wc, near = 0.0;
    int taps = int(pc.focus.w);
    for (int i = 0; i < taps; ++i) {
        vec2 o = dofTap(i, taps) * reach;
        float r = length(o);
        vec4 s = texelFetch(halfRead, clamp(q + ivec2(round(o)), ivec2(0), halfSize - 1), 0);
        float spread = abs(s.a);
        float cover = clamp(spread - r + 1.0, 0.0, 1.0);
        if (s.a > c.a) cover *= clamp(abs(c.a) - r + 1.0, 0.0, 1.0);
        float w = cover / max(spread * spread, 1.0);
        sum += s.rgb * w;
        total += w;
        if (s.a < 0.0 && s.a < c.a - 0.5) near += w;
    }
    imageStore(blurred, q, vec4(sum / total, near / total));
}
#endif

#ifdef DOF_COMPOSITE
// The sharp light where this pixel's circle is under a pixel, the blur where it is wider, and the blur
// of nearer things wherever it covers this pixel.
layout(local_size_x = 8, local_size_y = 8) in;
void main() {
    ivec2 p = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(result);
    if (any(greaterThanEqual(p, size))) return;
    vec2 uv = (vec2(p) + 0.5) / vec2(size);
    vec3 sharp = texelFetch(scene, p, 0).rgb;
    vec4 blur = texture(blurredRead, uv);
    float circle = abs(circleAt(distanceAt(p, vec2(size)), focusNow));
    float amount = max(clamp(circle - 0.5, 0.0, 1.0), clamp(blur.a * 2.0, 0.0, 1.0));
    imageStore(result, p, vec4(mix(sharp, blur.rgb, amount), 1.0));
}
#endif
