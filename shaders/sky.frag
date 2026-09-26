#version 450
layout(location = 0) in vec2 ndc;
layout(location = 0) out vec4 outColor;
layout(push_constant) uniform Sky { mat4 invViewProj; vec4 sunDir; } pc;
void main() {
    vec4 a = pc.invViewProj * vec4(ndc, 0.0, 1.0), b = pc.invViewProj * vec4(ndc, 1.0, 1.0);
    vec3 dir = normalize(b.xyz / b.w - a.xyz / a.w);
    float h = clamp(dir.y, 0.0, 1.0);
    vec3 c = mix(vec3(0.62, 0.74, 0.86), vec3(0.18, 0.38, 0.72), pow(h, 0.5));
    float sun = max(dot(dir, normalize(pc.sunDir.xyz)), 0.0);
    c += vec3(1.0, 0.9, 0.7) * (pow(sun, 900.0) * 8.0 + pow(sun, 12.0) * 0.25);
    if (dir.y < 0.0) c = mix(c, vec3(0.05, 0.12, 0.2), clamp(-dir.y * 4.0, 0.0, 1.0));
    c = c / (c + vec3(1.0));
    outColor = vec4(c, 1.0);
}
