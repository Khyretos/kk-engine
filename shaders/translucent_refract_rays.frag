#version 460
#extension GL_GOOGLE_include_directive : require
// Refraction with rays for what the screen can't show: KKE (stand-ins) or
// classic (every pixel, full meshes) (shaders/translucent_refract.glsl).
#define RAY_QUERY
#define RAY_SET 3
#define RAY_TLAS 0
#define RAY_MATERIALS 1
#define RAY_PARAMS 2
#include "ray_trace.glsl"
#include "translucent_refract.glsl"
