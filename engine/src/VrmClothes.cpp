#include "kke/VrmClothes.h"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace kke {

namespace {

std::string upper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

bool has(const std::string& s, const char* part) { return s.find(part) != std::string::npos; }

glm::vec3 jointPos(const std::vector<glm::mat4>& world, int bone) {
    return bone >= 0 && static_cast<size_t>(bone) < world.size() ? glm::vec3(world[static_cast<size_t>(bone)][3]) : glm::vec3(0.0f);
}

// What the cuts need to know of one VRoid avatar, once.
void prepare(VrmAvatar& av, VrmStock& st) {
    st = VrmStock{};
    st.ready = true;
    st.meshes = av.model.meshes.size();
    st.materials = av.model.materials.size();
    for (size_t i = 0; i < av.model.meshes.size(); ++i) {
        const std::string mat = upper(av.model.materials[av.model.meshes[i].material].name);
        if (has(mat, "BODY") && has(mat, "SKIN")) st.skinMeshes.push_back(i);
        if (has(mat, "CLOTH")) {
            st.stockIndices[i] = av.model.meshes[i].indices;
            st.stockSlot[i] = has(mat, "TOPS") ? "top" : has(mat, "BOTTOMS") ? "bottom" : has(mat, "SHOES") ? "shoes" : "";
        }
    }
    if (st.skinMeshes.empty()) return;

    std::vector<std::vector<ModelVertex>> posed;
    av.pose(av.restLocals(), {}, posed);
    st.rest = posed[st.skinMeshes[0]];
    const std::vector<glm::mat4> world = computeRestPose(av.model);
    auto J = [&](const char* name) { return jointPos(world, av.bone(name)); };
    st.hips = av.bone("hips");
    st.leftLeg = av.bone("leftUpperLeg");
    st.rightLeg = av.bone("rightUpperLeg");
    st.leftSign = J("leftUpperLeg").x >= J("rightUpperLeg").x ? 1.0f : -1.0f;

    // Each bone's humanoid name, for the bones the file adds (twist, secondary
    // ones) the nearest humanoid ancestor's.
    std::vector<std::string> humanoid(av.model.bones.size());
    for (const auto& [name, bone] : av.humanoid)
        if (bone >= 0 && static_cast<size_t>(bone) < humanoid.size()) humanoid[static_cast<size_t>(bone)] = name;
    auto humanoidOf = [&](int bone) {
        while (bone >= 0 && humanoid[static_cast<size_t>(bone)].empty()) bone = av.model.bones[static_cast<size_t>(bone)].parent;
        return bone >= 0 ? humanoid[static_cast<size_t>(bone)] : std::string();
    };

    GarmentBody& body = st.body;
    const std::vector<ModelVertex>& vs = st.rest;
    body.positions.resize(vs.size());
    body.bone.resize(vs.size());
    body.armT.assign(vs.size(), 0.0f);
    body.armDist.assign(vs.size(), 1e9f);
    float sole = 1e9f;
    for (size_t v = 0; v < vs.size(); ++v) {
        body.positions[v] = vs[v].position;
        // The bone that moves it most.
        const ModelVertex& orig = av.model.meshes[st.skinMeshes[0]].vertices[v];
        uint32_t best = 0;
        for (uint32_t k = 1; k < 4; ++k)
            if (orig.weights[static_cast<int>(k)] > orig.weights[static_cast<int>(best)]) best = k;
        const std::string h = humanoidOf(static_cast<int>(orig.joints[static_cast<int>(best)]));
        const bool left = h.rfind("left", 0) == 0;
        const std::string side = left ? "L" : "R";
        auto ends = [&](const char* a, const char* b) { return std::pair{ J(a), J(b) }; };
        auto along = [&](const std::pair<glm::vec3, glm::vec3>& seg) {
            const glm::vec3 d = seg.second - seg.first;
            const float len2 = std::max(glm::dot(d, d), 1e-8f);
            return std::clamp(glm::dot(vs[v].position - seg.first, d) / len2, 0.0f, 1.0f);
        };
        const std::string arm = left ? "left" : "right";
        std::string name;
        if (h == "hips") name = "pelvis";
        else if (h == "spine" || h == "chest" || h == "upperChest") name = "spine03";
        else if (h == "neck") name = "neck01";
        else if (h == "head" || h.find("Eye") != std::string::npos || h == "jaw") name = "head";
        else if (h.find("Shoulder") != std::string::npos) name = "clavicle." + side;
        else if (h.find("UpperArm") != std::string::npos) {
            const float t = along(ends((arm + "UpperArm").c_str(), (arm + "LowerArm").c_str()));
            body.armT[v] = t;
            name = std::string(t < 0.5f ? "upperarm01." : "upperarm02.") + side;
        } else if (h.find("LowerArm") != std::string::npos) {
            const float t = along(ends((arm + "LowerArm").c_str(), (arm + "Hand").c_str()));
            name = std::string(t < 0.5f ? "lowerarm01." : "lowerarm02.") + side;
        } else if (h.find("Hand") != std::string::npos) name = "wrist." + side;
        else if (h.find("Proximal") != std::string::npos) name = "finger1-1." + side;
        else if (h.find("Intermediate") != std::string::npos) name = "finger1-2." + side;
        else if (h.find("Distal") != std::string::npos) name = "finger1-3." + side;
        else if (h.find("UpperLeg") != std::string::npos) name = "upperleg01." + side;
        else if (h.find("LowerLeg") != std::string::npos) name = "lowerleg01." + side;
        else if (h.find("Foot") != std::string::npos) name = "foot." + side;
        else if (h.find("Toes") != std::string::npos) name = "toe." + side;
        body.bone[v] = name;
        if (name.rfind("upperarm", 0) == 0 || name.rfind("lowerarm", 0) == 0 || name.rfind("wrist", 0) == 0 || name.rfind("finger", 0) == 0)
            body.armDist[v] = glm::distance(vs[v].position, J((arm + "Hand").c_str()));
        if (name.rfind("foot", 0) == 0 || name.rfind("toe", 0) == 0) sole = std::min(sole, vs[v].position.y);
        body.skin.push_back(static_cast<uint32_t>(v));
    }
    body.sole = sole < 1e8f ? sole : 0.0f;
    body.waist = J("spine").y;
    body.wristTop = J("leftHand").y;
    body.neckY = J("neck").y;
    body.neckZ = J("neck").z;
}

VrmToon toonFor(const glm::vec3& colour, const GarmentInfo& info) {
    VrmToon t;
    t.color = glm::vec4(colour, 1.0f);
    t.shadeColor = colour * 0.62f;
    t.shadeToony = 0.9f;
    t.shadeShift = -0.05f;
    t.receiveShadow = 1.0f;
    t.giIntensity = 0.1f;
    t.outlineWidth = 0.18f;
    t.outlineColor = colour * 0.3f;
    t.outlineFixedColor = true;
    // Satin and leather catch a little rim light, wool and cotton don't.
    if (info.fabric == "satin" || info.fabric == "silk" || info.fabric == "leather" || info.fabric == "rubber") {
        t.rimColor = glm::vec3(0.35f);
        t.rimFresnelPower = 4.0f;
        t.rimLightingMix = 0.7f;
    }
    return t;
}

// A flared skirt round the hips from the waist down `reach` of the leg.
ModelMesh skirtMesh(const VrmAvatar& av, const VrmStock& st, float reach, float offset) {
    ModelMesh out;
    out.skinned = true;
    if (st.hips < 0 || st.leftLeg < 0 || st.rightLeg < 0) return out;
    const GarmentBody& b = st.body;
    const float top = b.waist, hem = top - reach * (top - b.sole);
    // The hips' extent at the waist: the skin there, arms left out.
    float minX = 1e9f, maxX = -1e9f, minZ = 1e9f, maxZ = -1e9f;
    for (size_t v = 0; v < b.positions.size(); ++v) {
        const std::string& n = b.bone[v];
        if (n.rfind("upperarm", 0) == 0 || n.rfind("lowerarm", 0) == 0 || n.rfind("wrist", 0) == 0 || n.rfind("finger", 0) == 0) continue;
        if (std::abs(b.positions[v].y - top) > 0.025f) continue;
        minX = std::min(minX, b.positions[v].x);
        maxX = std::max(maxX, b.positions[v].x);
        minZ = std::min(minZ, b.positions[v].z);
        maxZ = std::max(maxZ, b.positions[v].z);
    }
    if (minX > maxX) return out;
    const float margin = offset + 0.014f;
    const float rx = 0.5f * (maxX - minX) + margin, rz = 0.5f * (maxZ - minZ) + margin;
    const float cx = 0.5f * (maxX + minX), cz = 0.5f * (maxZ + minZ);
    const float leftSide = st.leftSign;
    constexpr int kRows = 7, kSides = 32;
    const float flare = 0.12f + 0.55f * reach;
    for (int r = 0; r <= kRows; ++r) {
        const float f = static_cast<float>(r) / kRows;
        const float y = top + 0.012f - f * (top + 0.012f - hem);
        const float s = 1.0f + flare * std::pow(f, 1.3f);
        const float toLegs = std::clamp(f * 1.15f - 0.1f, 0.0f, 0.85f); // follows the thighs more the lower it hangs
        for (int k = 0; k < kSides; ++k) {
            const float a = glm::two_pi<float>() * static_cast<float>(k) / kSides;
            ModelVertex v;
            const float px = cx + rx * s * std::cos(a), pz = cz + rz * s * std::sin(a);
            v.position = glm::vec3(px, y, pz);
            v.normal = glm::normalize(glm::vec3(std::cos(a) / rx, 0.3f * flare, std::sin(a) / rz));
            v.uv = glm::vec2(static_cast<float>(k) / kSides, f);
            const float leftShare = std::clamp(0.5f + 0.5f * leftSide * (px - cx) / (0.4f * rx), 0.0f, 1.0f);
            v.joints = glm::uvec4(static_cast<uint32_t>(st.hips), static_cast<uint32_t>(st.leftLeg), static_cast<uint32_t>(st.rightLeg), 0u);
            v.weights = glm::vec4(1.0f - toLegs, toLegs * leftShare, toLegs * (1.0f - leftShare), 0.0f);
            out.vertices.push_back(v);
        }
    }
    for (int r = 0; r < kRows; ++r)
        for (int k = 0; k < kSides; ++k) {
            const uint32_t a = static_cast<uint32_t>(r * kSides + k), b2 = static_cast<uint32_t>(r * kSides + (k + 1) % kSides);
            const uint32_t c = a + kSides, d = b2 + kSides;
            out.indices.insert(out.indices.end(), { a, b2, c, b2, d, c }); // counter-clockwise seen from outside
        }
    return out;
}

} // namespace

