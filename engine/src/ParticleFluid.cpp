#include "kke/ParticleFluid.h"

#include "kke/TaskPool.h"

#include <algorithm>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdint>

namespace kke {

namespace {
constexpr float kPi = 3.14159265358979f;

inline uint32_t hashCell(int x, int y, int z, uint32_t mask) {
    return (static_cast<uint32_t>(x) * 73856093u ^ static_cast<uint32_t>(y) * 19349663u ^ static_cast<uint32_t>(z) * 83492791u) & mask;
}
} // namespace

ParticleFluid::ParticleFluid(const Params& params, size_t maxParticles) : m_params(params), m_capacity(maxParticles) {
    m_pos.reserve(maxParticles);
    m_vel.reserve(maxParticles);
    m_temp.reserve(maxParticles);
    m_material.reserve(maxParticles);
}

bool ParticleFluid::add(const glm::vec3& position, const glm::vec3& velocity, float temperature, uint8_t material) {
    if (m_pos.size() >= m_capacity) return false;
    m_pos.push_back(position);
    m_vel.push_back(velocity);
    m_temp.push_back(temperature);
    m_material.push_back(material);
    return true;
}

void ParticleFluid::clear() {
    m_pos.clear();
    m_vel.clear();
    m_temp.clear();
    m_material.clear();
}

void ParticleFluid::removeIf(const std::function<bool(size_t)>& pred) {
    size_t n = m_pos.size();
    for (size_t i = 0; i < n;) {
        if (pred(i)) {
            --n;
            m_pos[i] = m_pos[n];
            m_vel[i] = m_vel[n];
            m_temp[i] = m_temp[n];
            m_material[i] = m_material[n];
            // re-test the element moved into slot i: pred sees current data
        } else {
            ++i;
        }
    }
    m_pos.resize(n);
    m_vel.resize(n);
    m_temp.resize(n);
    m_material.resize(n);
}

FluidMaterial ParticleFluid::materialOf(uint8_t id) const {
    if (id < 8 && m_materialSet[id]) return m_materials[id];
    FluidMaterial m;
    m.viscosityHot = m_params.viscosityHot;
    m.viscosityCold = m_params.viscosityCold;
    m.hotTemperature = m_params.hotTemperature;
    m.solidifyTemperature = m_params.solidifyTemperature;
    return m;
}

void ParticleFluid::buildGrid(const std::vector<glm::vec3>& p) {
    const size_t n = p.size();
    uint32_t size = 64;
    while (size < n * 2) size <<= 1;
    m_tableSize = size;
    const uint32_t mask = size - 1;
    m_cellOf.resize(n);
    m_cellStart.assign(size + 1, 0);
    const float inv = 1.0f / m_h;
    for (size_t i = 0; i < n; ++i) {
        glm::ivec3 c = glm::ivec3(glm::floor(p[i] * inv));
        uint32_t k = hashCell(c.x, c.y, c.z, mask);
        m_cellOf[i] = k;
        ++m_cellStart[k + 1];
    }
    for (uint32_t k = 0; k < size; ++k) m_cellStart[k + 1] += m_cellStart[k];
    m_sorted.resize(n);
    m_cursor.assign(m_cellStart.begin(), m_cellStart.end() - 1);
    for (size_t i = 0; i < n; ++i) m_sorted[m_cursor[m_cellOf[i]]++] = static_cast<uint32_t>(i);
}

// The 27 cells around `c`, as distinct hash buckets (cells that hash to
// the same bucket would otherwise be visited twice). Returns how many.
int ParticleFluid::bucketsAround(const glm::ivec3& c, uint32_t (&out)[27]) const {
    const uint32_t mask = m_tableSize - 1;
    int count = 0;
    for (int dz = -1; dz <= 1; ++dz)
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                uint32_t k = hashCell(c.x + dx, c.y + dy, c.z + dz, mask);
                if (m_cellStart[k] == m_cellStart[k + 1]) continue; // empty: nothing to visit
                bool dup = false;
                for (int s = 0; s < count; ++s) if (out[s] == k) { dup = true; break; }
                if (!dup) out[count++] = k;
            }
    return count;
}

