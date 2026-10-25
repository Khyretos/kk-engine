#include "kke/Vrm.h"

#include "ModelAssetDetail.h"
#include "kke/CookedFile.h"

#include <cgltf.h>
#include <nlohmann/json.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace kke {

namespace {

using json = nlohmann::json;

[[noreturn]] void fail(const std::string& path, const std::string& why) {
    throw std::runtime_error("VRM '" + path + "': " + why);
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

float number(const json& j, const char* key, float fallback) {
    auto it = j.find(key);
    return it != j.end() && it->is_number() ? it->get<float>() : fallback;
}

std::string text(const json& j, const char* key) {
    auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
}

glm::vec4 vec4Of(const json& props, const char* key, const glm::vec4& fallback) {
    auto it = props.find(key);
    if (it == props.end() || !it->is_array() || it->size() < 3) return fallback;
    const json& a = *it;
    return { a[0].get<float>(), a[1].get<float>(), a[2].get<float>(), a.size() > 3 ? a[3].get<float>() : 1.0f };
}

glm::vec3 xyz(const json& j, const glm::vec3& fallback) {
    if (!j.is_object()) return fallback;
    return { number(j, "x", fallback.x), number(j, "y", fallback.y), number(j, "z", fallback.z) };
}

struct Data {
    cgltf_data* data = nullptr;
    ~Data() {
        if (data) cgltf_free(data);
    }
};

// The image's bytes as an in-memory file, named as the glTF loader names
// the ones it reads ("<path>#image<N>.<ext>"), so a texture both name is
// loaded once. "" when the image isn't inside the file.
std::string imageFile(const std::string& path, const cgltf_data* data, cgltf_size textureIndex) {
    if (textureIndex >= data->textures_count) return {};
    const cgltf_image* img = data->textures[textureIndex].image;
    if (!img || !img->buffer_view) return {};
    const std::string mime = img->mime_type ? img->mime_type : "";
    const std::string ext = mime == "image/jpeg" ? ".jpg" : ".png";
    const std::string file = path + "#image" + std::to_string(static_cast<size_t>(img - data->images)) + ext;
    if (!cooked::hasMemoryFile(file)) {
        const uint8_t* bytes = cgltf_buffer_view_data(img->buffer_view);
        if (!bytes) return {};
        cooked::putMemoryFile(file, std::vector<uint8_t>(bytes, bytes + img->buffer_view->size));
    }
    return file;
}

VrmToon readToon(const json& mat, const std::string& path, const cgltf_data* data) {
    VrmToon t;
    t.renderQueue = mat.value("renderQueue", 2000);
    const json floats = mat.value("floatProperties", json::object());
    const json vectors = mat.value("vectorProperties", json::object());
    const json textures = mat.value("textureProperties", json::object());
    const int blend = static_cast<int>(number(floats, "_BlendMode", 0.0f));
    t.alpha = blend == 0 ? VrmToon::Alpha::Opaque : blend == 1 ? VrmToon::Alpha::Cutout : VrmToon::Alpha::Blend;
    t.cutoff = number(floats, "_Cutoff", 0.5f);
    t.doubleSided = number(floats, "_CullMode", 2.0f) == 0.0f;
    t.color = vec4Of(vectors, "_Color", glm::vec4(1.0f));
    t.shadeColor = glm::vec3(vec4Of(vectors, "_ShadeColor", glm::vec4(0.7f)));
    t.emissionColor = glm::vec3(vec4Of(vectors, "_EmissionColor", glm::vec4(0.0f)));
    t.rimColor = glm::vec3(vec4Of(vectors, "_RimColor", glm::vec4(0.0f)));
    t.outlineColor = glm::vec3(vec4Of(vectors, "_OutlineColor", glm::vec4(0.0f, 0.0f, 0.0f, 1.0f)));
    t.normalScale = number(floats, "_BumpScale", 1.0f);
    t.shadeShift = number(floats, "_ShadeShift", 0.0f);
    t.shadeToony = number(floats, "_ShadeToony", 0.9f);
    t.receiveShadow = number(floats, "_ReceiveShadowRate", 1.0f);
    t.lightColorAttenuation = number(floats, "_LightColorAttenuation", 0.0f);
    t.giIntensity = number(floats, "_IndirectLightIntensity", 0.1f);
    t.rimFresnelPower = number(floats, "_RimFresnelPower", 1.0f);
    t.rimLift = number(floats, "_RimLift", 0.0f);
    t.rimLightingMix = number(floats, "_RimLightingMix", 0.0f);
    const int widthMode = static_cast<int>(number(floats, "_OutlineWidthMode", 0.0f));
    t.outlineWidth = widthMode == 0 ? 0.0f : number(floats, "_OutlineWidth", 0.0f);
    t.outlineScreenWidth = widthMode == 2;
    t.outlineFixedColor = number(floats, "_OutlineColorMode", 0.0f) == 0.0f;
    t.outlineLightingMix = number(floats, "_OutlineLightingMix", 1.0f);
    auto tex = [&](const char* key) {
        auto it = textures.find(key);
        return it != textures.end() && it->is_number_integer() ? imageFile(path, data, it->get<cgltf_size>()) : std::string();
    };
    t.mainTexture = tex("_MainTex");
    t.shadeTexture = tex("_ShadeTexture");
    t.normalTexture = tex("_BumpMap");
    t.matcapTexture = tex("_SphereAdd");
    t.emissionTexture = tex("_EmissionMap");
    t.rimTexture = tex("_RimTexture");
    t.outlineWidthTexture = tex("_OutlineWidthTexture");
    return t;
}

// VRM 0.x faces -Z: half a turn round Y faces it +Z like the engine's
// other characters. Vertices, normals and morph offsets turn; the root
// bones' rest turns with them and every inverse bind takes the turn back
// off, so skinning gives the same shape, turned.
void faceForward(VrmAvatar& a) {
    const glm::mat4 turn = glm::rotate(glm::mat4(1.0f), glm::pi<float>(), glm::vec3(0, 1, 0));
    const glm::mat3 turn3(turn);
    for (ModelMesh& m : a.model.meshes)
        for (ModelVertex& v : m.vertices) {
            v.position = turn3 * v.position;
            v.normal = turn3 * v.normal;
        }
    for (VrmMorph& m : a.morphs) {
        for (glm::vec3& p : m.positions) p = turn3 * p;
        for (glm::vec3& n : m.normals) n = turn3 * n;
    }
    const glm::mat4 back = glm::transpose(turn); // a rotation's inverse
    for (ModelBone& b : a.model.bones) {
        if (b.parent < 0) b.localRest = turn * b.localRest;
        b.inverseBind = b.inverseBind * back;
    }
    for (ModelAnimation& anim : a.model.animations)
        for (std::vector<glm::mat4>& frame : anim.frames)
            for (size_t b = 0; b < frame.size() && b < a.model.bones.size(); ++b)
                if (a.model.bones[b].parent < 0) frame[b] = turn * frame[b];
    for (VrmSpringGroup& g : a.springs) g.gravityDir = turn3 * g.gravityDir;
    std::swap(a.model.boundsMin.x, a.model.boundsMax.x);
    std::swap(a.model.boundsMin.z, a.model.boundsMax.z);
    a.model.boundsMin.x = -a.model.boundsMin.x;
    a.model.boundsMax.x = -a.model.boundsMax.x;
    a.model.boundsMin.z = -a.model.boundsMin.z;
    a.model.boundsMax.z = -a.model.boundsMax.z;
}

} // namespace

VrmAvatar loadVrm(const std::string& path) {
    VrmAvatar a;
    ModelLoadOptions options;
    options.clipsInPlace = false;
    a.model = detail::loadGltfModel(path, options);

    std::vector<uint8_t> bytes;
    if (!cooked::readAssetFile(path, bytes)) fail(path, "can't be read");
    cgltf_options parse{};
    Data d;
    if (cgltf_parse(&parse, bytes.data(), bytes.size(), &d.data) != cgltf_result_success) fail(path, "is not a glTF file");
    if (cgltf_load_buffers(&parse, d.data, path.c_str()) != cgltf_result_success) fail(path, "its buffers can't be read");
    const cgltf_data* data = d.data;

    json vrm;
    for (cgltf_size i = 0; i < data->data_extensions_count; ++i) {
        const cgltf_extension& e = data->data_extensions[i];
        if (e.name && std::string(e.name) == "VRM" && e.data) vrm = json::parse(e.data, nullptr, false);
        if (e.name && std::string(e.name) == "VRMC_vrm") fail(path, "is VRM 1.0, which isn't read yet (VRM 0.x is)");
    }
    if (!vrm.is_object()) fail(path, "has no VRM extension (a plain glTF file: load it with loadModel)");

    // Bones by glTF node (the glTF loader names a bone after its node).
    auto boneOf = [&](int node) -> int {
        if (node < 0 || static_cast<cgltf_size>(node) >= data->nodes_count) return -1;
        const cgltf_node& n = data->nodes[node];
        return a.model.findBone(n.name ? n.name : "node" + std::to_string(node));
    };

    // Morph targets: the glTF loader makes one ModelMesh per primitive, in
    // order, named after its glTF mesh, with the primitive's vertices as
    // they are (normals present), so offsets line up index for index.
    std::vector<std::vector<int>> morphIndex(data->meshes_count); // [glTF mesh][target] -> first VrmAvatar::morphs entry
    std::vector<std::vector<std::vector<uint32_t>>> morphsOf(data->meshes_count);
    for (cgltf_size mi = 0; mi < data->meshes_count; ++mi) {
        const cgltf_mesh& mesh = data->meshes[mi];
        const std::string name = mesh.name ? mesh.name : "";
        std::vector<uint32_t> parts;
        for (uint32_t k = 0; k < a.model.meshes.size(); ++k)
            if (a.model.meshes[k].name == name) parts.push_back(k);
        size_t targets = 0;
        for (cgltf_size p = 0; p < mesh.primitives_count; ++p) targets = std::max<size_t>(targets, mesh.primitives[p].targets_count);
        morphsOf[mi].resize(targets);
        for (cgltf_size p = 0; p < mesh.primitives_count && p < parts.size(); ++p) {
            const cgltf_primitive& prim = mesh.primitives[p];
            const uint32_t part = parts[p];
            for (cgltf_size t = 0; t < prim.targets_count; ++t) {
                const cgltf_morph_target& target = prim.targets[t];
                VrmMorph m;
                m.mesh = part;
                const cgltf_accessor* pos = nullptr;
                const cgltf_accessor* nrm = nullptr;
                for (cgltf_size k = 0; k < target.attributes_count; ++k) {
                    if (target.attributes[k].type == cgltf_attribute_type_position) pos = target.attributes[k].data;
                    if (target.attributes[k].type == cgltf_attribute_type_normal) nrm = target.attributes[k].data;
                }
                const size_t count = a.model.meshes[part].vertices.size();
                if (!pos || pos->count != count) continue;
                std::vector<float> pv(pos->count * 3), nv;
                cgltf_accessor_unpack_floats(pos, pv.data(), pv.size());
                if (nrm && nrm->count == count) {
                    nv.resize(nrm->count * 3);
                    cgltf_accessor_unpack_floats(nrm, nv.data(), nv.size());
                }
                for (size_t v = 0; v < count; ++v) {
                    const glm::vec3 dp(pv[v * 3], pv[v * 3 + 1], pv[v * 3 + 2]);
                    const glm::vec3 dn = nv.empty() ? glm::vec3(0.0f) : glm::vec3(nv[v * 3], nv[v * 3 + 1], nv[v * 3 + 2]);
                    if (glm::dot(dp, dp) < 1e-14f && glm::dot(dn, dn) < 1e-10f) continue; // most of a face target is still
                    m.vertices.push_back(static_cast<uint32_t>(v));
                    m.positions.push_back(dp);
                    m.normals.push_back(dn);
                }
                if (m.vertices.empty()) continue;
                morphsOf[mi][t].push_back(static_cast<uint32_t>(a.morphs.size()));
                a.morphs.push_back(std::move(m));
            }
        }
    }

    // Expressions.
    const json groups = vrm.value("blendShapeMaster", json::object()).value("blendShapeGroups", json::array());
    for (const json& g : groups) {
        VrmExpression e;
        e.name = text(g, "name");
        e.preset = lower(text(g, "presetName"));
        e.binary = g.value("isBinary", false);
        for (const json& b : g.value("binds", json::array())) {
            const int mesh = b.value("mesh", -1), index = b.value("index", -1);
            if (mesh < 0 || static_cast<size_t>(mesh) >= morphsOf.size() || index < 0 || static_cast<size_t>(index) >= morphsOf[mesh].size()) continue;
            for (uint32_t m : morphsOf[mesh][index]) e.binds.push_back({ m, number(b, "weight", 100.0f) / 100.0f });
        }
        a.expressions.push_back(std::move(e));
    }

    // MToon, matched to the glTF materials by name (else by order).
    a.toon.resize(a.model.materials.size());
    const json props = vrm.value("materialProperties", json::array());
    for (size_t m = 0; m < a.model.materials.size(); ++m) {
        const json* found = nullptr;
        for (const json& p : props)
            if (text(p, "name") == a.model.materials[m].name) found = &p;
        if (!found && m < props.size()) found = &props[m];
        if (found && lower(text(*found, "shader")).find("mtoon") != std::string::npos) a.toon[m] = readToon(*found, path, data);
        else {
            VrmToon& t = a.toon[m]; // not MToon (unlit or standard): the material's own colour and map, lightly shaded
            t.color = glm::vec4(a.model.materials[m].baseColor, 1.0f);
            t.mainTexture = t.shadeTexture = a.model.materials[m].albedoTexture;
            t.shadeColor = glm::vec3(0.8f);
        }
        if (a.toon[m].mainTexture.empty()) a.toon[m].mainTexture = a.model.materials[m].albedoTexture;
        if (a.toon[m].shadeTexture.empty()) a.toon[m].shadeTexture = a.toon[m].mainTexture;
    }

    // Humanoid bones.
    for (const json& h : vrm.value("humanoid", json::object()).value("humanBones", json::array())) {
        const int bone = boneOf(h.value("node", -1));
        if (bone >= 0) a.humanoid[text(h, "bone")] = bone;
    }

    // Spring bones and their colliders.
    const json secondary = vrm.value("secondaryAnimation", json::object());
    for (const json& c : secondary.value("colliderGroups", json::array())) {
        VrmColliderGroup g;
        g.bone = boneOf(c.value("node", -1));
        for (const json& s : c.value("colliders", json::array()))
            g.colliders.push_back({ xyz(s.value("offset", json::object()), glm::vec3(0.0f)), number(s, "radius", 0.05f) });
        a.colliderGroups.push_back(std::move(g));
    }
    for (const json& s : secondary.value("boneGroups", json::array())) {
        VrmSpringGroup g;
        g.stiffness = number(s, "stiffiness", number(s, "stiffness", 1.0f)); // VRM 0.x's own spelling
        g.gravityPower = number(s, "gravityPower", 0.0f);
        g.gravityDir = xyz(s.value("gravityDir", json::object()), glm::vec3(0, -1, 0));
        g.drag = number(s, "dragForce", 0.4f);
        g.hitRadius = number(s, "hitRadius", 0.02f);
        for (const json& b : s.value("bones", json::array()))
            if (const int bone = boneOf(b.get<int>()); bone >= 0) g.roots.push_back(bone);
        for (const json& c : s.value("colliderGroups", json::array()))
            if (c.is_number_integer() && c.get<size_t>() < a.colliderGroups.size()) g.colliderGroups.push_back(c.get<uint32_t>());
        if (!g.roots.empty()) a.springs.push_back(std::move(g));
    }

    const json meta = vrm.value("meta", json::object());
    a.meta = { text(meta, "title"), text(meta, "author"), text(meta, "version"), text(meta, "licenseName"),
               text(meta, "contactInformation"), text(meta, "reference"), text(meta, "allowedUserName"),
               text(meta, "violentUssageName"), text(meta, "sexualUssageName"), text(meta, "commercialUssageName"),
               text(meta, "otherPermissionUrl"), text(meta, "otherLicenseUrl") };

    faceForward(a);

    // The eyes' height: the head bone's, raised by VRM's first-person offset.
    const std::vector<glm::mat4> rest = computeRestPose(a.model);
    const json first = vrm.value("firstPerson", json::object());
    int head = boneOf(first.value("firstPersonBone", -1));
    if (head < 0) head = a.bone("head");
    if (head >= 0) a.eyeHeight = rest[static_cast<size_t>(head)][3].y + xyz(first.value("firstPersonBoneOffset", json::object()), glm::vec3(0.0f, 0.06f, 0.0f)).y;
    return a;
}

int VrmAvatar::expression(const std::string& nameOrPreset) const {
    const std::string want = lower(nameOrPreset);
    for (size_t i = 0; i < expressions.size(); ++i)
        if (expressions[i].preset == want) return static_cast<int>(i);
    for (size_t i = 0; i < expressions.size(); ++i)
        if (lower(expressions[i].name) == want) return static_cast<int>(i);
    return -1;
}

int VrmAvatar::bone(const std::string& humanoidName) const {
    auto it = humanoid.find(humanoidName);
    return it == humanoid.end() ? -1 : it->second;
}

std::vector<glm::mat4> VrmAvatar::restLocals() const {
    std::vector<glm::mat4> out;
    out.reserve(model.bones.size());
    for (const ModelBone& b : model.bones) out.push_back(b.localRest);
    return out;
}

std::vector<float> VrmAvatar::weights(const std::vector<float>& expressionWeights) const {
    std::vector<float> out(morphs.size(), 0.0f);
    for (size_t e = 0; e < expressions.size() && e < expressionWeights.size(); ++e) {
        float w = std::clamp(expressionWeights[e], 0.0f, 1.0f);
        if (expressions[e].binary) w = w >= 0.5f ? 1.0f : 0.0f;
        if (w <= 0.0f) continue;
        for (const auto& [morph, weight] : expressions[e].binds) out[morph] += w * weight;
    }
    for (float& w : out) w = std::min(w, 1.0f);
    return out;
}

void VrmAvatar::pose(const std::vector<glm::mat4>& locals, const std::vector<float>& morphWeights,
                     std::vector<std::vector<ModelVertex>>& out) const {
    out.resize(model.meshes.size());
    for (size_t m = 0; m < model.meshes.size(); ++m) out[m] = model.meshes[m].vertices;
    for (size_t i = 0; i < morphs.size() && i < morphWeights.size(); ++i) {
        const float w = morphWeights[i];
        if (w <= 0.0f) continue;
        const VrmMorph& mo = morphs[i];
        std::vector<ModelVertex>& verts = out[mo.mesh];
        for (size_t k = 0; k < mo.vertices.size(); ++k) {
            ModelVertex& v = verts[mo.vertices[k]];
            v.position += mo.positions[k] * w;
            v.normal += mo.normals[k] * w;
        }
    }
    if (model.bones.empty()) return;
    // Plain float arithmetic: this runs every frame for some 50,000
    // vertices, and glm's matrix types are slow in unoptimised builds.
    const std::vector<glm::mat4> skin = computeSkinMatrices(model, locals);
    std::vector<float> rows(skin.size() * 12);
    for (size_t b = 0; b < skin.size(); ++b)
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 4; ++c) rows[b * 12 + static_cast<size_t>(r * 4 + c)] = skin[b][c][r];
    for (size_t m = 0; m < model.meshes.size(); ++m) {
        if (!model.meshes[m].skinned) continue;
        for (ModelVertex& v : out[m]) {
            float t[12] = {};
            for (int k = 0; k < 4; ++k) {
                const float w = v.weights[k];
                if (w <= 0.0f) continue;
                const float* r = &rows[static_cast<size_t>(v.joints[k]) * 12];
                for (int i = 0; i < 12; ++i) t[i] += r[i] * w;
            }
            const float px = v.position.x, py = v.position.y, pz = v.position.z;
            const float nx = v.normal.x, ny = v.normal.y, nz = v.normal.z;
            v.position.x = t[0] * px + t[1] * py + t[2] * pz + t[3];
            v.position.y = t[4] * px + t[5] * py + t[6] * pz + t[7];
            v.position.z = t[8] * px + t[9] * py + t[10] * pz + t[11];
            // Rigid bones: the matrix itself turns the normal well enough.
            const float qx = t[0] * nx + t[1] * ny + t[2] * nz;
            const float qy = t[4] * nx + t[5] * ny + t[6] * nz;
            const float qz = t[8] * nx + t[9] * ny + t[10] * nz;
            const float len = std::sqrt(qx * qx + qy * qy + qz * qz);
            v.normal = len > 1e-8f ? glm::vec3(qx / len, qy / len, qz / len) : glm::vec3(0, 1, 0);
        }
    }
}

} // namespace kke
