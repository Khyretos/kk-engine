#include "kke/Picking.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace kke {

glm::mat4 engineProjection(float fovDegrees, float aspect, float nearPlane, float farPlane) {
    glm::mat4 proj = glm::perspective(glm::radians(fovDegrees), aspect, nearPlane, farPlane);
    proj[1][1] *= -1.0f;
    return proj;
}

Ray screenToRay(const glm::vec2& pixel, const glm::vec2& viewport, const glm::mat4& view, const glm::mat4& proj) {
    // Pixel -> NDC. Vulkan NDC has y down, matching pixel rows, so no flip
    // here: the flip lives in `proj` and is undone by its inverse.
    glm::vec2 ndc = (pixel / viewport) * 2.0f - 1.0f;
    glm::mat4 inv = glm::inverse(proj * view);
    glm::vec4 nearP = inv * glm::vec4(ndc, 0.0f, 1.0f);
    glm::vec4 farP = inv * glm::vec4(ndc, 1.0f, 1.0f);
    nearP /= nearP.w;
    farP /= farP.w;
    Ray r;
    r.origin = glm::vec3(nearP);
    r.direction = glm::normalize(glm::vec3(farP - nearP));
    return r;
}

float rayPlaneY(const Ray& ray, float planeY) {
    if (std::abs(ray.direction.y) < 1e-8f) return -1.0f;
    return (planeY - ray.origin.y) / ray.direction.y;
}

float rayAabb(const Ray& ray, const glm::vec3& bmin, const glm::vec3& bmax) {
    float tmin = 0.0f, tmax = std::numeric_limits<float>::max();
    for (int a = 0; a < 3; ++a) {
        if (std::abs(ray.direction[a]) < 1e-12f) {
            if (ray.origin[a] < bmin[a] || ray.origin[a] > bmax[a]) return -1.0f;
            continue;
        }
        float inv = 1.0f / ray.direction[a];
        float t0 = (bmin[a] - ray.origin[a]) * inv, t1 = (bmax[a] - ray.origin[a]) * inv;
        if (t0 > t1) std::swap(t0, t1);
        tmin = std::max(tmin, t0);
        tmax = std::min(tmax, t1);
        if (tmin > tmax) return -1.0f;
    }
    return tmin;
}

void transformAabb(const glm::vec3& lmin, const glm::vec3& lmax, const glm::mat4& m, glm::vec3& omin, glm::vec3& omax) {
    omin = omax = glm::vec3(m[3]);
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            float a = m[j][i] * lmin[j], b = m[j][i] * lmax[j];
            omin[i] += std::min(a, b);
            omax[i] += std::max(a, b);
        }
    }
}

float snapTo(float value, float step) {
    return step > 0.0f ? std::round(value / step) * step : value;
}

Frustum Frustum::fromViewProj(const glm::mat4& m) {
    // Rows of the matrix (glm is column-major: m[col][row]).
    auto row = [&](int r) { return glm::vec4(m[0][r], m[1][r], m[2][r], m[3][r]); };
    Frustum f;
    f.planes[0] = row(3) + row(0); // left
    f.planes[1] = row(3) - row(0); // right
    f.planes[2] = row(3) + row(1); // bottom (top with the Vulkan y flip - doesn't matter, both are kept)
    f.planes[3] = row(3) - row(1);
    f.planes[4] = row(3) + row(2); // near (for -w..w depth; for 0..w this is a little behind it: still safe)
    f.planes[5] = row(3) - row(2); // far
    for (glm::vec4& p : f.planes) {
        float len = glm::length(glm::vec3(p));
        if (len > 0.0f) p /= len;
    }
    return f;
}

bool Frustum::intersectsAabb(const glm::vec3& mn, const glm::vec3& mx) const {
    for (const glm::vec4& p : planes) {
        // The box corner furthest along the plane normal.
        glm::vec3 v(p.x >= 0.0f ? mx.x : mn.x, p.y >= 0.0f ? mx.y : mn.y, p.z >= 0.0f ? mx.z : mn.z);
        if (glm::dot(glm::vec3(p), v) + p.w < 0.0f) return false;
    }
    return true;
}

} // namespace kke
