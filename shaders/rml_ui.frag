#version 450

layout(location = 0) in vec4 inColor;
layout(location = 1) in vec2 inTexCoord;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D uTexture;

// kke::RmlVulkanRenderInterface sets it when the swapchain is UNORM (10-bit,
// ENG-G17): colour stays sRGB-encoded and blends that way, as in a browser.
layout(constant_id = 0) const bool kTargetEncoded = false;


// Colors are authored in sRGB (color pickers, hex codes, palette values)
// but an sRGB swapchain (B8G8R8A8_SRGB) gamma-encodes
// whatever the shader writes. Writing sRGB values straight out encoded
// them twice: everything looked washed out. Convert to linear first.
vec3 srgbToLinear(vec3 c) {
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(0.04045, c));
}

void main() {
    // Untextured draws bind a persistent 1x1 opaque-white texture (see
    // RmlVulkanRenderInterface's default texture), so sampling here is
    // always valid and multiplying by white is a no-op for plain colors.
    // Premultiplied in, premultiplied out: un-premultiply, convert the
    // color to linear, re-premultiply. Blending then happens in linear
    // space (slightly different translucency than a browser, which
    // blends in sRGB, but no double gamma).
    vec4 c = texture(uTexture, inTexCoord) * inColor;
    vec3 straight = c.a > 0.0 ? c.rgb / c.a : vec3(0.0);
    outColor = kTargetEncoded ? c : vec4(srgbToLinear(straight) * c.a, c.a);
}
