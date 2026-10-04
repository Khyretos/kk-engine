#include "kke/Outfit.h"

#include "kke/AnimRig.h"

#include <array>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

namespace kke {

BodyRegion regionOfBone(const std::string& boneName, bool sleeves) {
    const std::string n = canonicalBoneName(boneName);
    auto has = [&n](const char* part) { return n.find(part) != std::string::npos; };
    if (has("foot") || has("toe") || has("ball")) return BodyRegion::Shoes;
    if (has("hand") || has("finger") || has("thumb") || has("index") || has("middle") || has("ring") || has("pinky")) return BodyRegion::Skin;
    if (has("head") || has("neck") || has("eye") || has("jaw")) return BodyRegion::Skin;
    if (has("lowerarm") || has("forearm") || has("elbow")) return sleeves ? BodyRegion::Top : BodyRegion::Skin;
    if (has("thigh") || has("calf") || has("knee") || has("leg") || has("pelvis") || has("hip")) return BodyRegion::Bottom;
    return BodyRegion::Top; // spine, chest, clavicles, upper arms, the root
}

ModelData dressModel(const ModelData& model, const Outfit& outfit) {
    ModelData out = model;
    out.meshes.clear();
    out.materials.clear();
    const std::array<glm::vec3, 4> colours = { outfit.skin, outfit.top, outfit.bottom, outfit.shoes };
    const char* names[4] = { "Outfit_Skin", "Outfit_Top", "Outfit_Bottom", "Outfit_Shoes" };
    for (size_t r = 0; r < 4; ++r) {
        ModelMaterial m;
        m.name = names[r];
        m.baseColor = colours[r];
        m.roughness = r == 0 ? 0.6f : 0.85f;
        out.materials.push_back(std::move(m));
    }
    std::vector<BodyRegion> regionOf(model.bones.size(), BodyRegion::Top);
    for (size_t b = 0; b < model.bones.size(); ++b) regionOf[b] = regionOfBone(model.bones[b].name, outfit.sleeves);
    for (const ModelMesh& mesh : model.meshes) {
        if (!mesh.skinned || model.bones.empty()) {
            // Not moved by bones (a prop on the model): its own material, unchanged.
            ModelMesh copy = mesh;
            copy.material = static_cast<uint32_t>(out.materials.size());
            out.materials.push_back(mesh.material < model.materials.size() ? model.materials[mesh.material] : ModelMaterial{});
            out.meshes.push_back(std::move(copy));
            continue;
        }
        std::array<ModelMesh, 4> parts;
        std::array<std::vector<uint32_t>, 4> remap;
        for (size_t r = 0; r < 4; ++r) {
            parts[r].name = mesh.name + "_" + names[r];
            parts[r].material = static_cast<uint32_t>(r);
            parts[r].skinned = true;
            remap[r].assign(mesh.vertices.size(), UINT32_MAX);
        }
        for (size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
            std::array<float, 4> weight{};
            for (size_t k = 0; k < 3; ++k) {
                const ModelVertex& v = mesh.vertices[mesh.indices[t + k]];
                for (int j = 0; j < 4; ++j)
                    if (v.joints[j] < regionOf.size()) weight[static_cast<size_t>(regionOf[v.joints[j]])] += v.weights[j];
            }
            size_t region = 1;
            for (size_t r = 0; r < 4; ++r)
                if (weight[r] > weight[region]) region = r;
            ModelMesh& part = parts[region];
            for (size_t k = 0; k < 3; ++k) {
                const uint32_t i = mesh.indices[t + k];
                if (remap[region][i] == UINT32_MAX) {
                    remap[region][i] = static_cast<uint32_t>(part.vertices.size());
                    part.vertices.push_back(mesh.vertices[i]);
                }
                part.indices.push_back(remap[region][i]);
            }
        }
        for (ModelMesh& part : parts)
            if (!part.indices.empty()) out.meshes.push_back(std::move(part));
    }
    return out;
}

const std::vector<NamedColour>& skinTones() {
    static const std::vector<NamedColour> tones = {
        { "Porcelain", { 0.96f, 0.80f, 0.69f } }, { "Ivory", { 0.93f, 0.75f, 0.60f } },  { "Sand", { 0.87f, 0.67f, 0.50f } },
        { "Honey", { 0.78f, 0.57f, 0.39f } },     { "Caramel", { 0.67f, 0.46f, 0.30f } }, { "Bronze", { 0.55f, 0.36f, 0.22f } },
        { "Umber", { 0.42f, 0.27f, 0.17f } },     { "Espresso", { 0.30f, 0.19f, 0.12f } }, { "Ebony", { 0.20f, 0.13f, 0.09f } },
    };
    return tones;
}

std::string outfitKey(const Outfit& o) {
    std::string key;
    for (const glm::vec3& c : { o.skin, o.top, o.bottom, o.shoes })
        for (int i = 0; i < 3; ++i) {
            const int v = static_cast<int>(glm::clamp(c[i], 0.0f, 2.0f) * 255.0f + 0.5f);
            key += "0123456789abcdef"[(v >> 8) & 15];
            key += "0123456789abcdef"[(v >> 4) & 15];
            key += "0123456789abcdef"[v & 15];
        }
    if (o.sleeves) key += 's';
    return key;
}

const std::vector<NamedColour>& clothColours() {
    static const std::vector<NamedColour> colours = {
        { "Sky", { 0.35f, 0.65f, 1.0f } },  { "Ember", { 1.0f, 0.45f, 0.2f } },  { "Moss", { 0.45f, 0.85f, 0.35f } },
        { "Plum", { 0.62f, 0.38f, 0.85f } }, { "Sun", { 1.0f, 0.82f, 0.22f } },   { "Rose", { 1.0f, 0.45f, 0.62f } },
        { "Snow", { 0.92f, 0.92f, 0.9f } },  { "Night", { 0.16f, 0.18f, 0.24f } }, { "Denim", { 0.22f, 0.32f, 0.52f } },
        { "Khaki", { 0.66f, 0.6f, 0.44f } }, { "Red", { 0.8f, 0.12f, 0.12f } },   { "Teal", { 0.15f, 0.62f, 0.62f } },
    };
    return colours;
}

} // namespace kke
