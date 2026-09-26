#include "kke/ModelAsset.h"

#include <ufbx.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <filesystem>
#include <functional>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace kke {

namespace fs = std::filesystem;

namespace {

glm::mat4 toGlm(const ufbx_matrix& m) {
    // ufbx_matrix is a 3x4 affine matrix stored as 4 columns.
    glm::mat4 r(1.0f);
    for (int c = 0; c < 4; ++c) {
        r[c][0] = static_cast<float>(m.cols[c].x);
        r[c][1] = static_cast<float>(m.cols[c].y);
        r[c][2] = static_cast<float>(m.cols[c].z);
    }
    return r;
}

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// Takes the file name from any path, whichever slash the author's OS used.
std::string baseName(const std::string& path) {
    size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

// Finds `fileName` (case-insensitively) in one of `dirs`, not recursing.
std::string findInDirs(const std::string& fileName, const std::vector<fs::path>& dirs) {
    const std::string wanted = toLower(fileName);
    for (const fs::path& dir : dirs) {
        std::error_code ec;
        if (!fs::is_directory(dir, ec)) continue;
        fs::path direct = dir / fileName;
        if (fs::is_regular_file(direct, ec)) return direct.string();
        for (const auto& entry : fs::directory_iterator(dir, ec)) {
            if (entry.is_regular_file(ec) && toLower(entry.path().filename().string()) == wanted) return entry.path().string();
        }
    }
    return {};
}

struct CornerKey {
    uint32_t position, normal, uv;
    bool operator==(const CornerKey& o) const { return position == o.position && normal == o.normal && uv == o.uv; }
};
struct CornerKeyHash {
    size_t operator()(const CornerKey& k) const {
        return (static_cast<size_t>(k.position) * 73856093u) ^ (static_cast<size_t>(k.normal) * 19349663u) ^ (static_cast<size_t>(k.uv) * 83492791u);
    }
};

} // namespace

bool ModelData::isSkinned() const {
    return std::any_of(meshes.begin(), meshes.end(), [](const ModelMesh& m) { return m.skinned; });
}

int ModelData::findBone(const std::string& name) const {
    for (size_t i = 0; i < bones.size(); ++i) {
        if (bones[i].name == name) return static_cast<int>(i);
    }
    return -1;
}

size_t ModelData::triangleCount() const {
    size_t n = 0;
    for (const ModelMesh& m : meshes) n += m.indices.size() / 3;
    return n;
}

ModelData loadModel(const std::string& path, const ModelLoadOptions& options) {
    ufbx_load_opts opts{};
    opts.target_axes = ufbx_axes_right_handed_y_up;
    opts.target_unit_meters = 1.0f;
    // Bake the unit/axis conversion into geometry and node transforms,
    // so nothing downstream has to know the file was centimeters Z-up.
    opts.space_conversion = UFBX_SPACE_CONVERSION_MODIFY_GEOMETRY;
    opts.generate_missing_normals = true;
    // OBJ materials live in a separate .mtl file; don't fail if it (or
    // anything else external) is missing.
    opts.load_external_files = true;
    opts.ignore_missing_external_files = true;

    ufbx_error error{};
    ufbx_scene* scene = ufbx_load_file(path.c_str(), &opts, &error);
    if (!scene) {
        throw std::runtime_error("loadModel: '" + path + "': " + std::string(error.description.data, error.description.length));
    }
    std::unique_ptr<ufbx_scene, void (*)(ufbx_scene*)> guard(scene, ufbx_free_scene);

    ModelData model;
    model.sourcePath = path;

    // ---- materials (+ texture resolution)
    const fs::path modelDir = fs::path(path).parent_path();
    std::vector<fs::path> searchDirs = { modelDir, modelDir / "Textures", modelDir / "textures",
                                         modelDir.parent_path() / "Textures", modelDir.parent_path() / "textures" };
    for (const std::string& extra : options.textureSearchPaths) searchDirs.emplace_back(extra);

    std::unordered_map<const ufbx_material*, uint32_t> materialIndex;
    for (size_t i = 0; i < scene->materials.count; ++i) {
        const ufbx_material* src = scene->materials.data[i];
        ModelMaterial mat;
        mat.name = std::string(src->name.data, src->name.length);
        const ufbx_material_map& base = src->pbr.base_color.has_value ? src->pbr.base_color : src->fbx.diffuse_color;
        if (base.has_value) {
            mat.baseColor = glm::vec3(static_cast<float>(base.value_vec3.x), static_cast<float>(base.value_vec3.y),
                                      static_cast<float>(base.value_vec3.z));
        }
        const ufbx_texture* tex = base.texture ? base.texture : src->fbx.diffuse_color.texture;
        // A texture node with no file name is no texture.
        if (tex && tex->filename.length == 0 && tex->relative_filename.length == 0) tex = nullptr;
        if (tex) {
            mat.albedoTextureOriginal = std::string(tex->filename.data, tex->filename.length);
            std::error_code ec;
            if (!mat.albedoTextureOriginal.empty() && fs::is_regular_file(mat.albedoTextureOriginal, ec)) {
                mat.albedoTexture = mat.albedoTextureOriginal;
            } else {
                std::string rel(tex->relative_filename.data, tex->relative_filename.length);
                std::string name = baseName(!mat.albedoTextureOriginal.empty() ? mat.albedoTextureOriginal : rel);
                if (!name.empty()) mat.albedoTexture = findInDirs(name, searchDirs);
                // Authoring formats (.psd, .tif...) are often referenced by
                // the FBX while the pack ships the same image as .png/.tga.
                if (mat.albedoTexture.empty() && !name.empty()) {
                    std::string stem = fs::path(name).stem().string();
                    for (const char* ext : { ".png", ".tga", ".jpg", ".jpeg", ".bmp" }) {
                        mat.albedoTexture = findInDirs(stem + ext, searchDirs);
                        if (!mat.albedoTexture.empty()) break;
                    }
                }
            }
            // A textured material's color is a tint; FBX exporters often
            // leave it at mid-grey, which would darken the texture.
            if (!mat.albedoTexture.empty()) mat.baseColor = glm::vec3(1.0f);
        }
        if (mat.albedoTexture.empty() && !options.assetTexture.empty()) {
            // Synty's road points at an artist's missing .psd; its own
            // road texture is the right one, not the pack atlas.
            mat.albedoTexture = options.assetTexture;
            mat.baseColor = glm::vec3(1.0f);
        }
        if (!tex && options.textureForMaterial) {
            mat.albedoTexture = options.textureForMaterial(mat.name);
            if (!mat.albedoTexture.empty()) mat.baseColor = glm::vec3(1.0f);
        }
        const bool neutral = std::max({ mat.baseColor.r, mat.baseColor.g, mat.baseColor.b }) -
                             std::min({ mat.baseColor.r, mat.baseColor.g, mat.baseColor.b }) < 0.1f;
        if (mat.albedoTexture.empty() && !options.fallbackTexture.empty() && (tex || (options.atlasForUntextured && neutral))) {
            mat.albedoTexture = options.fallbackTexture;
            if (!tex) mat.baseColor = glm::vec3(1.0f);
        }
        materialIndex[src] = static_cast<uint32_t>(model.materials.size());
        model.materials.push_back(std::move(mat));
    }
    if (model.materials.empty()) {
        ModelMaterial def;
        def.name = "default";
        model.materials.push_back(def);
    }

    // ---- skeleton: every node any skin cluster uses, plus their ancestors
    // up to (not including) the scene root, in parent-before-child order.
    std::unordered_set<const ufbx_node*> boneNodes;
    for (size_t s = 0; s < scene->skin_deformers.count; ++s) {
        const ufbx_skin_deformer* skin = scene->skin_deformers.data[s];
        for (size_t c = 0; c < skin->clusters.count; ++c) {
            for (const ufbx_node* n = skin->clusters.data[c]->bone_node; n && !n->is_root; n = n->parent) boneNodes.insert(n);
        }
    }
    std::unordered_map<const ufbx_node*, int> boneIndex;
    std::function<void(const ufbx_node*)> visit = [&](const ufbx_node* node) {
        if (boneNodes.count(node)) {
            ModelBone bone;
            bone.name = std::string(node->name.data, node->name.length);
            auto parentIt = node->parent ? boneIndex.find(node->parent) : boneIndex.end();
            bone.parent = parentIt != boneIndex.end() ? parentIt->second : -1;
            // A top-level bone carries its whole world transform, so any
            // non-bone ancestors (an armature root, the unit conversion)
            // are accounted for without being bones themselves.
            bone.localRest = toGlm(bone.parent >= 0 ? node->node_to_parent : node->node_to_world);
            boneIndex[node] = static_cast<int>(model.bones.size());
            model.bones.push_back(std::move(bone));
        }
        for (size_t i = 0; i < node->children.count; ++i) visit(node->children.data[i]);
    };
    if (scene->root_node) visit(scene->root_node);

    // ---- meshes
    for (size_t mi = 0; mi < scene->meshes.count; ++mi) {
        const ufbx_mesh* mesh = scene->meshes.data[mi];
        const ufbx_skin_deformer* skin = mesh->skin_deformers.count ? mesh->skin_deformers.data[0] : nullptr;
        std::vector<int> clusterToBone;
        if (skin) {
            for (size_t c = 0; c < skin->clusters.count; ++c) {
                const ufbx_skin_cluster* cluster = skin->clusters.data[c];
                auto it = boneIndex.find(cluster->bone_node);
                int b = it != boneIndex.end() ? it->second : -1;
                clusterToBone.push_back(b);
                if (b >= 0) model.bones[b].inverseBind = toGlm(cluster->geometry_to_bone);
            }
        }

        // Static meshes are baked into model space once per node that uses
        // them (FBX instancing); skinned meshes stay in geometry space.
        std::vector<glm::mat4> placements;
        if (skin) {
            placements.push_back(glm::mat4(1.0f));
        } else {
            for (size_t i = 0; i < mesh->instances.count; ++i) placements.push_back(toGlm(mesh->instances.data[i]->geometry_to_world));
            if (placements.empty()) placements.push_back(glm::mat4(1.0f));
        }

        std::vector<uint32_t> triIndices(mesh->max_face_triangles * 3);
        for (const glm::mat4& placement : placements) {
            const glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(placement)));
            const bool flipWinding = glm::determinant(glm::mat3(placement)) < 0.0f;
            for (size_t pi = 0; pi < mesh->material_parts.count; ++pi) {
                const ufbx_mesh_part& part = mesh->material_parts.data[pi];
                if (part.num_triangles == 0) continue;
                ModelMesh out;
                out.name = std::string(mesh->name.data, mesh->name.length);
                if (out.name.empty() && mesh->instances.count) out.name = std::string(mesh->instances.data[0]->name.data, mesh->instances.data[0]->name.length);
                out.skinned = skin != nullptr;
                const ufbx_material* srcMat = part.index < mesh->materials.count ? mesh->materials.data[part.index] : nullptr;
                auto matIt = srcMat ? materialIndex.find(srcMat) : materialIndex.end();
                out.material = matIt != materialIndex.end() ? matIt->second : 0;

                std::unordered_map<CornerKey, uint32_t, CornerKeyHash> dedup;
                for (size_t fi = 0; fi < part.face_indices.count; ++fi) {
                    ufbx_face face = mesh->faces.data[part.face_indices.data[fi]];
                    uint32_t numTris = ufbx_triangulate_face(triIndices.data(), triIndices.size(), mesh, face);
                    for (uint32_t t = 0; t < numTris; ++t) {
                        uint32_t tri[3] = { triIndices[t * 3], triIndices[t * 3 + 1], triIndices[t * 3 + 2] };
                        if (flipWinding) std::swap(tri[1], tri[2]);
                        for (uint32_t ix : tri) {
                            CornerKey key{ mesh->vertex_position.indices.data[ix],
                                           mesh->vertex_normal.exists ? mesh->vertex_normal.indices.data[ix] : 0u,
                                           mesh->vertex_uv.exists ? mesh->vertex_uv.indices.data[ix] : 0u };
                            auto [it, inserted] = dedup.try_emplace(key, static_cast<uint32_t>(out.vertices.size()));
                            if (inserted) {
                                ModelVertex v;
                                ufbx_vec3 p = ufbx_get_vertex_vec3(&mesh->vertex_position, ix);
                                v.position = glm::vec3(placement * glm::vec4(p.x, p.y, p.z, 1.0f));
                                if (mesh->vertex_normal.exists) {
                                    ufbx_vec3 n = ufbx_get_vertex_vec3(&mesh->vertex_normal, ix);
                                    glm::vec3 nn = normalMatrix * glm::vec3(n.x, n.y, n.z);
                                    float len = glm::length(nn);
                                    v.normal = len > 1e-8f ? nn / len : glm::vec3(0, 1, 0);
                                }
                                if (mesh->vertex_uv.exists) {
                                    ufbx_vec2 uv = ufbx_get_vertex_vec2(&mesh->vertex_uv, ix);
                                    v.uv = glm::vec2(uv.x, 1.0f - uv.y); // FBX is bottom-left origin, Vulkan top-left
                                }
                                if (skin) {
                                    const ufbx_skin_vertex& sv = skin->vertices.data[mesh->vertex_indices.data[ix]];
                                    // Keep the 4 strongest influences (of any number), renormalized.
                                    std::array<std::pair<float, int>, 4> top{}; // strongest first
                                    for (uint32_t w = 0; w < sv.num_weights; ++w) {
                                        const ufbx_skin_weight& sw = skin->weights.data[sv.weight_begin + w];
                                        int bone = sw.cluster_index < clusterToBone.size() ? clusterToBone[sw.cluster_index] : -1;
                                        std::pair<float, int> cand{ bone >= 0 ? static_cast<float>(sw.weight) : 0.0f, std::max(bone, 0) };
                                        if (cand.first <= top[3].first) continue;
                                        size_t k = 3;
                                        for (; k > 0 && top[k - 1].first < cand.first; --k) top[k] = top[k - 1];
                                        top[k] = cand;
                                    }
                                    float total = 0.0f;
                                    for (const auto& t : top) total += t.first;
                                    for (uint32_t w = 0; w < 4; ++w) {
                                        bool used = total > 0.0f;
                                        v.joints[w] = used ? static_cast<uint32_t>(top[w].second) : 0u;
                                        v.weights[w] = used ? top[w].first / total : 0.0f;
                                    }
                                    if (total <= 0.0f) v.weights[0] = 1.0f; // unweighted vertex: follow bone 0
                                }
                                out.vertices.push_back(v);
                            }
                            out.indices.push_back(it->second);
                        }
                    }
                }
                if (!out.indices.empty()) model.meshes.push_back(std::move(out));
            }
        }
    }
    if (model.meshes.empty() && !options.allowNoMeshes) throw std::runtime_error("loadModel: '" + path + "' contains no triangle meshes");

    // ---- animations (skinned models only), sampled per bone
    if (options.loadAnimations && !model.bones.empty()) {
        std::vector<const ufbx_node*> nodeOfBone(model.bones.size(), nullptr);
        for (auto& [node, index] : boneIndex) nodeOfBone[index] = node;
        for (size_t ai = 0; ai < scene->anim_stacks.count; ++ai) {
            const ufbx_anim_stack* stack = scene->anim_stacks.data[ai];
            ModelAnimation anim;
            anim.name = std::string(stack->name.data, stack->name.length);
            anim.sampleRate = options.animationSampleRate;
            anim.duration = static_cast<float>(stack->time_end - stack->time_begin);
            int frameCount = std::max(1, static_cast<int>(anim.duration * anim.sampleRate + 0.5f) + 1);
            anim.frames.resize(frameCount);
            for (int f = 0; f < frameCount; ++f) {
                double t = stack->time_begin + f / static_cast<double>(anim.sampleRate);
                auto& locals = anim.frames[f];
                locals.resize(model.bones.size());
                for (size_t b = 0; b < model.bones.size(); ++b) {
                    const ufbx_node* node = nodeOfBone[b];
                    ufbx_transform tr = ufbx_evaluate_transform(stack->anim, node, t);
                    glm::mat4 local = toGlm(ufbx_transform_to_matrix(&tr));
                    // Top-level bones: prepend the (static) world transform
                    // of their non-bone parent, matching localRest.
                    if (model.bones[b].parent < 0 && node->parent) local = toGlm(node->parent->node_to_world) * local;
                    locals[b] = local;
                }
            }
            model.animations.push_back(std::move(anim));
        }
    }

    // ---- bounds (rest pose for skinned meshes)
    bool first = true;
    auto grow = [&](const glm::vec3& p) {
        if (first) { model.boundsMin = model.boundsMax = p; first = false; }
        model.boundsMin = glm::min(model.boundsMin, p);
        model.boundsMax = glm::max(model.boundsMax, p);
    };
    std::vector<glm::mat4> restSkin;
    if (!model.bones.empty()) {
        std::vector<glm::mat4> locals(model.bones.size());
        for (size_t b = 0; b < model.bones.size(); ++b) locals[b] = model.bones[b].localRest;
        restSkin = computeSkinMatrices(model, locals);
    }
    for (const ModelMesh& m : model.meshes) {
        for (const ModelVertex& v : m.vertices) {
            if (m.skinned) {
                glm::vec3 p, n;
                skinVertex(v, restSkin, p, n);
                grow(p);
            } else {
                grow(v.position);
            }
        }
    }
    if (options.fixUnitMismatch && model.bones.empty() && !first && std::abs(scene->settings.unit_meters - 0.01) < 1e-6) {
        const glm::vec3 size = model.boundsMax - model.boundsMin;
        if (std::max(size.x, std::max(size.y, size.z)) < options.tinyModel) {
            for (ModelMesh& m : model.meshes)
                for (ModelVertex& v : m.vertices) v.position *= 100.0f;
            model.boundsMin *= 100.0f;
            model.boundsMax *= 100.0f;
        }
    }
    return model;
}

