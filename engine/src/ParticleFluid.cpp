#include "kke/ParticleFluid.h"

#include <algorithm>
#include <cmath>

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
    // Scatter with a running cursor per cell (reuse m_lambda storage as
    // cursors would alias floats; keep a local copy of starts instead).
    static thread_local std::vector<uint32_t> cursor;
    cursor.assign(m_cellStart.begin(), m_cellStart.end() - 1);
    for (size_t i = 0; i < n; ++i) m_sorted[cursor[m_cellOf[i]]++] = static_cast<uint32_t>(i);
}

template <typename F>
void ParticleFluid::forNeighbours(size_t i, const std::vector<glm::vec3>& p, F&& f) const {
    const float inv = 1.0f / m_h;
    const float h2 = m_h * m_h;
    const glm::ivec3 c = glm::ivec3(glm::floor(p[i] * inv));
    const uint32_t mask = m_tableSize - 1;
    // Cells hashing to the same bucket would be visited twice; dedupe the
    // (at most 27) buckets first.
    uint32_t seen[27];
    int nSeen = 0;
    for (int dz = -1; dz <= 1; ++dz)
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                uint32_t k = hashCell(c.x + dx, c.y + dy, c.z + dz, mask);
                bool dup = false;
                for (int s = 0; s < nSeen; ++s) if (seen[s] == k) { dup = true; break; }
                if (dup) continue;
                seen[nSeen++] = k;
                for (uint32_t s = m_cellStart[k]; s < m_cellStart[k + 1]; ++s) {
                    uint32_t j = m_sorted[s];
                    if (j == i) continue;
                    glm::vec3 r = p[i] - p[j];
                    float d2 = glm::dot(r, r);
                    if (d2 < h2) f(j, r, d2);
                }
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

    m_pred.resize(n);
    m_delta.resize(n);
    m_density.resize(n);
    m_lambda.resize(n);

    FluidMaterial mats[8];
    for (uint8_t m = 0; m < 8; ++m) mats[m] = materialOf(m);
    auto frozen = [&](size_t i) {
        const FluidMaterial& m = mats[m_material[i] & 7];
        return m.solidifyTemperature > -1e8f && m_temp[i] < m.solidifyTemperature;
    };

    for (int sub = 0; sub < substeps; ++sub) {
        // 1. Predict.
        for (size_t i = 0; i < n; ++i) {
            // Solidified particles (crust) aren't pinned — they'd hang in
            // the air when what's under them melts — they're heavily damped:
            // they sag and settle but no longer flow.
            if (frozen(i)) m_vel[i] *= 0.92f;
            m_vel[i] += m_params.gravity * h;
            m_pred[i] = m_pos[i] + m_vel[i] * h;
            collide(m_pred[i], m_pos[i]);
        }
        buildGrid(m_pred);
        // Neighbour lists once per substep (slightly generous radius),
        // reused by every solver pass below — the grid search runs 1x per
        // substep instead of 7x. Distances are re-checked on use.
        m_nbrStart.resize(n + 1);
        m_nbr.clear();
        for (size_t i = 0; i < n; ++i) {
            m_nbrStart[i] = static_cast<uint32_t>(m_nbr.size());
            forNeighbours(i, m_pred, [&](uint32_t j, const glm::vec3&, float) { m_nbr.push_back(j); });
        }
        m_nbrStart[n] = static_cast<uint32_t>(m_nbr.size());
        auto eachNeighbour = [&](size_t i, auto&& f) {
            for (uint32_t k = m_nbrStart[i]; k < m_nbrStart[i + 1]; ++k) {
                uint32_t j = m_nbr[k];
                glm::vec3 r = m_pred[i] - m_pred[j];
                float dd = glm::dot(r, r);
                if (dd < h2) f(j, r, dd);
            }
        };

        // 2. Density constraints (unilateral: only push apart when
        // compressed, so the free surface doesn't clump — a common game
        // simplification that also removes the need for s_corr).
        for (int it = 0; it < m_params.iterations; ++it) {
            for (size_t i = 0; i < n; ++i) {
                float rho = m_mass * m_poly6 * h2 * h2 * h2; // self contribution
                glm::vec3 gradI(0.0f);
                float sumGrad2 = 0.0f;
                eachNeighbour(i, [&](uint32_t, const glm::vec3& r, float dd) {
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
                m_density[i] = rho;
                float c = std::max(rho * invRest - 1.0f, 0.0f);
                m_lambda[i] = -c / (sumGrad2 + relaxation);
            }
            for (size_t i = 0; i < n; ++i) {
                glm::vec3 dp(0.0f);
                eachNeighbour(i, [&](uint32_t j, const glm::vec3& r, float dd) {
                    float len = std::sqrt(dd);
                    if (len < 1e-9f) return;
                    float x = m_h - len;
                    dp += (m_lambda[i] + m_lambda[j]) * m_spikyGrad * x * x * (r / len);
                });
                m_delta[i] = dp * (m_mass * invRest);
            }
            for (size_t i = 0; i < n; ++i) {
                glm::vec3 before = m_pred[i];
                // Cap one iteration's correction at half a particle: deep
                // overlaps resolve over a few iterations instead of launching
                // the particle (the velocity comes from this displacement).
                glm::vec3 dp = m_delta[i];
                float len = glm::length(dp);
                const float maxDp = m_params.radius * 0.5f;
                if (len > maxDp) dp *= maxDp / len;
                m_pred[i] += dp;
                collide(m_pred[i], before);
            }
        }

        // 3. Velocities, XSPH viscosity and heat.
        const float invH = 1.0f / h;
        m_scratchVel.resize(n);
        m_scratchTemp.resize(n);

        for (size_t i = 0; i < n; ++i) {
            m_vel[i] = (m_pred[i] - m_pos[i]) * invH;
            float sp = glm::length(m_vel[i]);
            if (sp > m_params.maxSpeed) m_vel[i] *= m_params.maxSpeed / sp;
        }
        for (size_t i = 0; i < n; ++i) {
            glm::vec3 dv(0.0f);
            float wsum = 0.0f, tsum = 0.0f;
            eachNeighbour(i, [&](uint32_t j, const glm::vec3&, float dd) {
                float w = h2 - dd;
                w = w * w * w;
                dv += (m_vel[j] - m_vel[i]) * w;
                tsum += (m_temp[j] - m_temp[i]) * w;
                wsum += w;
            });
            const FluidMaterial& mat = mats[m_material[i] & 7];
            const float coldEnd = mat.solidifyTemperature > -1e8f ? mat.solidifyTemperature : m_params.ambientTemperature;
            const float span = std::max(1.0f, mat.hotTemperature - coldEnd);
            float hot = std::clamp((m_temp[i] - coldEnd) / span, 0.0f, 1.0f);
            float visc = frozen(i) ? 0.35f : mat.viscosityCold + (mat.viscosityHot - mat.viscosityCold) * hot; // solid: moves with its neighbours
            m_scratchVel[i] = wsum > 0.0f ? m_vel[i] + dv * (visc / wsum) : m_vel[i];
            float t = m_temp[i];
            if (wsum > 0.0f) t += tsum / wsum * std::min(1.0f, m_params.heatDiffusion * h);
            t -= (t - m_params.ambientTemperature) * std::min(1.0f, m_params.coolingRate * h);
            m_scratchTemp[i] = t;
        }
        for (size_t i = 0; i < n; ++i) {
            m_vel[i] = m_scratchVel[i];
            m_temp[i] = m_scratchTemp[i];
            m_pos[i] = m_pred[i];
        }
    }
}

} // namespace kke
