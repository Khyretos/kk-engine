#version 450

// Genuinely empty — this render pass has no color attachment at all
// (see kke::ShadowMap), only depth, and depth gets written
// automatically by the fixed-function depth test. Pipeline still
// requires *some* fragment shader to bind (see kke::Pipeline's
// constructor, which always takes both a vert and a frag path), so
// this exists to satisfy that rather than to do anything itself.
void main() {
}
