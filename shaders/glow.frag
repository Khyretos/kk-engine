#version 450
#extension GL_GOOGLE_include_directive : require
// cube.frag's lighting plus incandescence: uv.x = glow 0..1 (e.g. how
// close a melting solid is to its melting point). Used with cube.vert by
// kke::DynamicMeshRenderer.
layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragNormalWorld;
layout(location = 2) in vec3 fragPosWorld;
layout(location = 3) in vec4 fragPosLightSpace;
layout(location = 4) in vec2 fragMetallicRoughness;
layout(location = 5) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

#include "pbr_common.glsl"
#include "glow.glsl"

void main() {
    vec3 lit = shadeSurface(srgbToLinear(fragColor), fragMetallicRoughness, fragNormalWorld, fragPosWorld, fragPosLightSpace);
    outColor = vec4(lit + glowColor(fragUV.x), 1.0);
}
