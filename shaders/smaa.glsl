// Morphological anti-aliasing (ENG-F05, kke::SmaaPass): edges, weights,
// blend, one frame and no history. The CPU copy and the tests' reference
// is engine/src/Smaa.cpp (kke/Smaa.h explains the method).
// #include after #version; each step's .comp defines its STEP_*.
layout(local_size_x = 8, local_size_y = 8) in;

layout(set = 0, binding = 0) uniform sampler2D sceneLight;               // this frame's scene light (HDR)
layout(set = 0, binding = 1, rgba8) uniform image2D edges;               // x = edge with the left neighbour, y = with the top one
layout(set = 0, binding = 2, rgba8) uniform image2D weights;             // how much of the left, top, right, bottom neighbour
layout(set = 0, binding = 3, rgba16f) uniform writeonly image2D smoothed; // the anti-aliased scene light

layout(push_constant) uniform Push {
    vec4 params; // x = threshold, y = most pixels followed each way
} pc;

ivec2 size() { return imageSize(edges); }

#ifdef STEP_EDGES
float luma(vec3 light) {
    vec3 t = max(light, vec3(0.0)) / (vec3(1.0) + max(light, vec3(0.0)));
    return sqrt(dot(t, vec3(0.2126, 0.7152, 0.0722)));
}

void main() {
    ivec2 p = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(p, size()))) return;
    float l = luma(texelFetch(sceneLight, p, 0).rgb);
    vec2 e = vec2(0.0);
    if (p.x > 0) e.x = float(abs(l - luma(texelFetch(sceneLight, p - ivec2(1, 0), 0).rgb)) > pc.params.x);
    if (p.y > 0) e.y = float(abs(l - luma(texelFetch(sceneLight, p - ivec2(0, 1), 0).rgb)) > pc.params.x);
    imageStore(edges, p, vec4(e, 0.0, 0.0));
}
#endif

#ifdef STEP_WEIGHTS
vec2 edgeAt(ivec2 p) {
    if (any(lessThan(p, ivec2(0))) || any(greaterThanEqual(p, size()))) return vec2(0.0);
    return imageLoad(edges, p).xy;
}

float endHeight(bool farSide, bool nearSide) { return farSide == nearSide ? 0.0 : (farSide ? 0.5 : -0.5); }

// kke::smaaLineArea: (the area on the far side, the area on this side).
vec2 lineArea(int before, int after, float leftEnd, float rightEnd) {
    float length = float(before + after + 1);
    vec2 area = vec2(0.0);
    float s = float(before), mid = 0.5 * length;
    float cuts[3] = float[3](s, clamp(mid, s, s + 1.0), s + 1.0);
    for (int k = 0; k < 2; ++k) {
        float a = cuts[k], b = cuts[k + 1];
        if (b <= a) continue;
        float ya, yb;
        if (leftEnd * rightEnd < 0.0) {
            ya = leftEnd + (rightEnd - leftEnd) * a / length;
            yb = leftEnd + (rightEnd - leftEnd) * b / length;
        } else {
            ya = a <= mid ? leftEnd * (1.0 - 2.0 * a / length) : rightEnd * (2.0 * a / length - 1.0);
            yb = b <= mid ? leftEnd * (1.0 - 2.0 * b / length) : rightEnd * (2.0 * b / length - 1.0);
        }
        float w = b - a;
        if (ya >= 0.0 && yb >= 0.0) {
            area.x += 0.5 * (ya + yb) * w;
        } else if (ya <= 0.0 && yb <= 0.0) {
            area.y -= 0.5 * (ya + yb) * w;
        } else {
            float c = ya / (ya - yb);
            float pa = 0.5 * ya * c * w, pb = 0.5 * yb * (1.0 - c) * w;
            area.x += max(pa, 0.0) + max(pb, 0.0);
            area.y -= min(pa, 0.0) + min(pb, 0.0);
        }
    }
    return area;
}

// A horizontal edge on top of row `row` through column x.
vec2 across(int x, int row) {
    int most = int(pc.params.y), before = 0, after = 0;
    while (before < most && edgeAt(ivec2(x - before - 1, row)).y > 0.5) ++before;
    while (after < most && edgeAt(ivec2(x + after + 1, row)).y > 0.5) ++after;
    int x0 = x - before, x1 = x + after + 1;
    float startEnd = before < most ? endHeight(edgeAt(ivec2(x0, row - 1)).x > 0.5, edgeAt(ivec2(x0, row)).x > 0.5) : 0.0;
    float finishEnd = after < most ? endHeight(edgeAt(ivec2(x1, row - 1)).x > 0.5, edgeAt(ivec2(x1, row)).x > 0.5) : 0.0;
    return lineArea(before, after, startEnd, finishEnd);
}

// A vertical edge left of column `col` through row y.
vec2 down(int col, int y) {
    int most = int(pc.params.y), before = 0, after = 0;
    while (before < most && edgeAt(ivec2(col, y - before - 1)).x > 0.5) ++before;
    while (after < most && edgeAt(ivec2(col, y + after + 1)).x > 0.5) ++after;
    int y0 = y - before, y1 = y + after + 1;
    float startEnd = before < most ? endHeight(edgeAt(ivec2(col - 1, y0)).y > 0.5, edgeAt(ivec2(col, y0)).y > 0.5) : 0.0;
    float finishEnd = after < most ? endHeight(edgeAt(ivec2(col - 1, y1)).y > 0.5, edgeAt(ivec2(col, y1)).y > 0.5) : 0.0;
    return lineArea(before, after, startEnd, finishEnd);
}

void main() {
    ivec2 p = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(p, size()))) return;
    vec4 w = vec4(0.0);
    if (edgeAt(p).x > 0.5) w.x = down(p.x, p.y).y;
    if (edgeAt(p).y > 0.5) w.y = across(p.x, p.y).y;
    if (edgeAt(p + ivec2(1, 0)).x > 0.5) w.z = down(p.x + 1, p.y).x;
    if (edgeAt(p + ivec2(0, 1)).y > 0.5) w.w = across(p.x, p.y + 1).x;
    float sum = w.x + w.y + w.z + w.w;
    imageStore(weights, p, sum > 1.0 ? w / sum : w);
}
#endif

#ifdef STEP_BLEND
vec3 lightAt(ivec2 p) { return texelFetch(sceneLight, clamp(p, ivec2(0), size() - 1), 0).rgb; }

void main() {
    ivec2 p = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(p, size()))) return;
    vec4 w = imageLoad(weights, p);
    vec3 c = lightAt(p) * (1.0 - w.x - w.y - w.z - w.w) + lightAt(p - ivec2(1, 0)) * w.x + lightAt(p - ivec2(0, 1)) * w.y +
             lightAt(p + ivec2(1, 0)) * w.z + lightAt(p + ivec2(0, 1)) * w.w;
    imageStore(smoothed, p, vec4(c, 1.0));
}
#endif
