#version 450
// shadow.vert for instanced draws (ModelModule): model matrix per
// instance at binding 1 (locations 8-11), same as model_instanced.vert.
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec2 inUV;
layout(location = 8) in vec4 inModel0;
layout(location = 9) in vec4 inModel1;
layout(location = 10) in vec4 inModel2;
layout(location = 11) in vec4 inModel3;
layout(location = 12) in vec4 inTint;

layout(push_constant) uniform ShadowPushConstants {
    mat4 lightViewProj;
    mat4 model; // unused here
} pc;

void main() {
    gl_Position = pc.lightViewProj * mat4(inModel0, inModel1, inModel2, inModel3) * vec4(inPosition, 1.0);
}
