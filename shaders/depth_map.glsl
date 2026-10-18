// Occlusion culling (ENG-F13, kke::DepthMapPass; the CPU copy and the
// tests' reference is kke::buildDepthMap in engine/src/Occlusion.cpp): one
// invocation per cell of the map, the farthest depth of its 8x8 block of
// the frame's depth (every sample with MSAA), written straight into memory
// the CPU reads two frames later. #include after #version and DEPTH_MS.
layout(local_size_x = 8, local_size_y = 8) in;

#ifdef DEPTH_MS
layout(set = 0, binding = 0) uniform sampler2DMS sceneDepth;
float farthestAt(ivec2 p) {
    float d = 0.0;
    for (int s = 0; s < textureSamples(sceneDepth); ++s) d = max(d, texelFetch(sceneDepth, p, s).r);
    return d;
}
ivec2 depthSize() { return textureSize(sceneDepth); }
#else
layout(set = 0, binding = 0) uniform sampler2D sceneDepth;
float farthestAt(ivec2 p) { return texelFetch(sceneDepth, p, 0).r; }
ivec2 depthSize() { return textureSize(sceneDepth, 0); }
#endif
layout(std430, set = 0, binding = 1) writeonly buffer Map { float farthest[]; };

layout(push_constant) uniform Push {
    uvec4 size; // xy = the map's size in cells
} pc;

const int kCell = 8;

void main() {
    uvec2 c = gl_GlobalInvocationID.xy;
    if (any(greaterThanEqual(c, pc.size.xy))) return;
    ivec2 last = depthSize() - 1;
    float d = 0.0;
    for (int y = 0; y < kCell; ++y)
        for (int x = 0; x < kCell; ++x) {
            ivec2 p = ivec2(c) * kCell + ivec2(x, y);
            if (p.x <= last.x && p.y <= last.y) d = max(d, farthestAt(p));
        }
    farthest[c.y * pc.size.x + c.x] = d;
}
