// What see-through surfaces show behind them (ENG-F02 part 4,
// kke::BehindPass): the scene drawn so far, copied before the translucent
// draws, with the distance from the camera to each pixel's surface in alpha
// (0 = sky). shaders/translucent_refract.frag reads it at the refracted spot.
// #include after #version and the DEPTH_MS define.
layout(local_size_x = 8, local_size_y = 8) in;

layout(set = 0, binding = 0) uniform sampler2D sceneLight;
#ifdef DEPTH_MS
layout(set = 0, binding = 1) uniform sampler2DMS sceneDepth;
float depthAt(ivec2 p) { return texelFetch(sceneDepth, p, 0).r; }
#else
layout(set = 0, binding = 1) uniform sampler2D sceneDepth;
float depthAt(ivec2 p) { return texelFetch(sceneDepth, p, 0).r; }
#endif
layout(set = 0, binding = 2, rgba16f) uniform writeonly image2D behindOut;

layout(push_constant) uniform Push {
    mat4 invProj; // inverse of the frame's projection (Vulkan's flipped Y)
    vec4 size;    // xy = the scene's size in pixels
} pc;

void main() {
    ivec2 p = ivec2(gl_GlobalInvocationID.xy);
    if (p.x >= int(pc.size.x) || p.y >= int(pc.size.y)) return;
    float d = depthAt(p);
    float dist = 0.0;
    if (d < 1.0) {
        vec2 ndc = (vec2(p) + 0.5) / pc.size.xy * 2.0 - 1.0;
        vec4 v = pc.invProj * vec4(ndc, d, 1.0);
        dist = length(v.xyz / v.w);
    }
    imageStore(behindOut, p, vec4(texelFetch(sceneLight, p, 0).rgb, dist));
}
