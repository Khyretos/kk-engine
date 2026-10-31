#pragma once

// ENG-F14: CPU mirrors of shaders/upscale_fsr1.glsl's maths (tests).

namespace kke {

// EASU's tap weight for a squared, stretched distance `d2` (texels^2)
// with lobe `lob` (0.21 along strong edges .. 0.5 in flat areas): an
// approximated Lanczos-2, 1 at the centre, 0 at distance 2 when lob is 0.25.
float fsrEasuWeight(float d2, float lob);

// The reversible tone map the upscaler filters in, and its inverse:
// c / (1 + max channel), so bright HDR pixels don't ring.
void fsrToFilterSpace(const float in[3], float out[3]);
void fsrFromFilterSpace(const float in[3], float out[3]);

} // namespace kke
