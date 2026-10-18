#pragma once

#include <glm/glm.hpp>

namespace kke {

// Motion blur (ENG-F10 part 4): the camera's movement smears the image
// along each pixel's motion over the time the shutter is open. Off by
// default (graphics setting "motionBlur").
//
// No velocity buffer: each pixel's motion comes from its depth and last
// frame's camera, the same reprojection TAA uses, so nothing extra is drawn
// and every shader and module gets it for free. Moving things themselves
// (a character's arm, a car passing a still camera) are not smeared; a
// still camera gives a still image. One compute pass gathers along each
// pixel's motion and weighs every sample by depth (McGuire et al. 2012):
// a sample in front counts where its own smear reaches this pixel, one
// behind only within this pixel's smear, so a sharp thing near the camera
// never smears the background over itself.
struct MotionBlurSettings {
    float shutter = 0.5f;   // the share of the frame time the shutter is open (0.5 is film's 180 degrees)
    float maxLength = 0.04f; // the longest smear, as a share of the image width
    int samples = 12;       // gather samples along the motion
};

// Where pixel `uv` (0..1) at depth `depth` was last frame, in uv, through `reproject`
// (last frame's viewProj times this frame's inverse viewProj).
glm::vec2 motionPrevious(const glm::vec2& uv, float depth, const glm::mat4& reproject);
// The smear in pixels for a motion of `velocityPixels` per frame: times the shutter, at most
// `maxPixels` long (the direction kept).
glm::vec2 motionSmear(const glm::vec2& velocityPixels, float shutter, float maxPixels);
// Distance from the camera along the view of NDC depth `depth`, for a projection whose
// [2][2] is `a` and [3][2] is `b` (any depth range).
float motionViewDistance(float depth, float a, float b);
// How much `nearer` (m) counts as in front of `farther` (m): 1 well in front, 0 behind, a soft
// step 2% of the distance wide between.
float motionInFront(float nearer, float farther);

} // namespace kke
