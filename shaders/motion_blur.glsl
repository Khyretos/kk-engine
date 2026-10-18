// Motion blur from the camera's movement (ENG-F10 part 4, kke::MotionBlurPass;
// the CPU copy and the tests' reference is engine/src/MotionBlur.cpp). Each
// pixel's motion comes from its depth and last frame's camera; the light is
// gathered along it, each sample weighed by depth (McGuire et al. 2012).
// #include after #version and the DEPTH_MS define.
layout(local_size_x = 8, local_size_y = 8) in;

layout(set = 0, binding = 0) uniform sampler2D scene; // the light (after SMAA, TAA and depth of field)
#ifdef DEPTH_MS
layout(set = 0, binding = 1) uniform sampler2DMS sceneDepth;
float depthAt(ivec2 p) { return texelFetch(sceneDepth, p, 0).r; }
#else
layout(set = 0, binding = 1) uniform sampler2D sceneDepth;
float depthAt(ivec2 p) { return texelFetch(sceneDepth, p, 0).r; }
#endif
layout(set = 0, binding = 2, rgba16f) uniform writeonly image2D result;

layout(push_constant) uniform Push {
    mat4 reproject; // last frame's viewProj x this frame's inverse viewProj
    vec4 lens;      // x, y = this projection's [2][2] and [3][2] (depth to distance), z = shutter, w = longest smear (pixels)
    vec4 params;    // x = samples, y = frame number (for the noise), z = 1 when there is a last frame
} pc;

float viewDistance(float depth) { return pc.lens.y / (depth + pc.lens.x); }

// kke::motionInFront
float inFront(float nearer, float farther) { return clamp((farther - nearer) / (0.02 * max(farther, 1e-3)) + 0.5, 0.0, 1.0); }

// This pixel's smear in pixels: kke::motionPrevious, then kke::motionSmear.
vec2 smearAt(ivec2 p, vec2 size, float depth) {
    vec2 uv = (vec2(p) + 0.5) / size;
    vec4 prev = pc.reproject * vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec2 v = (uv - (prev.xy / prev.w * 0.5 + 0.5)) * size * pc.lens.z;
    float len = length(v);
    return len > pc.lens.w ? v * (pc.lens.w / len) : v;
}

void main() {
    ivec2 p = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(result);
    if (any(greaterThanEqual(p, size))) return;
    vec3 centre = texelFetch(scene, p, 0).rgb;
    float d = depthAt(p);
    vec2 smear = pc.params.z > 0.5 ? smearAt(p, vec2(size), d) : vec2(0.0);
    float reach = length(smear);
    if (reach < 0.5) {
        imageStore(result, p, vec4(centre, 1.0));
        return;
    }
    float zc = viewDistance(d), rc = 0.5 * reach; // this smear's half length: samples reach that far either way
    // Interleaved gradient noise (Jimenez 2014) shifts the samples per pixel and frame: noise, not bands.
    float noise = fract(52.9829189 * fract(dot(vec2(p) + 5.588238 * pc.params.y, vec2(0.06711056, 0.00583715))));
    int n = int(pc.params.x);
    vec3 sum = centre / max(reach, 1.0);
    float total = 1.0 / max(reach, 1.0);
    for (int i = 0; i < n; ++i) {
        float t = (float(i) + noise) / float(n) - 0.5; // -0.5..0.5 along the smear
        if (abs(t) < 1e-3) continue;
        ivec2 q = clamp(p + ivec2(round(smear * t)), ivec2(0), size - 1);
        float r = length(smear * t);
        float ds = depthAt(q);
        float zs = viewDistance(ds);
        float rs = 0.5 * length(pc.params.z > 0.5 ? smearAt(q, vec2(size), ds) : vec2(0.0)); // its smear's half length
        // A sample in front counts where its own smear reaches here, one behind only within this pixel's smear,
        // and where both smears reach, the two blur into each other (McGuire's cylinders).
        float front = inFront(zs, zc), back = inFront(zc, zs);
        float coneS = clamp(1.0 - r / max(rs, 1e-3), 0.0, 1.0), coneC = clamp(1.0 - r / rc, 0.0, 1.0);
        float both = (1.0 - smoothstep(0.95 * rs, 1.05 * rs, r)) * (1.0 - smoothstep(0.95 * rc, 1.05 * rc, r));
        float w = front * coneS + back * coneC + both * 2.0;
        sum += texelFetch(scene, q, 0).rgb * w;
        total += w;
    }
    imageStore(result, p, vec4(sum / total, 1.0));
}
