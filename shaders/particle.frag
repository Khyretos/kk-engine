#version 450

layout(location = 0) in float inLifeFraction;
layout(location = 0) out vec4 outColor;


// Colors are authored in sRGB (color pickers, hex codes, palette values)
// but the swapchain is VK_FORMAT_B8G8R8A8_SRGB, which gamma-encodes
// whatever the shader writes. Writing sRGB values straight out encoded
// them twice: everything looked washed out. Convert to linear first.
vec3 srgbToLinear(vec3 c) {
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(0.04045, c));
}

void main() {
    vec2 uv = gl_PointCoord * 2.0 - 1.0;
    float dist = length(uv);
    float alpha = smoothstep(1.0, 0.0, dist);

    vec3 hot = vec3(1.0, 0.95, 0.8);
    vec3 mid = vec3(1.0, 0.5, 0.1);
    vec3 cool = vec3(0.6, 0.05, 0.02);

    vec3 color = inLifeFraction > 0.5
        ? mix(mid, hot, (inLifeFraction - 0.5) * 2.0)
        : mix(cool, mid, inLifeFraction * 2.0);

    outColor = vec4(srgbToLinear(color), alpha * inLifeFraction);
}
