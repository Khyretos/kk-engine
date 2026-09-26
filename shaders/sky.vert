#version 450
// Full-screen triangle; the fragment shader turns each pixel into a view ray.
layout(location = 0) out vec2 ndc;
void main() {
    vec2 p = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2) * 2.0 - 1.0;
    ndc = p;
    gl_Position = vec4(p, 1.0, 1.0); // far plane
}
