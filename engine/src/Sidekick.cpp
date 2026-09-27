#include "kke/Sidekick.h"

#include "kke/DataFile.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <map>
#include <stdexcept>
#include <unordered_map>
#include <cctype>
#include <cstdio>
#include <system_error>
#include <utility>

namespace fs = std::filesystem;

namespace kke {

namespace {

// The pack's Resources/Meshes folder: walk up from the .sk file.
std::string findMeshRoot(const fs::path& skFile) {
    std::error_code ec;
    for (fs::path p = skFile.parent_path(); !p.empty(); p = p.parent_path()) {
        const fs::path candidate = p / "Resources" / "Meshes";
        if (fs::is_directory(candidate, ec)) return candidate.string();
        if (p == p.parent_path()) break;
    }
    return {};
}

bool nearlyEqual(const glm::mat4& a, const glm::mat4& b, float eps) {
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            if (std::abs(a[c][r] - b[c][r]) > eps) return false;
    return true;
}

} // namespace

SidekickCharacter readSidekickCharacter(const std::string& skFile, const std::vector<std::string>& meshRoots) {
    std::error_code ec;
    if (!fs::exists(skFile, ec)) throw std::runtime_error("readSidekickCharacter: no file '" + skFile + "'");
    // A .sk file is YAML with an extension the loader doesn't know.
    std::string text;
    {
        FILE* f = std::fopen(skFile.c_str(), "rb");
        if (!f) throw std::runtime_error("readSidekickCharacter: can't open '" + skFile + "'");
        char buf[4096];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) text.append(buf, n);
        std::fclose(f);
    }
    nlohmann::json doc;
    std::string error;
    if (!datafile::parse(text, datafile::Format::Yaml, doc, &error))
        throw std::runtime_error("readSidekickCharacter: '" + skFile + "': " + error);

    SidekickCharacter c;
    c.name = datafile::text(doc, "Name", fs::path(skFile).stem().string());
    if (doc.contains("Parts") && doc["Parts"].is_array())
        for (const auto& p : doc["Parts"]) {
            SidekickPart part;
            part.name = datafile::text(p, "Name");
            part.type = datafile::text(p, "PartType");
            if (!part.name.empty()) c.parts.push_back(std::move(part));
        }
    if (c.parts.empty()) throw std::runtime_error("readSidekickCharacter: '" + skFile + "' names no parts");

    // Index every FBX under the roots once, by stem.
    std::vector<std::string> roots = meshRoots;
    if (roots.empty()) {
        const std::string r = findMeshRoot(fs::path(skFile));
        if (!r.empty()) roots.push_back(r);
    }
    std::unordered_map<std::string, std::string> byStem;
    for (const std::string& root : roots) {
        for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end;
             it.increment(ec)) {
            if (!it->is_regular_file(ec)) continue;
            std::string ext = it->path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            if (ext == ".fbx") byStem.emplace(it->path().stem().string(), it->path().string());
        }
    }
    for (SidekickPart& p : c.parts) {
        auto it = byStem.find(p.name);
        if (it != byStem.end()) p.file = it->second;
    }

    const fs::path tex = fs::path(skFile).parent_path() / "Textures" / ("T_" + c.name + "ColorMap.png");
    if (fs::exists(tex, ec)) c.colorMap = tex.string();
    return c;
}

