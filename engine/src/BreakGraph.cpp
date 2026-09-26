#include "kke/BreakGraph.h"

#include <algorithm>
#include <map>
#include <unordered_map>

namespace kke {

BreakGraph::BreakGraph(const TetMeshData& mesh, std::vector<uint32_t> chunkOfTet, std::vector<float> strength)
    : m_chunk(std::move(chunkOfTet)), m_strength(std::move(strength)) {
    const size_t nt = mesh.tets.size();
    m_chunk.resize(nt, 0);
    if (m_strength.size() != nt) m_strength.assign(nt, 1.0f);
    m_neighbour.assign(nt, { UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX });
    std::map<std::array<uint32_t, 3>, std::pair<uint32_t, int>> open;
    for (uint32_t t = 0; t < nt; ++t)
        for (int f = 0; f < 4; ++f) {
            std::array<uint32_t, 3> k{};
            int j = 0;
            for (int i = 0; i < 4; ++i) if (i != f) k[j++] = mesh.tets[t][i];
            std::sort(k.begin(), k.end());
            auto it = open.find(k);
            if (it == open.end()) { open.emplace(k, std::make_pair(t, f)); continue; }
            m_neighbour[t][f] = it->second.first;
            m_neighbour[it->second.first][it->second.second] = t;
            open.erase(it);
        }
    m_border.assign(nt, 0);
    m_exterior.assign(nt, 0);
    for (uint32_t t = 0; t < nt; ++t)
        for (int f = 0; f < 4; ++f) {
            uint32_t n = m_neighbour[t][f];
            if (n == UINT32_MAX) m_exterior[t] |= static_cast<uint8_t>(1u << f);
            else if (m_chunk[n] != m_chunk[t]) {
                m_border[t] = 1;
                m_borders.insert(key(m_chunk[t], m_chunk[n]));
            }
        }
    m_threshold.assign(nt, 0.0f);
}

uint64_t BreakGraph::key(uint32_t a, uint32_t b) {
    return a < b ? (static_cast<uint64_t>(a) << 32 | b) : (static_cast<uint64_t>(b) << 32 | a);
}

void BreakGraph::arm(float baseThreshold, const std::vector<float>& rest, float restFactor) {
    for (size_t t = 0; t < m_threshold.size(); ++t)
        m_threshold[t] = baseThreshold * m_strength[t] + (t < rest.size() ? restFactor * rest[t] : 0.0f);
}

bool BreakGraph::report(uint32_t tet, float stress, const std::function<bool(uint32_t)>& sameBody) {
    if (!m_border[tet] || !(stress > m_threshold[tet])) return false;
    bool fresh = false;
    for (int f = 0; f < 4; ++f) {
        uint32_t n = m_neighbour[tet][f];
        if (n == UINT32_MAX || m_chunk[n] == m_chunk[tet] || !sameBody(n)) continue;
        fresh |= m_broken.insert(key(m_chunk[tet], m_chunk[n])).second;
    }
    return fresh;
}

bool BreakGraph::isBroken(uint32_t a, uint32_t b) const { return m_broken.count(key(a, b)) != 0; }

std::vector<std::pair<uint32_t, uint32_t>> BreakGraph::brokenBorders() const {
    std::vector<std::pair<uint32_t, uint32_t>> out;
    out.reserve(m_broken.size());
    for (uint64_t k : m_broken) out.push_back({ static_cast<uint32_t>(k >> 32), static_cast<uint32_t>(k & 0xFFFFFFFFu) });
    std::sort(out.begin(), out.end());
    return out;
}

bool BreakGraph::breakBorder(uint32_t a, uint32_t b) {
    const uint64_t k = key(a, b);
    if (a == b || !m_borders.count(k)) return false;
    return m_broken.insert(k).second;
}

std::vector<std::vector<uint32_t>> BreakGraph::groups(const std::vector<uint32_t>& bodyTets, const std::function<bool(uint32_t)>& sameBody) const {
    std::unordered_map<uint32_t, uint32_t> parent;
    auto find = [&](uint32_t x) {
        uint32_t r = x;
        while (parent[r] != r) r = parent[r];
        while (parent[x] != r) { uint32_t n = parent[x]; parent[x] = r; x = n; }
        return r;
    };
    for (uint32_t t : bodyTets) parent.emplace(m_chunk[t], m_chunk[t]);
    for (uint32_t t : bodyTets)
        for (int f = 0; f < 4; ++f) {
            uint32_t n = m_neighbour[t][f];
            if (n == UINT32_MAX || !sameBody(n)) continue;
            uint32_t a = m_chunk[t], b = m_chunk[n];
            if (a == b || isBroken(a, b)) continue;
            uint32_t ra = find(a), rb = find(b);
            if (ra != rb) parent[std::max(ra, rb)] = std::min(ra, rb); // root = smallest id: deterministic order
        }
    std::map<uint32_t, std::vector<uint32_t>> byRoot;
    for (uint32_t t : bodyTets) byRoot[find(m_chunk[t])].push_back(t);
    std::vector<std::vector<uint32_t>> out;
    out.reserve(byRoot.size());
    for (auto& [root, tets] : byRoot) out.push_back(std::move(tets));
    return out;
}

} // namespace kke
