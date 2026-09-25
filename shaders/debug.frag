#version 450
layout(location = 0) in vec3 inColor;
layout(location = 0) out vec4 outColor;
vec3 srgbToLinear(vec3 c) { return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(0.04045, c)); }
void main() { outColor = vec4(srgbToLinear(inColor), 1.0); }
