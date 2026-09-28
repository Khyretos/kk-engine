#pragma once

#include <cstdint>
#include <memory>
#include <vector>

namespace kke {

class VulkanDevice;

// The cloth protection pass's pair search on the GPU (docs/CLOTH.md, "On
// the GPU"): which vertices may touch which triangles and which edges
// which edges, the part of Full protection that costs. The GPU looks at
// every candidate at once, where the CPU goes through dozens per vertex
// for each real pair in a crumpled pile. It runs on a queue of its own
// beside the frame, so the physics step waits only for the search, not
// for the frame being drawn.
//
//   world.setClothGpu(kke::ClothGpu::create(app.device()));
//
// create() gives null when the GPU has no queue of its own for compute
// (the physics then searches on the CPU, as always). KKE_CLOTH_GPU=0 turns
// it off; KKE_CLOTH_GPU=shared uses the graphics queue instead (for
// testing on a GPU with one queue: the search then waits for the frame).
// The CPU also takes over whenever the GPU can't answer (more pairs than
// its buffers hold, which it grows for next time), and whenever it is the
// faster of the two here: the physics times both now and then (on a phone
// the GPU, busy drawing, can take longer than the CPU).
class ClothGpu {
public:
    static std::shared_ptr<ClothGpu> create(VulkanDevice& device);
    ~ClothGpu();
    ClothGpu(const ClothGpu&) = delete;
    ClothGpu& operator=(const ClothGpu&) = delete;

    struct Stats {
        uint64_t searches = 0; // answered by the GPU
        uint64_t declined = 0; // left to the CPU
        double lastMs = 0.0;   // the last search, upload to answer, as the physics waited for it
        double gpuMs = 0.0;    // a search on the GPU, on average (as the physics waited for it)
        double cpuMs = 0.0;    // the same on the CPU (0 = not timed yet)
        bool onCpu = false;    // the CPU is faster here, so it searches (the GPU is tried again now and then)
    };
    Stats stats() const { return m_stats; }
    // For RigidWorld: the averages it compares, and which one it picked.
    void setTimes(double gpuMs, double cpuMs, bool onCpu) {
        m_stats.gpuMs = gpuMs;
        m_stats.cpuMs = cpuMs;
        m_stats.onCpu = onCpu;
    }

    // For RigidWorld: the queries packed by the cloth pass (the layout is
    // shaders/cloth_pairs.comp's), answered as pairs of pass-wide indices
    // and a flag per vertex. False: the CPU must do it.
    bool search(std::vector<uint32_t>& words, uint32_t vertices, uint32_t edges, std::vector<uint32_t>& vt, std::vector<uint32_t>& ee,
                std::vector<uint32_t>& flags);

private:
    struct Impl;
    explicit ClothGpu(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> m_impl;
    Stats m_stats;
};

} // namespace kke
