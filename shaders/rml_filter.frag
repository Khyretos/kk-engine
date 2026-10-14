#version 450

// The RmlUi layer filters of kke::RmlVulkanRenderInterface (ENG-G09), one
// full-screen pass each, chosen by `mode`. Layers hold linear, premultiplied
// colour (sRGB-encoded on a UNORM target, kTargetEncoded). The CPU copies of this maths are in engine/src/RmlFilters.cpp
// (tests/test_rml_filters.cpp): change both together.

layout(location = 0) in vec2 inUv;
layout(location = 0) out vec4 outColor;

// The layer being filtered or composited.
layout(set = 0, binding = 0) uniform sampler2D uSource;
// The saved mask image (mode 4); any texture otherwise.
layout(set = 1, binding = 0) uniform sampler2D uMask;

// kke::RmlFilterPush.
layout(push_constant) uniform Filter {
    mat4 matrix;
    vec4 color;
    vec2 offset;
    vec2 uvMin;
    vec2 uvMax;
    float sigma;
    int radius;
    int mode;
    float opacity;
} pc;

// kke::RmlVulkanRenderInterface sets it when the swapchain is UNORM (10-bit,
// ENG-G17): colour stays sRGB-encoded and blends that way, as in a browser.
layout(constant_id = 0) const bool kTargetEncoded = false;

vec3 srgbToLinear(vec3 c) {
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(0.04045, c));
}

vec3 linearToSrgb(vec3 c) {
    return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(0.0031308, c));
}

// The source inside the scissor rectangle (uvMin..uvMax, texel centres); transparent outside it.
vec4 sampleRegion(vec2 uv) {
    vec2 inside = step(pc.uvMin, uv) * step(uv, pc.uvMax);
    return texture(uSource, uv) * inside.x * inside.y;
}

void main() {
    if (pc.mode == 0) {
        // Opacity (and plain copies at 1).
        outColor = texture(uSource, inUv) * pc.opacity;
    } else if (pc.mode == 1) {
        // Colour matrix, in premultiplied sRGB space like RmlUi's GL3 backend.
        vec4 c = texture(uSource, inUv);
        if (c.a <= 0.0) {
            outColor = vec4(0.0);
            return;
        }
        vec3 s = kTargetEncoded ? c.rgb / c.a : linearToSrgb(c.rgb / c.a);
        vec4 t = pc.matrix * vec4(s * c.a, c.a);
        vec3 o = clamp(t.rgb / c.a, 0.0, 1.0);
        outColor = vec4((kTargetEncoded ? o : srgbToLinear(o)) * c.a, c.a);
    } else if (pc.mode == 2) {
        // One direction of a separable Gaussian: offset is one texel along it.
        // Taps i and i + 1 are one linear-filtered sample placed between them
        // by their weights, so each side takes half the samples.
        vec4 sum = sampleRegion(inUv);
        float total = 1.0;
        for (int i = 1; i <= pc.radius; i += 2) {
            float w1 = exp(-float(i * i) / (2.0 * pc.sigma * pc.sigma));
            float w2 = i + 1 <= pc.radius ? exp(-float((i + 1) * (i + 1)) / (2.0 * pc.sigma * pc.sigma)) : 0.0;
            float w = w1 + w2;
            float at = (float(i) * w1 + float(i + 1) * w2) / w;
            sum += (sampleRegion(inUv + pc.offset * at) + sampleRegion(inUv - pc.offset * at)) * w;
            total += 2.0 * w;
        }
        outColor = sum / total;
    } else if (pc.mode == 3) {
        // Drop shadow: the source's alpha, moved by offset (in uv), in the shadow colour.
        outColor = pc.color * sampleRegion(inUv - pc.offset).a;
    } else {
        // Mask image: keep the source where the mask is opaque.
        outColor = texture(uSource, inUv) * texture(uMask, inUv).a;
    }
}
