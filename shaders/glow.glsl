// Incandescence for hot matter (lava, molten metal): black -> deep red ->
// orange -> yellow-white as glow goes 0 -> 1, roughly a blackbody ramp.
// Added after lighting so hot things glow in the dark.
vec3 glowColor(float glow) {
    float g = clamp(glow, 0.0, 1.0);
    vec3 c = mix(vec3(0.0), vec3(0.6, 0.05, 0.0), smoothstep(0.0, 0.35, g));
    c = mix(c, vec3(1.0, 0.35, 0.02), smoothstep(0.35, 0.75, g));
    c = mix(c, vec3(1.0, 0.85, 0.45), smoothstep(0.75, 1.0, g));
    return c * (0.5 + g);
}
