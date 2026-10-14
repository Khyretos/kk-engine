#version 450

// RmlUi geometry drawn with a texture saved from a layer (box-shadows,
// ENG-G09). Such a texture already holds linear, premultiplied colour, so
// unlike rml_ui.frag only the vertex colour is converted from sRGB.

layout(location = 0) in vec4 inColor;
layout(location = 1) in vec2 inTexCoord;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D uTexture;

vec3 srgbToLinear(vec3 c) {
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(0.04045, c));
}

void main() {
    vec3 straight = inColor.a > 0.0 ? inColor.rgb / inColor.a : vec3(0.0);
    outColor = texture(uTexture, inTexCoord) * vec4(srgbToLinear(straight) * inColor.a, inColor.a);
}
