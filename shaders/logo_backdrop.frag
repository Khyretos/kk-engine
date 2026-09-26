#version 450

// The intro's backdrop: deep violet, a glow behind the logo (orange above,
// violet below, like the brand colours) and faint light rays turning
// slowly behind it. mode.x = 1 draws the fade-to/from-black
// overlay instead (alpha = params.z).

layout(location = 0) in vec2 inUV;

layout(push_constant) uniform BackdropPush {
    vec4 params; // x time, y aspect, z glow (or fade alpha), w ray angle
    vec4 mode;
} pc;

layout(location = 0) out vec4 outColor;

vec3 srgbToLinear(vec3 c) {
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(0.04045, c));
}

void main() {
    if (pc.mode.x > 0.5) {
        outColor = vec4(0.0, 0.0, 0.0, pc.params.z);
        return;
    }
    // Centred, aspect-correct coordinates, +Y up, logo centre slightly high.
    vec2 p = (inUV * 2.0 - 1.0) * vec2(pc.params.y, -1.0) - vec2(0.0, 0.28);
    float r = length(p);

    vec3 inner = srgbToLinear(vec3(0.105, 0.060, 0.180));
    vec3 outer = srgbToLinear(vec3(0.020, 0.012, 0.035));
    vec3 color = mix(inner, outer, smoothstep(0.0, 1.6, r));

    // Glow behind the emblem.
    float glow = pc.params.z;
    vec3 warm = srgbToLinear(vec3(0.95, 0.55, 0.12));
    vec3 cool = srgbToLinear(vec3(0.45, 0.25, 0.85));
    vec3 glowColor = mix(cool, warm, smoothstep(-0.6, 0.8, p.y));
    color += glowColor * glow * 0.22 * exp(-r * r * 2.2);

    // Soft light rays fanning out from behind the emblem, turning slowly
    // (params.w) and brightening with the glow.
    float a = atan(p.y, p.x) + pc.params.w;
    float rays = pow(0.5 + 0.5 * cos(a * 14.0), 6.0) * 0.6 + pow(0.5 + 0.5 * cos(a * 6.0 + 1.3), 8.0) * 0.4;
    color += glowColor * rays * glow * 0.10 * smoothstep(0.25, 0.7, r) * exp(-r * 1.3);

    // Vignette and a touch of noise against banding on dark gradients.
    color *= 1.0 - 0.35 * smoothstep(0.8, 2.0, r);
    float noise = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453);
    color += (noise - 0.5) / 255.0;
    outColor = vec4(color, 1.0);
}
