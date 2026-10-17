#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace kke {

// The slots of one kind's GPU particles (ENG-G18 part 4): a particle is
// written into a slot once, when it is born, and drawn from it until it
// dies. Slots are handed out in turn round the ring; when the next one
// still holds a living particle the ring doubles (up to `max`), and at
// `max` the living one is overwritten (the oldest goes, as on the CPU).
class GpuParticleRing {
public:
    explicit GpuParticleRing(size_t initial = 1024, size_t max = 262144);
    // A slot for a particle that dies at `diesAt` (seconds on the sim's clock); `now` is the clock.
    uint32_t allocate(float now, float diesAt);
    size_t capacity() const { return m_diesAt.size(); }
    // One past the highest slot ever handed out: what a draw covers.
    size_t used() const { return m_used; }
    // How many slots hold a living particle at `now`.
    size_t alive(float now) const;
    // Every slot free again (nothing alive).
    void clear();

private:
    std::vector<float> m_diesAt;
    size_t m_max;
    size_t m_head = 0;
    size_t m_used = 0;
};

} // namespace kke
