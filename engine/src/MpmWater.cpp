#include "kke/MpmWater.h"

#include "kke/MpmWaterGpu.h"
#include "kke/TaskPool.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace kke {

namespace {

constexpr size_t kGrain = 512;  // particles per task, at least
constexpr int kSlab = 4;        // z cells per slab: a slab's particles write 6 node layers, so slabs two apart never meet
// Bodies hold the grid half a cell out from their surface, as the walls
// do: water stops there instead of being pushed out of them every
// substep, which left a layer of stretched, pressureless water under a
// floating body (it got a sixth of its buoyancy).
constexpr float kBodyBand = 0.5f;

// Quadratic B-spline weights of the three nodes around a particle on one
// axis; fx = its position in cells from the first node (0.5 .. 1.5).
inline glm::vec3 weightsOf(float fx) {
    const float a = 1.5f - fx, b = fx - 1.0f, c = fx - 0.5f;
    return glm::vec3(0.5f * a * a, 0.75f - b * b, 0.5f * c * c);
}

// Signed distance from p to a body (negative inside) and the outward
// normal there. shaders/mpm_common.glsl has the same.
inline float bodyDistance(const glm::vec3& p, const glm::vec3& center, const glm::quat& rotation, const glm::vec3& half, bool sphere,
                          glm::vec3& normal) {
    const glm::vec3 r = p - center;
    if (sphere) {
        const float len = glm::length(r);
        normal = len > 1e-6f ? r / len : glm::vec3(0.0f, 1.0f, 0.0f);
        return len - half.x;
    }
    const glm::vec3 local = glm::conjugate(rotation) * r;
    const glm::vec3 d = glm::abs(local) - half;
    const glm::vec3 out = glm::max(d, glm::vec3(0.0f));
    const float outside = glm::length(out);
    glm::vec3 n;
    if (outside > 0.0f) {
        n = out / outside * glm::sign(local);
    } else {
        // Inside: out through the nearest face.
        const int axis = d.x > d.y ? (d.x > d.z ? 0 : 2) : (d.y > d.z ? 1 : 2);
        n = glm::vec3(0.0f);
        n[axis] = local[axis] < 0.0f ? -1.0f : 1.0f;
    }
    normal = rotation * n;
    return outside + std::min(std::max(d.x, std::max(d.y, d.z)), 0.0f);
}

float rand01(uint32_t& s) {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return (s >> 8) * (1.0f / 16777216.0f);
}

} // namespace

MpmWater::MpmWater(const Params& params) : m_params(params) {
    if (!(params.cellSize > 0.0f)) throw std::invalid_argument("MpmWater: cellSize must be > 0");
    const glm::vec3 size = params.boundsMax - params.boundsMin;
    if (!(size.x > 0.0f && size.y > 0.0f && size.z > 0.0f)) throw std::invalid_argument("MpmWater: boundsMax must be above boundsMin");
    m_dx = params.cellSize;
    m_invDx = 1.0f / m_dx;
    // Two cells of grid outside every wall: a particle at the wall still
    // has all its 3 x 3 x 3 nodes, and the outer two hold the wall.
    m_origin = params.boundsMin - glm::vec3(2.0f * m_dx);
    m_dims = glm::ivec3(glm::ceil(size * m_invDx)) + glm::ivec3(5);
    m_nodes = size_t(m_dims.x) * size_t(m_dims.y) * size_t(m_dims.z);
    if (m_nodes > params.maxNodes) throw std::invalid_argument("MpmWater: the tank needs more grid nodes than maxNodes (bigger cells or a smaller tank)");
    m_particleMass = params.density * m_dx * m_dx * m_dx / 8.0f;
    m_grid.assign(m_nodes, glm::vec4(0.0f));
}

MpmWater::~MpmWater() = default;

void MpmWater::add(const glm::vec3& position, const glm::vec3& velocity) {
    m_x.push_back(glm::clamp(position, m_params.boundsMin, m_params.boundsMax));
    m_v.push_back(velocity);
    m_C.push_back(glm::mat3(0.0f));
    m_J.push_back(1.0f);
}

