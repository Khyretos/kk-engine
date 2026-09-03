#version 450

layout(location = 0) in vec3 inWorldPos;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform GridPushConstants {
    mat4 viewProj;
    vec4 cameraPos;
} pc;

// Classic screen-space-derivative grid (Ben Golus' technique): draws
// anti-aliased lines at minor (1 unit) and major (5 unit) spacing without
// any texture lookups, and fades smoothly with distance so it reads as
// an "infinite" reference plane instead of a hard-edged quad.
float gridLine(vec2 coord, float spacing) {
    vec2 scaled = coord / spacing;
    vec2 derivative = fwidth(scaled);
    vec2 grid = abs(fract(scaled - 0.5) - 0.5) / derivative;
    return 1.0 - min(min(grid.x, grid.y), 1.0);
}

void main() {
    vec2 coord = inWorldPos.xz;

    float minor = gridLine(coord, 1.0);
    float major = gridLine(coord, 5.0);

    vec3 minorColor = vec3(0.35, 0.37, 0.42);
    vec3 majorColor = vec3(0.55, 0.58, 0.66);

    float line = max(minor * 0.6, major);
    vec3 color = mix(minorColor, majorColor, step(0.5, major));

    float dist = length(inWorldPos - pc.cameraPos.xyz);
    float fade = 1.0 - smoothstep(10.0, 25.0, dist);

    outColor = vec4(color, line * fade * 0.9);
}
