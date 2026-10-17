#include "kke/GpuParticleRing.h"

#include <algorithm>

namespace kke {

GpuParticleRing::GpuParticleRing(size_t initial, size_t max)
    : m_diesAt(std::max<size_t>(1, std::min(initial, max)), -1.0f), m_max(std::max<size_t>(1, max)) {}

uint32_t GpuParticleRing::allocate(float now, float diesAt) {
    if (m_head >= m_diesAt.size()) m_head = 0;
    if (m_diesAt[m_head] > now && m_diesAt.size() < m_max) {
        // The next slot is still in use: twice the slots, carry on in the new half.
        m_head = m_diesAt.size();
        m_diesAt.resize(std::min(m_diesAt.size() * 2, m_max), -1.0f);
    }
    const size_t slot = m_head++;
    m_diesAt[slot] = diesAt;
    m_used = std::max(m_used, slot + 1);
    return static_cast<uint32_t>(slot);
}

size_t GpuParticleRing::alive(float now) const {
    return static_cast<size_t>(std::count_if(m_diesAt.begin(), m_diesAt.begin() + static_cast<std::ptrdiff_t>(m_used),
                                             [now](float t) { return t > now; }));
}

void GpuParticleRing::clear() {
    std::fill(m_diesAt.begin(), m_diesAt.end(), -1.0f);
    m_head = 0;
    m_used = 0;
}

} // namespace kke