// Neighbour lists for slots [begin, end) of the sorted order, appended to
// `list`; start[s] / count[s] say where each slot's list is. Slots of one
// cell are next to each other, so the 27 buckets are worked out once per
// cell, and the positions read are the sorted copy: contiguous memory.
void ParticleFluid::findNeighbours(size_t begin, size_t end, std::vector<uint32_t>& list) {
    const float inv = 1.0f / m_h;
    const float h2 = m_h * m_h;
    const glm::vec3* P = m_pred.data();
    uint32_t buckets[27];
    int nBuckets = 0;
    glm::ivec3 lastCell(INT32_MIN);
    for (size_t s = begin; s < end; ++s) {
        const glm::vec3 pi = P[s];
        const glm::ivec3 c = glm::ivec3(glm::floor(pi * inv));
        if (c != lastCell) {
            nBuckets = bucketsAround(c, buckets);
            lastCell = c;
        }
        m_nbrStart[s] = static_cast<uint32_t>(list.size());
        for (int b = 0; b < nBuckets; ++b) {
            const uint32_t k = buckets[b];
            for (uint32_t t = m_cellStart[k], e = m_cellStart[k + 1]; t < e; ++t) {
                if (t == s) continue;
                glm::vec3 r = pi - P[t];
                if (glm::dot(r, r) < h2) list.push_back(t);
            }
        }
        m_nbrCount[s] = static_cast<uint32_t>(list.size()) - m_nbrStart[s];
    }
}

void ParticleFluid::collide(glm::vec3& p, const glm::vec3& prev) const {
    const float r = m_params.radius;
    auto frictionFrom = [&](const glm::vec3& n) {
        // Remove part of the tangential motion this step.
        glm::vec3 motion = p - prev;
        glm::vec3 tangential = motion - n * glm::dot(motion, n);
        p -= tangential * m_params.friction;
    };
    if (p.y < m_params.groundY + r) {
        p.y = m_params.groundY + r;
        frictionFrom(glm::vec3(0, 1, 0));
    }
    for (int a = 0; a < 3; ++a) {
        if (p[a] < m_params.boundsMin[a] + r) p[a] = m_params.boundsMin[a] + r;
        if (p[a] > m_params.boundsMax[a] - r) p[a] = m_params.boundsMax[a] - r;
    }
    for (const Collider& col : m_colliders) {
        glm::vec3 n(0, 1, 0);
        float d = col(p, n) - r;
        if (d < 0.0f) {
            p -= n * d;
            frictionFrom(n);
        }
    }
}

void ParticleFluid::buildNeighbourListsOnCpu() {
    const size_t n = m_pred.size();
    m_nbrStart.resize(n);
    m_nbrCount.resize(n);
    const size_t workers = m_pool ? static_cast<size_t>(m_pool->workers()) : 1;
    const size_t chunks = (workers > 1 && n >= 2 * kGrain) ? std::min(n / kGrain, workers * 4) : 1;
    if (chunks == 1) {
        m_nbr.clear();
        findNeighbours(0, n, m_nbr);
        return;
    }
    // Each chunk of slots fills a list of its own, then the lists are
    // joined: same result as one thread, in the same order.
    const size_t per = (n + chunks - 1) / chunks;
    if (m_chunkLists.size() < chunks) m_chunkLists.resize(chunks);
    m_chunkBase.resize(chunks + 1);
    m_pool->run(chunks, [&](size_t c) {
        std::vector<uint32_t>& list = m_chunkLists[c];
        list.clear();
        const size_t b = c * per, e = std::min(n, b + per);
        if (b < e) findNeighbours(b, e, list);
    });
    m_chunkBase[0] = 0;
    for (size_t c = 0; c < chunks; ++c) m_chunkBase[c + 1] = m_chunkBase[c] + static_cast<uint32_t>(m_chunkLists[c].size());
    m_nbr.resize(m_chunkBase[chunks]);
    m_pool->run(chunks, [&](size_t c) {
        const std::vector<uint32_t>& list = m_chunkLists[c];
        std::copy(list.begin(), list.end(), m_nbr.begin() + m_chunkBase[c]);
        const size_t b = c * per, e = std::min(n, b + per);
        for (size_t s = b; s < e; ++s) m_nbrStart[s] += m_chunkBase[c];
    });
}