std::vector<glm::mat4> computeRestPose(const ModelData& model) {
    std::vector<glm::mat4> world(model.bones.size());
    for (size_t b = 0; b < model.bones.size(); ++b) {
        int p = model.bones[b].parent;
        world[b] = p >= 0 ? world[p] * model.bones[b].localRest : model.bones[b].localRest;
    }
    return world;
}

std::vector<glm::mat4> computeSkinMatrices(const ModelData& model, const std::vector<glm::mat4>& locals) {
    std::vector<glm::mat4> world(model.bones.size()), skin(model.bones.size());
    for (size_t b = 0; b < model.bones.size(); ++b) {
        int p = model.bones[b].parent;
        world[b] = p >= 0 ? world[p] * locals[b] : locals[b];
        skin[b] = world[b] * model.bones[b].inverseBind;
    }
    return skin;
}

void skinVertex(const ModelVertex& in, const std::vector<glm::mat4>& skinMatrices, glm::vec3& outPosition, glm::vec3& outNormal) {
    glm::vec3 p(0.0f), n(0.0f);
    for (int w = 0; w < 4; ++w) {
        float weight = in.weights[w];
        if (weight <= 0.0f || in.joints[w] >= skinMatrices.size()) continue;
        const glm::mat4& m = skinMatrices[in.joints[w]];
        p += weight * glm::vec3(m * glm::vec4(in.position, 1.0f));
        n += weight * glm::mat3(m) * in.normal;
    }
    float len = glm::length(n);
    outPosition = p;
    outNormal = len > 1e-8f ? n / len : in.normal;
}

} // namespace kke
