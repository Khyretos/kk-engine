#include "kke/GpuSkin.h"

namespace kke {

// packSkinBind: a mesh's bind vertices for the shader (kSkinBindVec4s per vertex).
std::vector<glm::vec4> packSkinBind(const std::vector<ModelVertex>& vertices) {
    std::vector<glm::vec4> out;
    out.reserve(vertices.size() * kSkinBindVec4s);
    for (const ModelVertex& v : vertices) {
        out.push_back(glm::vec4(v.position, v.uv.x));
        out.push_back(glm::vec4(v.normal, v.uv.y));
        out.push_back(v.weights);
        out.push_back(glm::vec4(v.joints));
    }
    return out;
}

// packSkinData: the skin matrices (four columns each), then the jiggle zones.
void packSkinData(const std::vector<glm::mat4>& skin, const std::vector<SkinJiggleOffset>& zones, std::vector<glm::vec4>& out) {
    out.clear();
    out.reserve(skin.size() * kSkinVec4sPerBone + zones.size() * kSkinVec4sPerZone);
    for (const glm::mat4& m : skin)
        for (int c = 0; c < 4; ++c) out.push_back(m[c]);
    for (const SkinJiggleOffset& z : zones) {
        out.push_back(glm::vec4(z.center, z.radius));
        out.push_back(glm::vec4(z.offset, 0.0f));
    }
}

// skinPackedVertex: shaders/skin.comp line for line (keep the two in step): skinVertex's
// weighted sum of skin matrices, then the jiggle zones summed at the skinned position.
void skinPackedVertex(const glm::vec4* bind, const glm::vec4* data, const SkinPush& pc, uint32_t v, float* out) {
    const glm::vec4* b = bind + static_cast<size_t>(v) * kSkinBindVec4s;
    const glm::vec3 bindPos(b[0]);
    const glm::vec3 bindNormal(b[1]);
    glm::vec3 p(0.0f), n(0.0f);
    // Up to four bones; a zero weight or a joint past the skeleton is skipped.
    for (int w = 0; w < 4; ++w) {
        const float weight = b[2][w];
        const uint32_t j = static_cast<uint32_t>(b[3][w]);
        if (weight <= 0.0f || j >= pc.boneCount) continue;
        const glm::vec4* c = data + static_cast<size_t>(j) * kSkinVec4sPerBone;
        const glm::mat4 m(c[0], c[1], c[2], c[3]);
        p += weight * glm::vec3(m * glm::vec4(bindPos, 1.0f));
        n += weight * (glm::mat3(m) * bindNormal);
    }
    // No weights at all: keep the bind normal.
    const float len = glm::length(n);
    n = len > 1e-8f ? n / len : bindNormal;
    // Jiggle zones (skinJiggleDisplacement): summed first, then added.
    glm::vec3 d(0.0f);
    const glm::vec4* zones = data + static_cast<size_t>(pc.boneCount) * kSkinVec4sPerBone;
    for (uint32_t z = 0; z < pc.zoneCount; ++z) {
        const glm::vec4 centre = zones[z * kSkinVec4sPerZone];
        const glm::vec3 e = p - glm::vec3(centre);
        const float r2 = centre.w * centre.w;
        const float l2 = glm::dot(e, e);
        if (l2 >= r2) continue;
        const float x = 1.0f - l2 / r2;
        d += glm::vec3(zones[z * kSkinVec4sPerZone + 1]) * (x * x);
    }
    p += d;
    out[0] = p.x;
    out[1] = p.y;
    out[2] = p.z;
    out[3] = bindPos.x;
    out[4] = bindPos.y;
    out[5] = bindPos.z;
    out[6] = n.x;
    out[7] = n.y;
    out[8] = n.z;
    out[9] = b[0].w;
    out[10] = b[1].w;
}

} // namespace kke
