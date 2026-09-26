#include "kke/DrawOrder.h"

#include <algorithm>
#include <numeric>

namespace kke {

std::vector<uint32_t> frontToBackOrder(const std::vector<glm::vec3>& positions, const glm::vec3& eye) {
    std::vector<float> dist(positions.size());
    for (size_t i = 0; i < positions.size(); ++i) {
        const glm::vec3 d = positions[i] - eye;
        dist[i] = glm::dot(d, d);
    }
    std::vector<uint32_t> order(positions.size());
    std::iota(order.begin(), order.end(), 0u);
    std::stable_sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) { return dist[a] < dist[b]; });
    return order;
}

} // namespace kke
