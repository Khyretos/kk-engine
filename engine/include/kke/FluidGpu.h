#pragma once

#include "kke/ParticleFluid.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace kke {

class VulkanDevice;

// The particle liquid's neighbour search on the GPU (docs/OPTIMIZATION.md
// #45): the 27-cell queries that cost about half of a kke::ParticleFluid
// step on the CPU, done by one GPU thread per particle
// (shaders/fluid_neighbours.comp). It runs on a queue of its own beside
// the frame, so the step waits only for the search.
//
//     fluid.setNeighbourSearch(kke::FluidGpu::create(app.device()));
//
// create() gives null when the GPU has no queue of its own for compute
// (the fluid then searches on the CPU, as always). KKE_FLUID_GPU=0 turns
// it off; KKE_FLUID_GPU=shared uses the graphics queue instead (a GPU with
// one queue, such as lavapipe: the search then waits for the frame);
// KKE_FLUID_GPU_CHECK=1 has the fluid search on both and count any
// difference (ParticleFluid::searchStats(); slow). The fluid itself keeps
// to whichever of GPU and CPU is faster on this machine.
class FluidGpu : public FluidNeighbourSearch {
public:
    static std::shared_ptr<FluidGpu> create(VulkanDevice& device);
    ~FluidGpu() override;
    FluidGpu(const FluidGpu&) = delete;
    FluidGpu& operator=(const FluidGpu&) = delete;

    bool search(const Query& q, uint32_t& stride, std::vector<uint32_t>& list, std::vector<uint32_t>& counts) override;
    bool checkAgainstCpu() const override { return m_check; }

private:
    struct Impl;
    explicit FluidGpu(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> m_impl;
    bool m_check = false;
    std::vector<uint32_t> m_words, m_out;
};

} // namespace kke
