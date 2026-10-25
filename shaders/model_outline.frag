#version 450
#extension GL_GOOGLE_include_directive : require
// kke::ModelModule::setToon's outline colour: the material's own colour,
// much darker and a little more saturated (an ink line in the surface's
// hue, as anime line art is coloured), lit only by the sky.
layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragPosWorld;
layout(location = 0) out vec4 outColor;

#include "pbr_common.glsl"

void main() {
    vec3 base = srgbToLinear(fragColor);
    float grey = dot(base, vec3(0.2126, 0.7152, 0.0722));
    vec3 ink = mix(vec3(grey), base, 1.3) * 0.22;
    vec3 sky = ambientIrradiance(vec3(0.0, 1.0, 0.0));
    outColor = vec4(applyFog(max(ink, vec3(0.0)) * (0.6 + sky), fragPosWorld), reflectionMark(1.0));
}
