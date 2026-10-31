#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include <glm/glm.hpp>

namespace kke {

class MpmWater;
class VulkanDevice;

// kke::MpmWater on the GPU (docs/OPTIMIZATION.md #46): the particles live
// in GPU memory and every substep is four compute passes
// (shaders/mpm_p2g.comp, mpm_grid.comp, mpm_bodies.comp, mpm_g2p.comp),
// recorded into one submit a step; the step then reads back positions,
// velocities and what the water did to the bodies.
//
//     water.setGpu(kke::MpmWaterGpu::create(app.device()));
//
// Particles write the grid with integer atomics (fixed point), which every
// Vulkan GPU has, so the sums don't depend on the order the GPU runs them
// in. create() gives null on a software GPU such as lavapipe (the CPU is
// faster there) unless KKE_MPM_GPU=1; KKE_MPM_GPU=0 turns it off. It uses
// a compute queue of its own when there is one, else the graphics queue.
// One MpmWaterGpu serves one MpmWater.
class MpmWaterGpu {
public:
    static std::shared_ptr<MpmWaterGpu> create(VulkanDevice& device);
    ~MpmWaterGpu();
    MpmWaterGpu(const MpmWaterGpu&) = delete;
    MpmWaterGpu& operator=(const MpmWaterGpu&) = delete;

    // Fixed point of the grid's mass and momentum (per particle mass) and
    // of the bodies' impulses (N s): shaders/mpm_common.glsl has the same.
    static constexpr float kGridScale = 524288.0f;  // 2^19
    static constexpr float kImpulseScale = 16384.0f; // 2^14

    // Used by MpmWater.
    // `substeps` of `dt` each; false = not finished in time (the water
    // skips this step and the GPU is used again once it has finished).
    bool step(MpmWater& water, float dt, int substeps);
    // The particles' full state (velocity gradients and volumes too) into
    // the water's arrays.
    void download(MpmWater& water);

private:
    struct Impl;
    explicit MpmWaterGpu(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> m_impl;
};

} // namespace kke
