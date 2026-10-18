#pragma once

#include <glm/glm.hpp>

#include <vector>

namespace kke {

// Depth of field (ENG-F10 part 3): what is nearer or farther than the focus
// is blurred as a camera lens would, a circle of confusion per pixel from a
// thin lens. Off by default (graphics setting "depthOfField"); a game turns it
// on for a cinematic look or sets the focus itself.
//
// One gather at half resolution, no tiles: every pixel looks out to the
// largest circle, and each sample counts where its own circle reaches the
// pixel (scatter as gather). Near samples spread over sharper pixels behind
// them, far samples never over nearer ones, so in-focus edges keep no halo.
// Auto focus reads the depth at the middle of the screen on the GPU and
// follows it smoothly, with nothing read back.
struct DofSettings {
    float focusDistance = 0.0f; // metres; 0 = auto focus on what is at the middle of the screen
    float fStop = 1.4f;         // the aperture: smaller numbers blur more
    // Millimetres. A game camera's wide view (60 degrees is a 21 mm lens) leaves almost everything sharp, as it
    // would on a real camera, so the blur is that of a 50 mm portrait lens by default; 0 = the lens that matches
    // the field of view on a 24 mm high sensor.
    float focalLength = 50.0f;
    float maxBlur = 0.012f;     // the largest circle's radius, as a share of the image height
    float focusSpeed = 6.0f;    // how fast auto focus follows (per second)
};

// The focal length (mm) that gives vertical field of view `fovY` (radians) on a 24 mm high sensor.
float dofFocalLength(float fovY);
// The signed radius in pixels of the circle a point at `distance` (m) makes on an image `imageHeight`
// pixels high, with the lens focused at `focus` (m): negative nearer than the focus, positive farther,
// at most `maxRadius` either way.
float dofCircle(float distance, float focus, float focalLengthMm, float fStop, float imageHeight, float maxRadius);
// Auto focus one frame on: `current` moved toward `target` (m) at `speed` per second over `dt` seconds,
// evenly in log distance (as near as far); a `current` of 0 (nothing yet) jumps straight to `target`.
float dofFollow(float current, float target, float speed, float dt);
// Sample `index` of `count` in the unit disc, as the gather uses them: golden-angle spiral, even by area,
// sorted from the middle out (shaders/dof.glsl's dofTap).
glm::vec2 dofTap(int index, int count);

} // namespace kke