ModelData mergeSkinnedModels(const std::vector<ModelData>& models) {
    ModelData out;
    std::unordered_map<std::string, int> boneIndex;
    std::map<std::pair<std::string, std::string>, uint32_t> materialIndex;
    for (const ModelData& m : models) {
        if (out.sourcePath.empty()) out.sourcePath = m.sourcePath;
        // Bones: add the ones not seen yet (parents come first in each
        // model, so a new bone's parent is already in `out`).
        std::vector<int> remap(m.bones.size(), -1);
        for (size_t b = 0; b < m.bones.size(); ++b) {
            auto it = boneIndex.find(m.bones[b].name);
            if (it != boneIndex.end()) {
                remap[b] = it->second;
                continue;
            }
            ModelBone bone = m.bones[b];
            bone.parent = bone.parent >= 0 ? remap[static_cast<size_t>(bone.parent)] : -1;
            remap[b] = static_cast<int>(out.bones.size());
            boneIndex.emplace(bone.name, remap[b]);
            out.bones.push_back(std::move(bone));
        }
        // This model's mesh geometry space may differ from the merged one
        // by a rigid transform: inverseBind_here = inverseBind_merged * G.
        glm::mat4 toMerged(1.0f);
        for (size_t b = 0; b < m.bones.size(); ++b) {
            const ModelBone& ours = out.bones[static_cast<size_t>(remap[b])];
            if (ours.name == m.bones[b].name && !nearlyEqual(ours.inverseBind, m.bones[b].inverseBind, 1e-4f)) {
                toMerged = glm::inverse(ours.inverseBind) * m.bones[b].inverseBind;
                break;
            }
        }
        const glm::mat3 normalMat = glm::transpose(glm::inverse(glm::mat3(toMerged)));
        const bool moved = !nearlyEqual(toMerged, glm::mat4(1.0f), 1e-6f);

        std::vector<uint32_t> matRemap(m.materials.size(), 0);
        for (size_t i = 0; i < m.materials.size(); ++i) {
            const auto key = std::make_pair(m.materials[i].name, m.materials[i].albedoTexture);
            auto it = materialIndex.find(key);
            if (it == materialIndex.end()) {
                it = materialIndex.emplace(key, static_cast<uint32_t>(out.materials.size())).first;
                out.materials.push_back(m.materials[i]);
            }
            matRemap[i] = it->second;
        }
        for (const ModelMesh& mesh : m.meshes) {
            ModelMesh copy = mesh;
            copy.material = mesh.material < matRemap.size() ? matRemap[mesh.material] : 0;
            if (copy.skinned)
                for (ModelVertex& v : copy.vertices) {
                    for (int k = 0; k < 4; ++k) {
                        const uint32_t j = v.joints[k];
                        v.joints[k] = j < remap.size() && remap[j] >= 0 ? static_cast<uint32_t>(remap[j]) : 0u;
                    }
                    if (moved) {
                        v.position = glm::vec3(toMerged * glm::vec4(v.position, 1.0f));
                        v.normal = glm::normalize(normalMat * v.normal);
                    }
                }
            out.meshes.push_back(std::move(copy));
        }
        if (out.meshes.size() == m.meshes.size()) {
            out.boundsMin = m.boundsMin;
            out.boundsMax = m.boundsMax;
        } else {
            out.boundsMin = glm::min(out.boundsMin, m.boundsMin);
            out.boundsMax = glm::max(out.boundsMax, m.boundsMax);
        }
        // Clips: the first model that has them.
        if (out.animations.empty()) out.animations = m.animations;
    }
    return out;
}

void joinMeshesByMaterial(ModelData& model) {
    std::vector<ModelMesh> joined;
    std::map<std::pair<uint32_t, bool>, size_t> slot;
    for (ModelMesh& m : model.meshes) {
        const auto key = std::make_pair(m.material, m.skinned);
        auto it = slot.find(key);
        if (it == slot.end()) {
            slot.emplace(key, joined.size());
            joined.push_back(std::move(m));
            continue;
        }
        ModelMesh& into = joined[it->second];
        const uint32_t base = static_cast<uint32_t>(into.vertices.size());
        into.vertices.insert(into.vertices.end(), m.vertices.begin(), m.vertices.end());
        for (uint32_t i : m.indices) into.indices.push_back(base + i);
        into.name += "+" + m.name;
    }
    model.meshes = std::move(joined);
}

ModelData loadSidekickCharacter(const SidekickCharacter& character, const SidekickLoadOptions& options, std::vector<std::string>* missing) {
    ModelLoadOptions lo;
    lo.loadAnimations = false;
    lo.fixUnitMismatch = false;
    std::vector<ModelData> parts;
    for (const SidekickPart& p : character.parts) {
        if (std::find(options.skipTypes.begin(), options.skipTypes.end(), p.type) != options.skipTypes.end()) continue;
        if (p.file.empty()) {
            if (missing) missing->push_back(p.name);
            continue;
        }
        try {
            parts.push_back(loadModel(p.file, lo));
        } catch (const std::exception&) {
            if (missing) missing->push_back(p.name);
        }
    }
    if (parts.empty()) throw std::runtime_error("loadSidekickCharacter: none of '" + character.name + "''s parts loaded");
    ModelData model = mergeSkinnedModels(parts);
    for (ModelMaterial& m : model.materials)
        if (!character.colorMap.empty()) {
            m.albedoTexture = character.colorMap;
            m.baseColor = glm::vec3(1.0f);
        }
    // Every part uses the one colour map: one mesh per material.
    if (!character.colorMap.empty()) {
        for (ModelMesh& m : model.meshes) m.material = 0;
        model.materials.resize(1);
        model.materials[0].name = character.name;
    }
    joinMeshesByMaterial(model);
    model.sourcePath = character.name;
    return model;
}

} // namespace kke
