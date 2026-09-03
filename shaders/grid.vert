#version 450

// No vertex buffer: a 10x10-unit quad on the Y=0 plane, synthesized here
// from gl_VertexIndex. Two triangles, six vertices, wound so back-face
// culling (frontFace = CLOCKWISE, same as the rest of the engine) keeps
// the top face visible.
vec3 positions[6] = vec3[](
    vec3(-25.0, 0.0, -25.0), vec3(-25.0, 0.0,  25.0), vec3( 25.0, 0.0,  25.0),
    vec3( 25.0, 0.0,  25.0), vec3( 25.0, 0.0, -25.0), vec3(-25.0, 0.0, -25.0)
);

layout(push_constant) uniform GridPushConstants {
    mat4 viewProj;
    vec4 cameraPos; // xyz used, w padding
} pc;

layout(location = 0) out vec3 outWorldPos;

void main() {
    vec3 pos = positions[gl_VertexIndex];
    outWorldPos = pos;
    gl_Position = pc.viewProj * vec4(pos, 1.0);
}