size_t MpmWater::fill(const glm::vec3& min, const glm::vec3& max, const glm::vec3& velocity) {
    const glm::vec3 lo = glm::max(min, m_params.boundsMin), hi = glm::min(max, m_params.boundsMax);
    const float s = m_dx * 0.5f;
    const glm::ivec3 n = glm::ivec3(glm::floor((hi - lo) / s));
    if (n.x <= 0 || n.y <= 0 || n.z <= 0) return 0;
    const size_t count = size_t(n.x) * size_t(n.y) * size_t(n.z);
    m_x.reserve(m_x.size() + count);
    for (int z = 0; z < n.z; ++z)
        for (int y = 0; y < n.y; ++y)
            for (int x = 0; x < n.x; ++x) {
                const glm::vec3 jitter(rand01(m_rng) - 0.5f, rand01(m_rng) - 0.5f, rand01(m_rng) - 0.5f);
                add(lo + (glm::vec3(x, y, z) + 0.5f + jitter * 0.3f) * s, velocity);
            }
    return count;
}

void MpmWater::clear() {
    m_x.clear();
    m_v.clear();
    m_C.clear();
    m_J.clear();
    m_gpuValid = 0;
    m_gpuOwns = false;
    m_lastMaxSpeed = 0.0f;
}

void MpmWater::removeIf(const std::function<bool(size_t)>& pred) {
    if (m_gpuOwns && m_gpu) m_gpu->download(*this);
    m_gpuOwns = false;
    size_t w = 0;
    for (size_t i = 0; i < m_x.size(); ++i) {
        if (pred(i)) continue;
        m_x[w] = m_x[i];
        m_v[w] = m_v[i];
        m_C[w] = m_C[i];
        m_J[w] = m_J[i];
        ++w;
    }
    m_x.resize(w);
    m_v.resize(w);
    m_C.resize(w);
    m_J.resize(w);
    m_gpuValid = 0;
}

void MpmWater::setGpu(std::shared_ptr<MpmWaterGpu> gpu) {
    if (gpu == m_gpu) return;
    if (m_gpuOwns && m_gpu) m_gpu->download(*this);
    m_gpuOwns = false;
    m_gpuValid = 0;
    m_gpu = std::move(gpu);
}

int MpmWater::substepsFor(float dt) const {
    if (!(dt > 0.0f)) return 0;
    const float speed = std::max(m_lastMaxSpeed, 1.0f) + m_params.soundSpeed;
    const float longest = m_params.cfl * m_dx / speed;
    const int n = static_cast<int>(std::ceil(dt / longest));
    return std::clamp(n, 1, std::max(1, m_params.maxSubsteps));
}

void MpmWater::beginStep() {
    const size_t n = std::min(m_bodies.size(), kMaxBodies);
    m_proxies.resize(n);
    for (size_t i = 0; i < n; ++i) {
        const Body& b = m_bodies[i];
        Proxy& p = m_proxies[i];
        p.position = b.position;
        p.rotation = glm::normalize(b.rotation);
        p.halfExtents = glm::max(b.halfExtents, glm::vec3(1e-4f));
        p.velocity = b.velocity;
        p.angularVelocity = b.angularVelocity;
        p.sphere = b.sphere;
        p.friction = std::clamp(b.friction, 0.0f, 1.0f);
        p.invMass = b.mass > 0.0f ? 1.0f / b.mass : 0.0f;
        if (b.mass > 0.0f) {
            const glm::vec3 h = p.halfExtents;
            const glm::vec3 inertia = b.sphere ? glm::vec3(0.4f * b.mass * h.x * h.x)
                                               : b.mass / 3.0f * glm::vec3(h.y * h.y + h.z * h.z, h.x * h.x + h.z * h.z, h.x * h.x + h.y * h.y);
            p.invInertia = 1.0f / glm::max(inertia, glm::vec3(1e-9f));
        } else {
            p.invInertia = glm::vec3(0.0f);
        }
        p.dv = p.dw = glm::vec3(0.0f);
    }
}

