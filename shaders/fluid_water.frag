#version 450
#extension GL_GOOGLE_include_directive : require
// Screen-space fluid, clear water (kke::FluidSurfaceRenderer::drawWater, in
// renderTranslucent): the surface rebuilt from the blurred depth as
// fluid_composite.frag does, then what is behind it (kke::BehindPass)
// bent by its normal and dimmed per colour by the water it passes
// (Beer-Lambert), the water's own deep colour where it is thick, the sky
// reflected (Fresnel), the sun's glint, and foam. Without the behind image
// (refraction off, split screen) it blends over the scene instead.
layout(location = 0) in vec2 ndc;
layout(location = 0) out vec4 outColor;

#include "lighting_ubo.glsl"
layout(set = 1, binding = 0) uniform sampler2D fluidDepth;
layout(set = 1, binding = 1) uniform sampler2D fluidColor;
layout(set = 1, binding = 2) uniform sampler2D fluidThickness;
// invProj; the view's rotation to world by columns, w = absorption per
// metre (r, g, b); deep colour + refraction strength.
layout(push_constant) uniform PC { mat4 invProj; vec4 toWorld[3]; vec4 deepRefract; } pc;

vec3 viewPos(vec2 uv) {
    float z = texture(fluidDepth, uv).r;
    vec4 v = pc.invProj * vec4(uv * 2.0 - 1.0, 1.0, 1.0);
    vec3 dir = v.xyz / v.w;
    return dir / -dir.z * z;
}

void main() {
    vec2 uv = ndc * 0.5 + 0.5;
    float z = texture(fluidDepth, uv).r;
    if (z <= 0.0) discard;
    vec2 texel = 1.0 / vec2(textureSize(fluidDepth, 0));
    vec3 P = viewPos(uv);
    vec3 ddx = viewPos(uv + vec2(texel.x, 0)) - P, ddx2 = P - viewPos(uv - vec2(texel.x, 0));
    vec3 ddy = viewPos(uv + vec2(0, texel.y)) - P, ddy2 = P - viewPos(uv - vec2(0, texel.y));
    if (abs(ddx2.z) < abs(ddx.z)) ddx = ddx2;
    if (abs(ddy2.z) < abs(ddy.z)) ddy = ddy2;
    vec3 Nview = normalize(cross(ddx, ddy));
    if (Nview.z < 0.0) Nview = -Nview;
    mat3 toWorld = mat3(pc.toWorld[0].xyz, pc.toWorld[1].xyz, pc.toWorld[2].xyz);
    vec3 N = normalize(toWorld * Nview);
    vec3 Pw = toWorld * P + lighting.cameraPos.xyz;
    vec4 clip = lighting.viewProj * vec4(Pw, 1.0);
    gl_FragDepth = clip.z / clip.w;

    vec2 tf = texture(fluidThickness, uv).rg;
    float thick = max(tf.r, 0.0);
    float foam = clamp(tf.g * 8.0, 0.0, 1.0); // 12 cm of churned water reads as white
    vec3 absorb = vec3(pc.toWorld[0].w, pc.toWorld[1].w, pc.toWorld[2].w);
    vec3 transmit = exp(-absorb * thick);
    vec3 V = normalize(lighting.cameraPos.xyz - Pw);
    float NdotV = clamp(dot(N, V), 0.0, 1.0);
    float fresnel = 0.02 + 0.98 * pow(1.0 - NdotV, 5.0);
    vec3 L = normalize(-lighting.lights[0].directionOrPosition.xyz);
    vec3 sun = lighting.lights[0].colorIntensity.rgb * lighting.lights[0].colorIntensity.a;
    vec3 H = normalize(L + V);
    float spec = pow(max(dot(N, H), 0.0), 400.0) * 3.0 + pow(max(dot(N, H), 0.0), 60.0) * 0.15;
    vec3 sky = skyReflection(reflect(-V, N), 0.04);
    // Light the water itself sends back: the deep colour, lit by the sky
    // and the sun, as much as the water isn't see-through.
    vec3 lightIn = lighting.ambient.rgb + sun * max(dot(N, L), 0.0) * 0.5 + ambientIrradiance(N);
    vec3 own = pc.deepRefract.rgb * lightIn * (1.0 - dot(transmit, vec3(1.0 / 3.0)));
    vec3 foamColor = lightIn * 0.8;

    float alpha;
    vec3 color;
    if (lighting.behind.x > 0.5) {
        // What's behind, bent: never something in front of the water.
        float surface = length(Pw - lighting.cameraPos.xyz);
        vec2 bent = uv - vec2(Nview.x, -Nview.y) * pc.deepRefract.w * min(thick, 1.0);
        vec2 at = uv;
        if (all(greaterThanEqual(bent, vec2(0.0))) && all(lessThanEqual(bent, vec2(1.0)))) {
            float there = texture(behindImage, bent).a;
            if (!(there > 0.0 && there < surface)) at = bent;
        }
        vec3 behind = texture(behindImage, at).rgb;
        color = behind * transmit + own;
        alpha = 1.0;
    } else {
        color = own;
        alpha = 1.0 - dot(transmit, vec3(1.0 / 3.0));
    }
    // Reflection and glint over the top (premultiplied: they add light).
    color = color * (1.0 - fresnel) + sky * fresnel + sun * spec;
    alpha = alpha * (1.0 - fresnel) + fresnel;
    color = mix(color, foamColor, foam);
    alpha = mix(alpha, 1.0, foam);
    color = applyFog(color, Pw);
    outColor = vec4(color, alpha);
}
