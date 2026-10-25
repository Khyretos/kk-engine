#include "kke/HumanClothes.h"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace kke {

const std::vector<GarmentInfo>& garments() {
    static const std::vector<GarmentInfo> list = {
        { "t-shirt", "T-shirt", "top" },          { "tank top", "Tank top", "top" },
        { "crop top", "Crop top", "top" },        { "long sleeve", "Long sleeve", "top" },
        { "shorts", "Shorts", "bottom" },         { "trousers", "Trousers", "bottom" },
        { "tennis skirt", "Tennis skirt", "bottom" }, { "skirt", "Skirt", "bottom" },
        { "long skirt", "Long skirt", "bottom" }, { "socks", "Socks", "socks" },
        { "shoes", "Shoes", "shoes" },
    };
    return list;
}

const GarmentInfo* findGarment(const std::string& id) {
    for (const GarmentInfo& g : garments())
        if (g.id == id) return &g;
    return nullptr;
}

namespace {

// Every vertex of a group, from its faces.
std::vector<uint32_t> groupVerts(const HumanPack& pack, const std::string& name) {
    const int g = pack.findGroup(name);
    if (g < 0) return {};
    std::set<uint32_t> out;
    const HumanGroup& group = pack.groups()[static_cast<size_t>(g)];
    for (uint32_t f = group.firstFace; f < group.firstFace + group.faceCount; ++f)
        for (uint32_t v : pack.quads()[f].verts) out.insert(v);
    return { out.begin(), out.end() };
}

// The bone that moves each vertex most ("" = none).
std::vector<std::string> mainBone(const HumanPack& pack) {
    std::vector<std::string> bone(pack.positions().size());
    std::vector<float> best(pack.positions().size(), 0.0f);
    for (const auto& [name, w] : pack.weights())
        for (size_t i = 0; i < w.verts.size(); ++i)
            if (w.weights[i] > best[w.verts[i]]) {
                best[w.verts[i]] = w.weights[i];
                bone[w.verts[i]] = name;
            }
    return bone;
}

bool startsWith(const std::string& s, const char* prefix) { return s.rfind(prefix, 0) == 0; }

} // namespace

GarmentShape garmentShape(const HumanPack& pack, const std::vector<glm::vec3>& shaped, const std::string& id) {
    GarmentShape out;
    if (!findGarment(id) || shaped.size() != pack.positions().size()) return out;

    // Skirts: the long skirt's top part, as cloth pinned at the waist.
    const float skirtLength = id == "tennis skirt" ? 0.3f : id == "skirt" ? 0.55f : id == "long skirt" ? 1.0f : 0.0f;
    if (skirtLength > 0.0f) {
        out.group = "helper-skirt";
        out.cloth = true;
        const std::vector<uint32_t> all = groupVerts(pack, out.group);
        if (all.empty()) return out;
        float top = -1e9f, bottom = 1e9f;
        for (uint32_t v : all) {
            top = std::max(top, shaped[v].y);
            bottom = std::min(bottom, shaped[v].y);
        }
        const float hem = top - skirtLength * (top - bottom) - 1e-4f;
        for (uint32_t v : all) {
            if (shaped[v].y < hem) continue;
            out.verts.push_back(v);
            if (shaped[v].y > top - 0.012f) out.pinned.push_back(v); // the waistband
        }
        return out;
    }

    // The rest: the body suit's vertices, by the bone that moves them.
    out.group = "helper-tights";
    const std::vector<std::string> bone = mainBone(pack);
    auto torso = [](const std::string& b) {
        return startsWith(b, "spine0") || startsWith(b, "breast") || startsWith(b, "clavicle") || startsWith(b, "shoulder01");
    };
    auto arm = [](const std::string& b) {
        return startsWith(b, "upperarm") || startsWith(b, "lowerarm") || startsWith(b, "wrist") || startsWith(b, "metacarpal") ||
               startsWith(b, "finger") || startsWith(b, "hand");
    };
    auto foot = [](const std::string& b) { return startsWith(b, "foot") || startsWith(b, "toe"); };
    const std::vector<uint32_t> suit = groupVerts(pack, out.group);
    float sole = 1e9f;
    for (uint32_t v : suit)
        if (foot(bone[v])) sole = std::min(sole, shaped[v].y);

    // Where tops end and bottoms start: the waist, the skirt's top. The
    // bones alone can't say it (spine05 carries the belly and the groin).
    float waist = -1e9f;
    for (uint32_t v : groupVerts(pack, "helper-skirt")) waist = std::max(waist, shaped[v].y);
    const float hem = waist - 0.03f; // a top tucked in, so no skin shows between
    // Bottoms rise a little over it, under the top (which sits further out).
    auto below = [&](uint32_t v) { return !arm(bone[v]) && !foot(bone[v]) && shaped[v].y < waist + 0.04f; };

    std::function<bool(uint32_t)> wears;
    if (id == "t-shirt") {
        out.offset = 0.003f;
        wears = [&](uint32_t v) { return (torso(bone[v]) || startsWith(bone[v], "upperarm01")) && shaped[v].y > hem; };
    } else if (id == "tank top") {
        out.offset = 0.003f;
        wears = [&](uint32_t v) { return torso(bone[v]) && !startsWith(bone[v], "shoulder01") && shaped[v].y > hem; };
    } else if (id == "crop top") {
        out.offset = 0.003f;
        // The belly shows: it ends a hand's width over the waist.
        wears = [&](uint32_t v) { return (torso(bone[v]) || startsWith(bone[v], "upperarm01")) && shaped[v].y > waist + 0.11f; };
    } else if (id == "long sleeve") {
        out.offset = 0.003f;
        wears = [&](uint32_t v) {
            return (torso(bone[v]) || startsWith(bone[v], "upperarm") || startsWith(bone[v], "lowerarm")) && shaped[v].y > hem;
        };
    } else if (id == "shorts") {
        out.offset = 0.002f;
        wears = [&](uint32_t v) { return below(v) && shaped[v].y > 0.62f * waist; }; // to mid-thigh
    } else if (id == "trousers") {
        out.offset = 0.002f;
        wears = [&](uint32_t v) { return below(v) && shaped[v].y > sole + 0.05f; }; // to the ankles
    } else if (id == "socks") {
        out.offset = 0.001f;
        wears = [&](uint32_t v) { return foot(bone[v]) || (startsWith(bone[v], "lowerleg02") && shaped[v].y < sole + 0.2f); };
    } else { // shoes
        out.offset = 0.006f;
        wears = [&](uint32_t v) { return foot(bone[v]) && shaped[v].y < sole + 0.09f; };
    }
    for (uint32_t v : suit)
        if (wears(v)) out.verts.push_back(v);
    return out;
}