void MpmWater::endStep() {
    for (size_t i = 0; i < m_bodies.size(); ++i) {
        m_bodies[i].deltaVelocity = i < m_proxies.size() ? m_proxies[i].dv : glm::vec3(0.0f);
        m_bodies[i].deltaAngularVelocity = i < m_proxies.size() ? m_proxies[i].dw : glm::vec3(0.0f);
    }
}

void MpmWater::step(float dt) {
    const auto t0 = std::chrono::steady_clock::now();
    const int n = substepsFor(dt);
    beginStep();
    if (n > 0 && !m_x.empty()) {
        if (m_gpu) {
            if (m_gpu->step(*this, dt / float(n), n)) {
                ++m_stats.gpuSteps;
                m_gpuOwns = true;
            } else {
                ++m_stats.gpuLate; // the water waits a step
            }
        } else {
            stepCpu(dt / float(n), n);
        }
    }
    endStep();
    m_stats.substeps = n;
    m_stats.maxSpeed = m_lastMaxSpeed;
    m_stats.stepMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

void MpmWater::stepOnCpu(float dt) {
    const auto t0 = std::chrono::steady_clock::now();
    const int n = substepsFor(dt);
    beginStep();
    if (n > 0 && !m_x.empty()) stepCpu(dt / float(n), n);
    endStep();
    m_stats.substeps = n;
    m_stats.maxSpeed = m_lastMaxSpeed;
    m_stats.stepMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

void MpmWater::stepCpu(float dt, int substeps) {
    if (m_gpuOwns && m_gpu) m_gpu->download(*this);
    m_gpuOwns = false;
    m_gpuValid = 0; // the GPU takes this step's result at its next step
    for (int s = 0; s < substeps; ++s) substep(dt);
}

void MpmWater::integrateBodies(float dt, const glm::vec3* impulse, const glm::vec3* angularImpulse) {
    for (size_t b = 0; b < m_proxies.size(); ++b) {
        Proxy& p = m_proxies[b];
        if (p.invMass > 0.0f) {
            const glm::vec3 dv = impulse[b] * p.invMass;
            const glm::mat3 R = glm::mat3_cast(p.rotation);
            const glm::vec3 dw = R * (p.invInertia * (glm::transpose(R) * angularImpulse[b]));
            p.velocity += dv + m_params.gravity * dt;
            p.angularVelocity += dw;
            p.dv += dv;
            p.dw += dw;
        }
        p.position += p.velocity * dt;
        const glm::quat spin(0.0f, p.angularVelocity.x, p.angularVelocity.y, p.angularVelocity.z);
        p.rotation = glm::normalize(p.rotation + (spin * p.rotation) * (0.5f * dt));
    }
}

void MpmWater::substep(float dt) {
    const size_t n = m_x.size();
    const float dx = m_dx, invDx = m_invDx;
    m_base.resize(n);
    m_affine.resize(n);
    m_speed.resize(n);
    m_hitBody.resize(n);
    m_hitImpulse.resize(n);
    m_hitAngular.resize(n);

    // 1. Each particle's first node, and the matrix it hands the grid:
    //    pressure from its volume (weakly compressible: p / rho =
    //    c^2 (1 - J)), viscosity from its velocity gradient, and the
    //    gradient itself (APIC). MLS-MPM folds them into one matrix.
    const float k = dt * 4.0f * invDx * invDx;
    const float c2 = m_params.soundSpeed * m_params.soundSpeed;
    const float nu = std::min(m_params.viscosity, 0.1f * dx * dx / dt); // more is unstable at this substep
    const float tension = std::max(0.0f, m_params.tension);
    TaskPool::forRanges(m_pool, n, kGrain, [&](size_t begin, size_t end) {
        for (size_t p = begin; p < end; ++p) {
            const glm::vec3 local = (m_x[p] - m_origin) * invDx;
            m_base[p] = glm::ivec3(glm::floor(local - 0.5f));
            const glm::mat3& C = m_C[p];
            const float pressure = c2 * std::max(1.0f - m_J[p], -tension);
            m_affine[p] = k * (glm::mat3(pressure) - nu * (C + glm::transpose(C))) + C;
        }
    });

    // 2. The nodes it touches, and the particles by z slab.
    glm::ivec3 lo(INT32_MAX), hi(INT32_MIN);
    for (size_t p = 0; p < n; ++p) {
        lo = glm::min(lo, m_base[p]);
        hi = glm::max(hi, m_base[p]);
    }
    hi += glm::ivec3(2);
    lo = glm::max(lo, glm::ivec3(0));
    hi = glm::min(hi, m_dims - 1);
    m_lo = lo;
    m_hi = hi;
    const int slabs = (hi.z - lo.z) / kSlab + 1;
    m_slabStart.assign(size_t(slabs) + 1, 0);
    for (size_t p = 0; p < n; ++p) ++m_slabStart[size_t((m_base[p].z - lo.z) / kSlab) + 1];
    for (int s = 0; s < slabs; ++s) m_slabStart[size_t(s) + 1] += m_slabStart[size_t(s)];
    m_order.resize(n);
    {
        std::vector<uint32_t>& cursor = m_slabCursor;
        cursor.assign(m_slabStart.begin(), m_slabStart.end() - 1);
        for (size_t p = 0; p < n; ++p) m_order[cursor[size_t((m_base[p].z - lo.z) / kSlab)]++] = static_cast<uint32_t>(p);
    }
    const int layers = hi.z - lo.z + 1;
    const size_t rowLen = size_t(hi.x - lo.x + 1);
    auto node = [&](int x, int y, int z) { return (size_t(z) * size_t(m_dims.y) + size_t(y)) * size_t(m_dims.x) + size_t(x); };

    // 3. Clear the nodes this substep uses.
    TaskPool::forRanges(m_pool, size_t(layers), 1, [&](size_t begin, size_t end) {
        for (size_t l = begin; l < end; ++l)
            for (int y = lo.y; y <= hi.y; ++y) std::memset(&m_grid[node(lo.x, y, lo.z + int(l))], 0, rowLen * sizeof(glm::vec4));
    });

    // 4. Particles -> grid: even slabs, then odd ones. Slabs of one parity
    //    write node layers far apart, so they run side by side, and every
    //    node sums its particles in the same order on any thread count.
    auto scatterSlab = [&](size_t s) {
        for (uint32_t i = m_slabStart[s]; i < m_slabStart[s + 1]; ++i) {
            const uint32_t p = m_order[i];
            const glm::ivec3 base = m_base[p];
            const glm::vec3 fx = (m_x[p] - m_origin) * invDx - glm::vec3(base);
            const glm::vec3 w[3] = { weightsOf(fx.x), weightsOf(fx.y), weightsOf(fx.z) };
            const glm::vec3 v = m_v[p];
            const glm::mat3& A = m_affine[p];
            for (int c = 0; c < 3; ++c)
                for (int b = 0; b < 3; ++b)
                    for (int a = 0; a < 3; ++a) {
                        const float weight = w[0][a] * w[1][b] * w[2][c];
                        const glm::vec3 dpos = (glm::vec3(a, b, c) - fx) * dx;
                        m_grid[node(base.x + a, base.y + b, base.z + c)] += glm::vec4(weight * (v + A * dpos), weight);
                    }
        }
    };
    for (int parity = 0; parity < 2; ++parity) {
        const size_t count = size_t((slabs - parity + 1) / 2);
        if (count == 0) continue;
        if (m_pool && m_pool->workers() > 1 && count > 1) m_pool->run(count, [&](size_t i) { scatterSlab(size_t(parity) + 2 * i); });
        else
            for (size_t i = 0; i < count; ++i) scatterSlab(size_t(parity) + 2 * i);
    }

    // 5. Grid velocities: momentum / mass, gravity, then bodies and walls
    //    stop what moves into them. What the bodies stop, they receive,
    //    summed per layer and then layer by layer (same order on any
    //    thread count).
    const size_t bodies = m_proxies.size();
    m_layerImpulse.assign(size_t(layers) * bodies * 2, glm::vec3(0.0f));
    const glm::vec3 g = m_params.gravity * dt;
    const glm::vec3 wallLo = m_params.boundsMin, wallHi = m_params.boundsMax;
    // Nodes on the wall and past it hold it: particles then stop half a
    // cell short of it instead of piling up flat against it.
    const float wallBand = 0.5f * dx;
    const float wallKeep = 1.0f - std::clamp(m_params.wallFriction, 0.0f, 1.0f);
    const float pm = m_particleMass;
    TaskPool::forRanges(m_pool, size_t(layers), 1, [&](size_t begin, size_t end) {
        for (size_t l = begin; l < end; ++l) {
            const int z = lo.z + int(l);
            glm::vec3* imp = bodies ? &m_layerImpulse[l * bodies * 2] : nullptr;
            for (int y = lo.y; y <= hi.y; ++y)
                for (int x = lo.x; x <= hi.x; ++x) {
                    glm::vec4& gn = m_grid[node(x, y, z)];
                    if (gn.w <= 0.0f) {
                        gn = glm::vec4(0.0f);
                        continue;
                    }
                    glm::vec3 v = glm::vec3(gn) / gn.w + g;
                    const glm::vec3 pos = m_origin + glm::vec3(x, y, z) * dx;
                    for (size_t b = 0; b < bodies; ++b) {
                        const Proxy& pr = m_proxies[b];
                        glm::vec3 nrm;
                        if (bodyDistance(pos, pr.position, pr.rotation, pr.halfExtents, pr.sphere, nrm) >= kBodyBand * dx) continue;
                        const glm::vec3 r = pos - pr.position;
                        const glm::vec3 vb = pr.velocity + glm::cross(pr.angularVelocity, r);
                        const glm::vec3 vr = v - vb;
                        const float vn = glm::dot(vr, nrm);
                        if (vn >= 0.0f) continue; // leaving the body: free
                        glm::vec3 vt = vr - vn * nrm;
                        const float lt = glm::length(vt);
                        if (pr.friction > 0.0f && lt > 0.0f) vt *= std::max(0.0f, 1.0f + pr.friction * vn / lt);
                        const glm::vec3 vnew = vb + vt;
                        if (pr.invMass > 0.0f) {
                            const glm::vec3 J = (gn.w * pm) * (v - vnew);
                            imp[b * 2] += J;
                            imp[b * 2 + 1] += glm::cross(r, J);
                        }
                        v = vnew;
                    }
                    // Walls: no flow out through them (slip, or friction along them).
                    bool hit[3] = { false, false, false };
                    for (int a = 0; a < 3; ++a) {
                        if ((pos[a] < wallLo[a] + wallBand && v[a] < 0.0f) || (pos[a] > wallHi[a] - wallBand && v[a] > 0.0f)) {
                            v[a] = 0.0f;
                            hit[a] = true;
                        }
                    }
                    if (wallKeep < 1.0f && (hit[0] || hit[1] || hit[2]))
                        for (int a = 0; a < 3; ++a)
                            if (!hit[a]) v[a] *= wallKeep;
                    gn = glm::vec4(v, gn.w);
                }
        }
    });
    if (bodies) {
        m_bodyImpulse.assign(bodies * 2, glm::vec3(0.0f));
        for (int l = 0; l < layers; ++l)
            for (size_t i = 0; i < bodies * 2; ++i) m_bodyImpulse[i] += m_layerImpulse[size_t(l) * bodies * 2 + i];
        std::vector<glm::vec3> lin(bodies), ang(bodies);
        for (size_t b = 0; b < bodies; ++b) {
            lin[b] = m_bodyImpulse[b * 2];
            ang[b] = m_bodyImpulse[b * 2 + 1];
        }
        integrateBodies(dt, lin.data(), ang.data());
    }

    // 6. Grid -> particles: velocity and its gradient (APIC), volume, move;
    //    then out of bodies and inside the walls.
    // J follows the particle's real volume, stretched or squeezed (spray far
    // apart, then landing, comes back to 1): only the pull it gives is capped.
    const float jMin = 0.6f, jMax = 4.0f;
    const float maxSpeed = m_params.maxSpeed;
    TaskPool::forRanges(m_pool, n, kGrain, [&](size_t begin, size_t end) {
        for (size_t p = begin; p < end; ++p) {
            const glm::ivec3 base = m_base[p];
            const glm::vec3 fx = (m_x[p] - m_origin) * invDx - glm::vec3(base);
            const glm::vec3 w[3] = { weightsOf(fx.x), weightsOf(fx.y), weightsOf(fx.z) };
            glm::vec3 v(0.0f);
            glm::mat3 B(0.0f);
            for (int c = 0; c < 3; ++c)
                for (int b = 0; b < 3; ++b)
                    for (int a = 0; a < 3; ++a) {
                        const float weight = w[0][a] * w[1][b] * w[2][c];
                        const glm::vec3 gv = glm::vec3(m_grid[node(base.x + a, base.y + b, base.z + c)]);
                        const glm::vec3 dpos = (glm::vec3(a, b, c) - fx) * dx;
                        v += weight * gv;
                        B += weight * glm::outerProduct(gv, dpos);
                    }
            const glm::mat3 C = (4.0f * invDx * invDx) * B;
            float J = m_J[p] * (1.0f + dt * (C[0][0] + C[1][1] + C[2][2]));
            J = std::clamp(J, jMin, jMax);
            glm::vec3 x = m_x[p] + dt * v;
            // A particle the grid still carries into a body is put back on
            // its surface, and the body gets the momentum it stops (most of
            // the pressure a body feels arrives this way).
            int32_t hit = -1;
            glm::vec3 hitJ(0.0f), hitL(0.0f);
            for (size_t b = 0; b < bodies; ++b) {
                const Proxy& pr = m_proxies[b];
                glm::vec3 nrm;
                const float d = bodyDistance(x, pr.position, pr.rotation, pr.halfExtents, pr.sphere, nrm);
                if (d >= 0.0f) continue;
                x -= nrm * d;
                const glm::vec3 vb = pr.velocity + glm::cross(pr.angularVelocity, x - pr.position);
                const float vn = glm::dot(v - vb, nrm);
                if (vn < 0.0f) {
                    v -= vn * nrm;
                    if (pr.invMass > 0.0f) {
                        hit = static_cast<int32_t>(b);
                        hitJ = (pm * vn) * nrm;
                        hitL = glm::cross(x - pr.position, hitJ);
                    }
                }
            }
            m_hitBody[p] = hit;
            m_hitImpulse[p] = hitJ;
            m_hitAngular[p] = hitL;
            for (int a = 0; a < 3; ++a) {
                if (x[a] < wallLo[a]) {
                    x[a] = wallLo[a];
                    v[a] = std::max(v[a], 0.0f);
                } else if (x[a] > wallHi[a]) {
                    x[a] = wallHi[a];
                    v[a] = std::min(v[a], 0.0f);
                }
            }
            float speed = glm::length(v);
            if (speed > maxSpeed) {
                v *= maxSpeed / speed;
                speed = maxSpeed;
            }
            m_x[p] = x;
            m_v[p] = v;
            m_C[p] = C;
            m_J[p] = J;
            m_speed[p] = speed;
        }
    });
    float fastest = 0.0f;
    for (size_t p = 0; p < n; ++p) fastest = std::max(fastest, m_speed[p]);
    if (bodies) {
        // What the particles stopped, in particle order (the same on any
        // thread count); the bodies take it now, without moving.
        std::vector<glm::vec3> lin(bodies, glm::vec3(0.0f)), ang(bodies, glm::vec3(0.0f));
        for (size_t p = 0; p < n; ++p) {
            if (m_hitBody[p] < 0) continue;
            lin[size_t(m_hitBody[p])] += m_hitImpulse[p];
            ang[size_t(m_hitBody[p])] += m_hitAngular[p];
        }
        integrateBodies(0.0f, lin.data(), ang.data());
    }
    m_lastMaxSpeed = fastest;
}

} // namespace kke
