#version 450
#extension GL_GOOGLE_include_directive : require
// kke::AnimeRenderer's outline colour: MToon's own when the material fixes
// it, else a deep tone of the surface's shade colour (line art coloured
// in the surface's hue, as gacha games draw it), lit by the sky only a little.
layout(location = 0) in vec3 fragPosWorld;
layout(location = 1) in vec2 fragUV;
layout(location = 2) in vec3 fragNormalWorld;
layout(location = 0) out vec4 outColor;

#include "pbr_common.glsl"
#include "anime_common.glsl"

void main() {
    vec4 texel = texture(mainTexture, fragUV);
    if (toon.params.w >= 0.0 && texel.a * toon.color.a < toon.params.w) discard;
    vec3 surface = srgbToLinear(toon.shade.rgb) * texture(shadeTexture, fragUV).rgb;
    float grey = dot(surface, vec3(0.2126, 0.7152, 0.0722));
    vec3 ink = max(mix(vec3(grey), surface, 1.4), vec3(0.0)) * 0.3;
    vec3 colour = toon.outline2.x > 0.5 ? srgbToLinear(toon.outline.rgb) : ink;
    float intensity = lighting.lights[0].colorIntensity.a;
    vec3 sun = intensity > 0.0 ? lighting.lights[0].colorIntensity.rgb * intensity : vec3(0.0);
    vec3 light = sun / PI + ambientIrradiance(vec3(0.0, 1.0, 0.0));
    colour *= mix(vec3(1.0), light, toon.outline2.y * 0.5);
    outColor = vec4(applyFog(colour, fragPosWorld), reflectionMark(1.0));
}
