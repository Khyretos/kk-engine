// Shared by translucent_absorb.frag and translucent_light.frag (see
// kke::DynamicMeshRenderer::drawTranslucent). #include after
// pbr_common.glsl. Per vertex: color = tint (sRGB), uv.x = density,
// uv.y = milkiness.
//
// A cheap stand-in for a volume: the light path through the body is
// taken as `thickness / cos(view angle)` (thin face-on, thick at the
// silhouette, which is why real jelly looks darker and richer at its
// edges), and Beer-Lambert absorption turns the tint into how much of
// each colour gets through: transmittance = tint ^ (density * path).

const float kThickness = 1.0;

float translucentNdotV(vec3 N, vec3 V) { return clamp(dot(N, V), 0.12, 1.0); }

float translucentFresnel(float NdotV) {
    // Schlick, F0 = 0.04 (water, gelatin: n ~ 1.34)
    float m = 1.0 - NdotV;
    return 0.04 + 0.96 * m * m * m * m * m;
}

// Fraction of what's behind that reaches the eye, per colour.
vec3 translucentTransmittance(vec3 tintLinear, float density, float milkiness, float NdotV) {
    float path = kThickness / NdotV;
    vec3 t = pow(clamp(tintLinear, vec3(0.002), vec3(1.0)), vec3(max(density, 0.0) * path));
    return t * (1.0 - translucentFresnel(NdotV)) * (1.0 - 0.85 * clamp(milkiness, 0.0, 1.0));
}
