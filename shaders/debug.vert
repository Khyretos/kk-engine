#version 450
// Unlit debug geometry (kke::DebugDrawModule). Declares all four
// kke::Vertex attributes even though only position/color are used: the
// pipeline's vertex input always describes all four, and a mismatch
// silently broke drawing on lavapipe once (BUGS.md BUG-010).
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec2 inUV;
layout(push_constant) uniform PC { mat4 viewProj; } pc;
layout(location = 0) out vec3 outColor;
void main() {
    gl_Position = pc.viewProj * vec4(inPosition, 1.0);
    outColor = inColor;
}