size_t dressVrm(VrmAvatar& av, VrmStock& st, const std::vector<HumanLook::Worn>& clothes) {
    if (!st.ready) prepare(av, st);
    // Back to the avatar as it was loaded.
    av.model.meshes.resize(st.meshes);
    av.model.materials.resize(st.materials);
    av.toon.resize(st.materials);
    for (const auto& [mesh, indices] : st.stockIndices) av.model.meshes[mesh].indices = indices;
    if (st.skinMeshes.empty()) return 0;

    std::set<std::string> slots;
    for (const HumanLook::Worn& w : clothes)
        if (const GarmentInfo* g = findGarment(w.id)) slots.insert(g->slot);
    const bool dress = slots.count("dress") != 0;
    auto taken = [&](const std::string& stock) {
        if (stock == "top") return slots.count("top") || slots.count("outer") || dress;
        if (stock == "bottom") return slots.count("bottom") || dress;
        if (stock == "shoes") return slots.count("shoes") != 0;
        return false;
    };
    for (const auto& [mesh, slot] : st.stockSlot)
        if (!slot.empty() && taken(slot)) av.model.meshes[mesh].indices.clear();

    size_t added = 0;
    auto addMaterial = [&](const GarmentInfo& info, const glm::vec3& colour, bool doubleSided) {
        ModelMaterial mat;
        mat.name = "Garment_" + std::to_string(av.model.materials.size());
        mat.baseColor = colour;
        av.model.materials.push_back(mat);
        VrmToon t = toonFor(colour, info);
        t.doubleSided = doubleSided;
        av.toon.push_back(t);
        return static_cast<uint32_t>(av.model.materials.size() - 1);
    };
    for (const HumanLook::Worn& w : clothes) {
        const GarmentInfo* info = findGarment(w.id);
        if (!info) continue;
        for (const GarmentShape& shape : garmentPiecesOn(st.body, w.id)) {
            const std::set<uint32_t> in(shape.verts.begin(), shape.verts.end());
            ModelMesh out;
            std::map<uint32_t, uint32_t> index;
            for (size_t s : st.skinMeshes) {
                const ModelMesh& skin = av.model.meshes[s];
                out.skinned = skin.skinned;
                for (size_t t = 0; t + 2 < skin.indices.size(); t += 3) {
                    const uint32_t tri[3] = { skin.indices[t], skin.indices[t + 1], skin.indices[t + 2] };
                    if (!in.count(tri[0]) || !in.count(tri[1]) || !in.count(tri[2])) continue;
                    for (uint32_t v : tri) {
                        auto [it, fresh] = index.emplace(v, static_cast<uint32_t>(out.vertices.size()));
                        if (fresh) {
                            ModelVertex mv = skin.vertices[v];
                            mv.position += glm::normalize(mv.normal) * shape.offset;
                            out.vertices.push_back(mv);
                        }
                        out.indices.push_back(it->second);
                    }
                }
            }
            if (out.indices.empty()) continue;
            out.name = w.id;
            out.material = addMaterial(*info, w.colour, false);
            av.model.meshes.push_back(std::move(out));
            ++added;
        }
        float reach = 0.0f, offset = 0.0f;
        if (garmentSkirt(w.id, &reach, &offset)) {
            ModelMesh skirt = skirtMesh(av, st, reach, std::max(offset, info->slot == "outer" ? 0.012f : 0.006f));
            if (!skirt.indices.empty()) {
                skirt.name = w.id + " skirt";
                skirt.material = addMaterial(*info, w.colour, true);
                av.model.meshes.push_back(std::move(skirt));
                ++added;
            }
        }
    }
    return added;
}

} // namespace kke