ModelMesh garmentMesh(const ModelMesh& mesh, const std::vector<uint32_t>& sourceVertex, const GarmentShape& shape,
                      std::vector<uint32_t>* from) {
    ModelMesh out;
    out.skinned = mesh.skinned;
    const std::set<uint32_t> in(shape.verts.begin(), shape.verts.end());
    std::map<uint32_t, uint32_t> index;
    std::vector<uint32_t> source;
    for (size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
        const uint32_t a = mesh.indices[t], b = mesh.indices[t + 1], c = mesh.indices[t + 2];
        if (!in.count(sourceVertex[a]) || !in.count(sourceVertex[b]) || !in.count(sourceVertex[c])) continue;
        for (uint32_t v : { a, b, c }) {
            auto [it, added] = index.emplace(v, static_cast<uint32_t>(out.vertices.size()));
            if (added) {
                ModelVertex mv = mesh.vertices[v];
                mv.position += mv.normal * shape.offset;
                out.vertices.push_back(mv);
                source.push_back(v);
            }
            out.indices.push_back(it->second);
        }
    }
    if (from) *from = std::move(source);
    return out;
}

std::vector<uint32_t> skinUnder(const ModelMesh& garment, const ModelMesh& skin) {
    std::vector<uint32_t> under(garment.vertices.size(), 0);
    if (skin.vertices.empty()) return under;
    for (size_t i = 0; i < garment.vertices.size(); ++i) {
        float best = 1e30f;
        for (size_t s = 0; s < skin.vertices.size(); ++s) {
            const glm::vec3 d = skin.vertices[s].position - garment.vertices[i].position;
            if (const float dd = glm::dot(d, d); dd < best) {
                best = dd;
                under[i] = static_cast<uint32_t>(s);
            }
        }
    }
    return under;
}

void keepOutside(ModelMesh& garment, const ModelMesh& skin, const std::vector<uint32_t>& under, float gap) {
    if (under.size() != garment.vertices.size()) return;
    for (size_t i = 0; i < under.size(); ++i) {
        if (under[i] >= skin.vertices.size()) continue;
        const ModelVertex& s = skin.vertices[under[i]];
        const glm::vec3 n = glm::normalize(s.normal);
        const float over = glm::dot(garment.vertices[i].position - s.position, n);
        if (over < gap) garment.vertices[i].position += n * (gap - over);
    }
}

void dress(ModelData& model, const Human& human, const std::vector<HumanLook::Worn>& clothes, bool skeleton) {
    if (clothes.empty()) return;
    Human::BakeOptions options;
    options.groups = { "helper-tights", "helper-skirt" };
    options.skeleton = skeleton;
    const Human::Bake helpers = human.bake(options); // the same bones as any bake of this human
    const std::vector<glm::vec3> shaped = human.shapedPositions();
    ModelMesh skin;
    for (const ModelMesh& m : model.meshes)
        if (m.name == "body") skin = m;
    for (const HumanLook::Worn& worn : clothes) {
        const GarmentShape shape = garmentShape(human.pack(), shaped, worn.id);
        for (size_t m = 0; m < helpers.model.meshes.size(); ++m) {
            if (helpers.model.meshes[m].name != shape.group) continue;
            ModelMesh mesh = garmentMesh(helpers.model.meshes[m], helpers.sourceVertex[m], shape);
            if (mesh.indices.empty()) break;
            keepOutside(mesh, skin, skinUnder(mesh, skin), shape.offset);
            ModelMaterial mat;
            mat.name = "Clothes_" + worn.id;
            mat.baseColor = worn.colour;
            mat.roughness = worn.id == "shoes" ? 0.5f : 0.85f;
            model.materials.push_back(mat);
            mesh.name = worn.id;
            mesh.material = static_cast<uint32_t>(model.materials.size() - 1);
            model.meshes.push_back(std::move(mesh));
        }
    }
}

} // namespace kke
