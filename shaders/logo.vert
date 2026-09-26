#version 450

// The intro's 3D logo (kke::LogoIntro). Vertex colour is the brand colour
// (sRGB); normals are rotated by the piece's model rotation for lighting in
// a fixed, camera-facing frame.

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec2 inUV;

layout(push_constant) uniform LogoPush {
    mat4 mvp;
    vec4 normalRows[3];
    vec4 params; // x time, y sheen position, z brightness
} pc;

layout(location = 0) out vec3 outColor;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec2 outLogoPos;

void main() {
    gl_Position = pc.mvp * vec4(inPosition, 1.0);
    outColor = inColor;
    outNormal = vec3(dot(pc.normalRows[0].xyz, inNormal), dot(pc.normalRows[1].xyz, inNormal), dot(pc.normalRows[2].xyz, inNormal));
    outLogoPos = inPosition.xy;
}