namespace {
void average(double& avg, double ms) { avg = avg == 0.0 ? ms : avg * 0.9 + ms * 0.1; }
} // namespace

// Lists from the search set with setNeighbourSearch() (the GPU) while it
// is the faster, else from the CPU; both give the same lists.
void ParticleFluid::buildNeighbourLists() {
    const size_t n = m_pred.size();
    constexpr size_t kElsewhereFrom = 1024; // below this the CPU always wins (a GPU round trip alone is ~0.1 ms)
    constexpr uint64_t kTimeCpuEvery = 64;
    if (!m_search || n < kElsewhereFrom) {
        buildNeighbourListsOnCpu();
        return;
    }
    using Clock = std::chrono::steady_clock;
    auto ms = [](Clock::time_point a) { return std::chrono::duration<double, std::milli>(Clock::now() - a).count(); };
    SearchStats& st = m_searchStats;
    const bool timeCpu = ++m_searchCalls % kTimeCpuEvery == 1 || st.cpuMs == 0.0;
    const bool check = m_search->checkAgainstCpu();
    bool found = false;
    // Three declines in a row (more neighbours than the GPU's lists can
    // ever hold, a GPU that keeps timing out): the CPU searches until the
    // next timing round.
    const bool backedOff = m_searchDeclinedInARow >= 3 && !timeCpu;
    if ((!st.onCpu || timeCpu || check) && !backedOff) {
        FluidNeighbourSearch::Query q;
        q.positions = m_pred.data();
        q.count = n;
        q.cellStart = m_cellStart.data();
        q.tableSize = m_tableSize;
        q.invCellSize = 1.0f / m_h;
        q.radius2 = m_h * m_h;
        const auto t0 = Clock::now();
        found = m_search->search(q, m_searchStride, m_nbr, m_nbrCount);
        if (found) {
            average(st.elsewhereMs, ms(t0));
            ++st.elsewhere;
            m_searchDeclinedInARow = 0;
            m_nbrStart.resize(n);
            for (size_t s = 0; s < n; ++s) m_nbrStart[s] = static_cast<uint32_t>(s * m_searchStride);
        } else {
            ++st.declined;
            ++m_searchDeclinedInARow;
        }
    }
    if (!found || timeCpu || check) {
        if (found) {
            // Keep the answer; the CPU's own goes to scratch to be timed or compared.
            m_checkList.swap(m_nbr);
            m_checkStart.swap(m_nbrStart);
            m_checkCount.swap(m_nbrCount);
        }
        const auto t0 = Clock::now();
        buildNeighbourListsOnCpu();
        average(st.cpuMs, ms(t0));
        if (found) {
            if (check) {
                ++st.checks;
                for (size_t s = 0; s < n; ++s) {
                    if (m_nbrCount[s] != m_checkCount[s] ||
                        !std::equal(m_nbr.begin() + m_nbrStart[s], m_nbr.begin() + m_nbrStart[s] + m_nbrCount[s], m_checkList.begin() + m_checkStart[s])) {
                        ++st.differed;
                        break;
                    }
                }
            }
            // Go on with the search's answer: that is the one being timed or checked.
            m_checkList.swap(m_nbr);
            m_checkStart.swap(m_nbrStart);
            m_checkCount.swap(m_nbrCount);
        }
    }
    if (st.elsewhereMs > 0.0 && st.cpuMs > 0.0) st.onCpu = st.cpuMs < st.elsewhereMs;
}

void ParticleFluid::step(float dt) {
    if (m_pos.empty() || dt <= 0.0f) return;
    // Two substeps at 60 Hz: PBF is stable at large steps, but thin
    // streams and fast pours tunnel less at 120 Hz.
    const int substeps = std::max(1, m_params.substeps);
    const float h = dt / static_cast<float>(substeps);
    const size_t n = m_pos.size();

    // Kernel constants from the particle size: spacing d = 2r, kernel
    // radius h = 2d (~30 neighbours), mass so a d-lattice is at rest density.
    m_h = m_params.radius * 4.0f;
    const float d = m_params.radius * 2.0f;
    m_mass = m_params.restDensity * d * d * d;
    m_poly6 = 315.0f / (64.0f * kPi * std::pow(m_h, 9.0f));
    m_spikyGrad = -45.0f / (kPi * std::pow(m_h, 6.0f));
    const float h2 = m_h * m_h;
    const float invRest = 1.0f / m_params.restDensity;
    const float relaxation = 1e-6f * m_params.restDensity; // epsilon in lambda's denominator (CFM)

    // The solver works on copies in cell order ("slots"): particle
    // m_sorted[s] lives in slot s while a substep runs, so a cell's
    // particles, and mostly its neighbours', sit side by side in memory.
    // Galaxy Engine's neighbour search (MIT, docs/RESEARCH.md) does the
    // same with its sorted entry arrays.
    m_predIn.resize(n);
    m_pred.resize(n);
    m_sPos.resize(n);
    m_sVel.resize(n);
    m_sTemp.resize(n);
    m_sMat.resize(n);
    m_delta.resize(n);
    m_sDensity.resize(n);
    m_density.resize(n);
    m_lambda.resize(n);
    m_scratchVel.resize(n);
    m_scratchTemp.resize(n);

    FluidMaterial mats[8];
    for (uint8_t m = 0; m < 8; ++m) mats[m] = materialOf(m);
    auto frozen = [&](uint8_t material, float temperature) {
        const FluidMaterial& m = mats[material & 7];
        return m.solidifyTemperature > -1e8f && temperature < m.solidifyTemperature;
    };
    TaskPool* pool = m_pool;
    auto each = [&](auto&& body) {
        TaskPool::forRanges(pool, n, kGrain, [&](size_t b, size_t e) {
            for (size_t i = b; i < e; ++i) body(i);
        });
    };
    auto eachNeighbour = [&](size_t s, auto&& f) {
        const uint32_t* list = m_nbr.data() + m_nbrStart[s];
        const uint32_t count = m_nbrCount[s];
        const glm::vec3 ps = m_pred[s];
        for (uint32_t k = 0; k < count; ++k) {
            uint32_t t = list[k];
            glm::vec3 r = ps - m_pred[t];
            float dd = glm::dot(r, r);
            if (dd < h2) f(t, r, dd);
        }
    };

    for (int sub = 0; sub < substeps; ++sub) {
        // 1. Predict.
        each([&](size_t i) {
            // Solidified particles (crust) aren't pinned — they'd hang in
            // the air when what's under them melts — they're heavily damped:
            // they sag and settle but no longer flow.
            if (frozen(m_material[i], m_temp[i])) m_vel[i] *= 0.92f;
            m_vel[i] += m_params.gravity * h;
            m_predIn[i] = m_pos[i] + m_vel[i] * h;
            collide(m_predIn[i], m_pos[i]);
        });
        buildGrid(m_predIn);
        each([&](size_t s) {
            const uint32_t i = m_sorted[s];
            m_pred[s] = m_predIn[i];
            m_sPos[s] = m_pos[i];
            m_sVel[s] = m_vel[i];
            m_sTemp[s] = m_temp[i];
            m_sMat[s] = m_material[i];
        });
        // Neighbour lists once per substep, reused by every solver pass
        // below — the grid search runs 1x per substep instead of 7x.
        // Distances are re-checked on use.
        buildNeighbourLists();

        // 2. Density constraints (unilateral: only push apart when
        // compressed, so the free surface doesn't clump — a common game
        // simplification that also removes the need for s_corr).
        for (int it = 0; it < m_params.iterations; ++it) {
            each([&](size_t s) {
                float rho = m_mass * m_poly6 * h2 * h2 * h2; // self contribution
                glm::vec3 gradI(0.0f);
                float sumGrad2 = 0.0f;
                eachNeighbour(s, [&](uint32_t, const glm::vec3& r, float dd) {
                    float w = h2 - dd;
                    rho += m_mass * m_poly6 * w * w * w;
                    float len = std::sqrt(dd);
                    if (len > 1e-9f) {
                        float x = m_h - len;
                        glm::vec3 g = (m_mass * invRest) * m_spikyGrad * x * x * (r / len);
                        gradI += g;
                        sumGrad2 += glm::dot(g, g);
                    }
                });
                sumGrad2 += glm::dot(gradI, gradI);
                m_sDensity[s] = rho;
                float c = std::max(rho * invRest - 1.0f, 0.0f);
                m_lambda[s] = -c / (sumGrad2 + relaxation);
            });
            each([&](size_t s) {
                glm::vec3 dp(0.0f);
                eachNeighbour(s, [&](uint32_t t, const glm::vec3& r, float dd) {
                    float len = std::sqrt(dd);
                    if (len < 1e-9f) return;
                    float x = m_h - len;
                    dp += (m_lambda[s] + m_lambda[t]) * m_spikyGrad * x * x * (r / len);
                });
                m_delta[s] = dp * (m_mass * invRest);
            });
            each([&](size_t s) {
                glm::vec3 before = m_pred[s];
                // Cap one iteration's correction at half a particle: deep
                // overlaps resolve over a few iterations instead of launching
                // the particle (the velocity comes from this displacement).
                glm::vec3 dp = m_delta[s];
                float len = glm::length(dp);
                const float maxDp = m_params.radius * 0.5f;
                if (len > maxDp) dp *= maxDp / len;
                m_pred[s] += dp;
                collide(m_pred[s], before);
            });
        }

        // 3. Velocities, XSPH viscosity and heat.
        const float invH = 1.0f / h;
        each([&](size_t s) {
            m_sVel[s] = (m_pred[s] - m_sPos[s]) * invH;
            float sp = glm::length(m_sVel[s]);
            if (sp > m_params.maxSpeed) m_sVel[s] *= m_params.maxSpeed / sp;
        });
        each([&](size_t s) {
            glm::vec3 dv(0.0f);
            float wsum = 0.0f, tsum = 0.0f;
            eachNeighbour(s, [&](uint32_t t, const glm::vec3&, float dd) {
                float w = h2 - dd;
                w = w * w * w;
                dv += (m_sVel[t] - m_sVel[s]) * w;
                tsum += (m_sTemp[t] - m_sTemp[s]) * w;
                wsum += w;
            });
            const FluidMaterial& mat = mats[m_sMat[s] & 7];
            const float coldEnd = mat.solidifyTemperature > -1e8f ? mat.solidifyTemperature : m_params.ambientTemperature;
            const float span = std::max(1.0f, mat.hotTemperature - coldEnd);
            float hot = std::clamp((m_sTemp[s] - coldEnd) / span, 0.0f, 1.0f);
            float visc = frozen(m_sMat[s], m_sTemp[s]) ? 0.35f : mat.viscosityCold + (mat.viscosityHot - mat.viscosityCold) * hot; // solid: moves with its neighbours
            m_scratchVel[s] = wsum > 0.0f ? m_sVel[s] + dv * (visc / wsum) : m_sVel[s];
            float temp = m_sTemp[s];
            if (wsum > 0.0f) temp += tsum / wsum * std::min(1.0f, m_params.heatDiffusion * h);
            temp -= (temp - m_params.ambientTemperature) * std::min(1.0f, m_params.coolingRate * h);
            m_scratchTemp[s] = temp;
        });
        // Back to the particles' own order (the order callers index).
        each([&](size_t s) {
            const uint32_t i = m_sorted[s];
            m_vel[i] = m_scratchVel[s];
            m_temp[i] = m_scratchTemp[s];
            m_pos[i] = m_pred[s];
            m_density[i] = m_sDensity[s];
        });
    }
}

} // namespace kke
