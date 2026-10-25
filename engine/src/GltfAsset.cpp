// glTF 2.0 import (.gltf + .bin + images, or one .glb) through cgltf,
// into the same kke::ModelData the FBX/OBJ loader makes (kke/ModelAsset.h).
//
// What it reads: triangle meshes (indexed or not, strips and fans too,
// quantized and sparse accessors), the metallic-roughness material
// (base colour, metallic and roughness factors and maps, normal map),
// skins (JOINTS_0/WEIGHTS_0, inverse bind matrices) and skeletal
// animations (translation/rotation/scale; step, linear and cubic-spline
// keys), sampled at options.animationSampleRate like FBX clips.
//
// Images inside the file (a .glb's buffer views, base64 data URIs) become
// in-memory files named "<model path>#image<N>.<ext>" (kke::cooked::
// putMemoryFile), so the texture loaders read them like any file. The
// engine reads roughness and metallic maps from their red channel, while
// glTF packs them into one image (green = roughness, blue = metallic), so
// that image is split into two grey images, "#image<N>.roughness.pgm" and
// "#image<N>.metallic.pgm".
//
// Not read (yet): morph targets, node animation without a skin, more than
// four influences per vertex (JOINTS_1), TEXCOORD_1, KHR_texture_transform,
// emissive, alpha modes and the KHR_materials_* extensions. Files that
// need Draco or meshopt compression, or KTX2/WebP images only, say so in
// the error instead of loading wrong.

#include "ModelAssetDetail.h"
#include "kke/CookedFile.h"

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

// The implementation lives in RmlVulkanRenderInterface.cpp.
#include <stb_image.h>

#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace kke::detail {

namespace fs = std::filesystem;

namespace {

[[noreturn]] void fail(const std::string& path, const std::string& why) {
    throw std::runtime_error("loadModel: '" + path + "': " + why);
}

const char* resultText(cgltf_result r) {
    switch (r) {
    case cgltf_result_success: return "ok";
    case cgltf_result_data_too_short: return "the file is cut short";
    case cgltf_result_unknown_format: return "not a glTF file";
    case cgltf_result_invalid_json: return "invalid JSON";
    case cgltf_result_invalid_gltf: return "not valid glTF 2.0";
    case cgltf_result_invalid_options: return "invalid options";
    case cgltf_result_file_not_found: return "a file it refers to is missing";
    case cgltf_result_io_error: return "a file it refers to can't be read";
    case cgltf_result_out_of_memory: return "out of memory";
    case cgltf_result_legacy_gltf: return "glTF 1.0 (only 2.0 is supported)";
    default: return "unknown error";
    }
}

// cgltf reads the .bin files and images a .gltf names through this, so
// cooked art (docs/COOKED_ART.md) and in-memory files load too.
cgltf_result readFile(const cgltf_memory_options*, const cgltf_file_options*, const char* path, cgltf_size* size, void** data) {
    std::vector<uint8_t> bytes;
    if (!cooked::readAssetFile(path, bytes)) return cgltf_result_file_not_found;
    void* out = std::malloc(bytes.empty() ? 1 : bytes.size());
    if (!out) return cgltf_result_out_of_memory;
    if (!bytes.empty()) std::memcpy(out, bytes.data(), bytes.size());
    *size = bytes.size();
    *data = out;
    return cgltf_result_success;
}

void releaseFile(const cgltf_memory_options*, const cgltf_file_options*, void* data) { std::free(data); }

std::vector<float> unpackFloats(const cgltf_accessor* a) {
    std::vector<float> out(a->count * cgltf_num_components(a->type));
    if (!out.empty()) cgltf_accessor_unpack_floats(a, out.data(), out.size());
    return out;
}

const cgltf_accessor* attribute(const cgltf_primitive& prim, cgltf_attribute_type type, int index = 0) {
    for (cgltf_size i = 0; i < prim.attributes_count; ++i) {
        const cgltf_attribute& a = prim.attributes[i];
        if (a.type == type && a.index == index) return a.data;
    }
    return nullptr;
}

std::string extensionFor(const cgltf_image* img, const std::string& uri) {
    const std::string mime = img->mime_type ? img->mime_type : "";
    if (mime == "image/png") return ".png";
    if (mime == "image/jpeg") return ".jpg";
    if (mime == "image/ktx2") return ".ktx2";
    if (mime == "image/webp") return ".webp";
    if (uri.rfind("data:image/png", 0) == 0) return ".png";
    if (uri.rfind("data:image/jpeg", 0) == 0) return ".jpg";
    return ".png";
}

// "P5" grey image: stb_image reads it, and the engine reads a roughness or
// metallic map's red channel, which a grey image fills.
std::vector<uint8_t> greyImage(const uint8_t* rgba, int w, int h, int channel) {
    const std::string header = "P5\n" + std::to_string(w) + " " + std::to_string(h) + "\n255\n";
    std::vector<uint8_t> out(header.begin(), header.end());
    out.reserve(out.size() + static_cast<size_t>(w) * static_cast<size_t>(h));
    for (size_t i = 0, n = static_cast<size_t>(w) * static_cast<size_t>(h); i < n; ++i) out.push_back(rgba[i * 4 + channel]);
    return out;
}

class Importer {
public:
    Importer(const std::string& path, const ModelLoadOptions& options, cgltf_data* data)
        : m_path(path), m_options(options), m_data(data), m_dirs(textureSearchDirs(path, options)) {}

    ModelData run() {
        m_model.sourcePath = m_path;
        readMaterials();
        collectRoots();
        readSkeleton();
        for (const cgltf_node* root : m_roots) readMeshes(root);
        if (m_model.meshes.empty() && !m_options.allowNoMeshes) fail(m_path, "contains no triangle meshes");
        if (m_model.materials.empty()) defaultMaterial(); // kke::ModelData always has one
        if (m_options.loadAnimations && !m_model.bones.empty()) readAnimations();
        finishModel(m_model, m_options);
        return std::move(m_model);
    }

private:
    // ---- images and materials

    // The image's bytes as a file the texture loaders can open: the file
    // next to the model for a .gltf's uri, an in-memory file otherwise.
    // "" when it can't be found or isn't an image stb_image reads.
    std::string imageFile(const cgltf_image* img) {
        if (!img) return {};
        const cgltf_size index = static_cast<cgltf_size>(img - m_data->images);
        if (auto it = m_imageFiles.find(index); it != m_imageFiles.end()) return it->second;
        std::string file;
        const std::string uri = img->uri ? img->uri : "";
        const std::string ext = extensionFor(img, uri);
        if (ext == ".ktx2" || ext == ".webp") {
            // stb_image reads neither; KTX2 is ENG-F60.
        } else if (img->buffer_view) {
            const uint8_t* bytes = cgltf_buffer_view_data(img->buffer_view);
            if (bytes) {
                file = m_path + "#image" + std::to_string(index) + ext;
                cooked::putMemoryFile(file, std::vector<uint8_t>(bytes, bytes + img->buffer_view->size));
            }
        } else if (uri.rfind("data:", 0) == 0) {
            const size_t comma = uri.find(";base64,");
            if (comma != std::string::npos) {
                const char* base64 = uri.c_str() + comma + 8;
                const size_t chars = std::strlen(base64);
                size_t pad = 0;
                while (pad < 2 && chars > pad && base64[chars - 1 - pad] == '=') ++pad;
                // Shorter than one 4-character group: nothing to decode
                // (and chars / 4 * 3 - pad would wrap around).
                const cgltf_size size = chars >= 4 ? chars / 4 * 3 - pad : 0;
                void* decoded = nullptr;
                cgltf_options opts{};
                if (size > 0 && cgltf_load_buffer_base64(&opts, size, base64, &decoded) == cgltf_result_success && decoded) {
                    const auto* b = static_cast<const uint8_t*>(decoded);
                    file = m_path + "#image" + std::to_string(index) + ext;
                    cooked::putMemoryFile(file, std::vector<uint8_t>(b, b + size));
                    std::free(decoded);
                }
            }
        } else if (!uri.empty()) {
            std::string decoded = uri;
            decoded.resize(cgltf_decode_uri(decoded.data()));
            const fs::path direct = fs::path(m_path).parent_path() / fs::path(reinterpret_cast<const char8_t*>(decoded.c_str()));
            std::error_code ec;
            if (cooked::hasMemoryFile(direct.string()) || fs::is_regular_file(direct, ec)) file = direct.string();
            else file = findTextureFile(decoded, m_dirs);
        }
        m_imageFiles[index] = file;
        return file;
    }

    static const cgltf_image* imageOf(const cgltf_texture_view& view) {
        return view.texture ? view.texture->image : nullptr;
    }

    // The packed metallic-roughness image split into its two channels.
    std::pair<std::string, std::string> roughnessMetallicFiles(const cgltf_image* img) {
        if (!img) return {};
        const cgltf_size index = static_cast<cgltf_size>(img - m_data->images);
        if (auto it = m_splitFiles.find(index); it != m_splitFiles.end()) return it->second;
        std::pair<std::string, std::string> files;
        const std::string source = imageFile(img);
        std::vector<uint8_t> bytes;
        if (!source.empty() && cooked::readAssetFile(source, bytes)) {
            int w = 0, h = 0, n = 0;
            if (stbi_uc* px = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, &n, 4)) {
                const std::string base = m_path + "#image" + std::to_string(index);
                files.first = base + ".roughness.pgm";
                files.second = base + ".metallic.pgm";
                cooked::putMemoryFile(files.first, greyImage(px, w, h, 1));
                cooked::putMemoryFile(files.second, greyImage(px, w, h, 2));
                stbi_image_free(px);
            }
        }
        m_splitFiles[index] = files;
        return files;
    }

    void readMaterials() {
        for (cgltf_size i = 0; i < m_data->materials_count; ++i) {
            const cgltf_material& src = m_data->materials[i];
            ModelMaterial mat;
            mat.name = src.name ? src.name : "material" + std::to_string(i);
            const cgltf_image* albedo = nullptr;
            if (src.has_pbr_metallic_roughness) {
                const cgltf_pbr_metallic_roughness& pbr = src.pbr_metallic_roughness;
                mat.baseColor = glm::vec3(pbr.base_color_factor[0], pbr.base_color_factor[1], pbr.base_color_factor[2]);
                mat.metallic = pbr.metallic_factor;
                mat.roughness = pbr.roughness_factor;
                albedo = imageOf(pbr.base_color_texture);
                if (!albedo && pbr.base_color_texture.texture) {
                    // KTX2 or WebP only: nothing stb_image reads.
                    const cgltf_texture* t = pbr.base_color_texture.texture;
                    const cgltf_image* other = t->has_basisu ? t->basisu_image : t->webp_image;
                    if (other && other->uri) mat.albedoTextureOriginal = other->uri;
                    else mat.albedoTextureOriginal = t->has_basisu ? "(KTX2 image)" : "(WebP image)";
                }
                // Factors stay as they are: in glTF a map multiplies its
                // factor, which is what the engine does too.
                const auto [rough, metal] = roughnessMetallicFiles(imageOf(pbr.metallic_roughness_texture));
                mat.roughnessTexture = rough;
                mat.metallicTexture = metal;
            } else {
                mat.roughness = 1.0f; // glTF's default for a material with no PBR block
                mat.metallic = 1.0f;
            }
            if (albedo) {
                mat.albedoTextureOriginal = albedo->uri && std::strncmp(albedo->uri, "data:", 5) != 0
                                                ? albedo->uri
                                                : (albedo->name ? albedo->name : "embedded image");
                mat.albedoTexture = imageFile(albedo);
            }
            mat.normalTexture = imageFile(imageOf(src.normal_texture));
            applyTextureOptions(mat, albedo != nullptr || !mat.albedoTextureOriginal.empty(), m_options);
            m_model.materials.push_back(std::move(mat));
        }
    }

    // Primitives without a material use glTF's default one, added after the
    // file's own (so their indices stay the file's) the first time it's needed.
    uint32_t defaultMaterial() {
        if (m_defaultMaterial == kNone) {
            ModelMaterial def;
            def.name = "default";
            def.roughness = 1.0f;
            def.metallic = 1.0f;
            applyTextureOptions(def, false, m_options);
            m_defaultMaterial = static_cast<uint32_t>(m_model.materials.size());
            m_model.materials.push_back(def);
        }
        return m_defaultMaterial;
    }

    // ---- scene graph

    void collectRoots() {
        const cgltf_scene* scene = m_data->scene ? m_data->scene : (m_data->scenes_count ? &m_data->scenes[0] : nullptr);
        if (scene) {
            for (cgltf_size i = 0; i < scene->nodes_count; ++i) m_roots.push_back(scene->nodes[i]);
        } else {
            for (cgltf_size i = 0; i < m_data->nodes_count; ++i)
                if (!m_data->nodes[i].parent) m_roots.push_back(&m_data->nodes[i]);
        }
    }

    static glm::mat4 localMatrix(const cgltf_node* n) {
        glm::mat4 m(1.0f);
        cgltf_node_transform_local(n, glm::value_ptr(m));
        return m;
    }

    static glm::mat4 worldMatrix(const cgltf_node* n) {
        glm::mat4 m(1.0f);
        cgltf_node_transform_world(n, glm::value_ptr(m));
        return m;
    }

    // Every joint of every skin and their ancestors, parents first. A
    // top-level bone is a scene root, so its local transform is its world
    // transform, as kke::ModelBone expects.
    void readSkeleton() {
        std::unordered_set<const cgltf_node*> boneNodes;
        for (cgltf_size s = 0; s < m_data->skins_count; ++s) {
            const cgltf_skin& skin = m_data->skins[s];
            for (cgltf_size j = 0; j < skin.joints_count; ++j)
                for (const cgltf_node* n = skin.joints[j]; n; n = n->parent) boneNodes.insert(n);
        }
        // An animation-only file: the nodes its clips move are the skeleton.
        if (boneNodes.empty() && m_options.allowNoMeshes) {
            for (cgltf_size a = 0; a < m_data->animations_count; ++a) {
                const cgltf_animation& anim = m_data->animations[a];
                for (cgltf_size c = 0; c < anim.channels_count; ++c)
                    for (const cgltf_node* n = anim.channels[c].target_node; n; n = n->parent) boneNodes.insert(n);
            }
        }
        if (boneNodes.empty()) return;
        std::function<void(const cgltf_node*)> visit = [&](const cgltf_node* node) {
            if (boneNodes.count(node)) {
                ModelBone bone;
                const cgltf_size index = static_cast<cgltf_size>(node - m_data->nodes);
                bone.name = node->name ? node->name : "node" + std::to_string(index);
                auto parentIt = node->parent ? m_boneIndex.find(node->parent) : m_boneIndex.end();
                bone.parent = parentIt != m_boneIndex.end() ? parentIt->second : -1;
                bone.localRest = bone.parent >= 0 ? localMatrix(node) : worldMatrix(node);
                m_boneIndex[node] = static_cast<int>(m_model.bones.size());
                m_boneNode.push_back(node);
                m_model.bones.push_back(std::move(bone));
            }
            for (cgltf_size i = 0; i < node->children_count; ++i) visit(node->children[i]);
        };
        for (const cgltf_node* root : m_roots) visit(root);
        // Inverse bind matrices; a joint without one binds at identity.
        for (cgltf_size s = 0; s < m_data->skins_count; ++s) {
            const cgltf_skin& skin = m_data->skins[s];
            std::vector<float> ibm;
            if (skin.inverse_bind_matrices) ibm = unpackFloats(skin.inverse_bind_matrices);
            for (cgltf_size j = 0; j < skin.joints_count; ++j) {
                auto it = m_boneIndex.find(skin.joints[j]);
                if (it == m_boneIndex.end()) continue;
                if (ibm.size() >= (j + 1) * 16) m_model.bones[static_cast<size_t>(it->second)].inverseBind = glm::make_mat4(&ibm[j * 16]);
            }
        }
    }

    // ---- meshes

    void readMeshes(const cgltf_node* node) {
        if (node->mesh) {
            const glm::mat4 placement = node->skin ? glm::mat4(1.0f) : worldMatrix(node);
            for (cgltf_size p = 0; p < node->mesh->primitives_count; ++p) readPrimitive(*node, node->mesh->primitives[p], placement);
        }
        for (cgltf_size i = 0; i < node->children_count; ++i) readMeshes(node->children[i]);
    }

    static std::vector<uint32_t> triangleList(const cgltf_primitive& prim, size_t vertexCount) {
        std::vector<uint32_t> idx;
        if (prim.indices) {
            idx.resize(prim.indices->count);
            if (!idx.empty()) cgltf_accessor_unpack_indices(prim.indices, idx.data(), sizeof(uint32_t), idx.size());
        } else {
            idx.resize(vertexCount);
            for (size_t i = 0; i < vertexCount; ++i) idx[i] = static_cast<uint32_t>(i);
        }
        std::vector<uint32_t> tris;
        if (prim.type == cgltf_primitive_type_triangles) {
            idx.resize(idx.size() / 3 * 3);
            return idx;
        }
        if (prim.type == cgltf_primitive_type_triangle_strip) {
            for (size_t i = 2; i < idx.size(); ++i) {
                // Every other triangle of a strip is wound the other way.
                if (i % 2 == 0) tris.insert(tris.end(), { idx[i - 2], idx[i - 1], idx[i] });
                else tris.insert(tris.end(), { idx[i - 1], idx[i - 2], idx[i] });
            }
        } else if (prim.type == cgltf_primitive_type_triangle_fan) {
            for (size_t i = 2; i < idx.size(); ++i) tris.insert(tris.end(), { idx[0], idx[i - 1], idx[i] });
        }
        return tris;
    }

    void readPrimitive(const cgltf_node& node, const cgltf_primitive& prim, const glm::mat4& placement) {
        if (prim.type != cgltf_primitive_type_triangles && prim.type != cgltf_primitive_type_triangle_strip &&
            prim.type != cgltf_primitive_type_triangle_fan)
            return; // points and lines draw nothing in a triangle renderer
        const cgltf_accessor* posAcc = attribute(prim, cgltf_attribute_type_position);
        if (!posAcc || posAcc->count == 0) return;
        const std::vector<float> pos = unpackFloats(posAcc);
        const cgltf_accessor* normAcc = attribute(prim, cgltf_attribute_type_normal);
        const cgltf_accessor* uvAcc = attribute(prim, cgltf_attribute_type_texcoord);
        const std::vector<float> nrm = normAcc && normAcc->count == posAcc->count ? unpackFloats(normAcc) : std::vector<float>{};
        const std::vector<float> uv = uvAcc && uvAcc->count == posAcc->count ? unpackFloats(uvAcc) : std::vector<float>{};
        const bool skinned = node.skin != nullptr;
        const cgltf_accessor* jointAcc = skinned ? attribute(prim, cgltf_attribute_type_joints) : nullptr;
        const cgltf_accessor* weightAcc = skinned ? attribute(prim, cgltf_attribute_type_weights) : nullptr;
        const std::vector<float> wts = weightAcc && weightAcc->count == posAcc->count ? unpackFloats(weightAcc) : std::vector<float>{};
        std::vector<int> jointToBone;
        if (skinned) {
            for (cgltf_size j = 0; j < node.skin->joints_count; ++j) {
                auto it = m_boneIndex.find(node.skin->joints[j]);
                jointToBone.push_back(it != m_boneIndex.end() ? it->second : -1);
            }
        }

        const glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(placement)));
        const bool flipWinding = glm::determinant(glm::mat3(placement)) < 0.0f;
        auto makeVertex = [&](uint32_t i) {
            ModelVertex v;
            v.position = glm::vec3(placement * glm::vec4(pos[i * 3], pos[i * 3 + 1], pos[i * 3 + 2], 1.0f));
            if (!nrm.empty()) {
                const glm::vec3 n = normalMatrix * glm::vec3(nrm[i * 3], nrm[i * 3 + 1], nrm[i * 3 + 2]);
                const float len = glm::length(n);
                v.normal = len > 1e-8f ? n / len : glm::vec3(0, 1, 0);
            }
            if (!uv.empty()) v.uv = glm::vec2(uv[i * 2], uv[i * 2 + 1]); // glTF's origin is top-left, like Vulkan
            if (skinned) {
                std::array<std::pair<float, int>, 4> top{};
                if (jointAcc && !wts.empty()) {
                    cgltf_uint joints[4] = { 0, 0, 0, 0 };
                    cgltf_accessor_read_uint(jointAcc, i, joints, 4);
                    for (int k = 0; k < 4; ++k) {
                        const int bone = joints[k] < jointToBone.size() ? jointToBone[joints[k]] : -1;
                        top[static_cast<size_t>(k)] = { bone >= 0 ? std::max(wts[i * 4 + static_cast<size_t>(k)], 0.0f) : 0.0f, std::max(bone, 0) };
                    }
                }
                float total = 0.0f;
                for (const auto& t : top) total += t.first;
                for (int k = 0; k < 4; ++k) {
                    v.joints[k] = total > 0.0f ? static_cast<uint32_t>(top[static_cast<size_t>(k)].second) : 0u;
                    v.weights[k] = total > 0.0f ? top[static_cast<size_t>(k)].first / total : 0.0f;
                }
                if (total <= 0.0f) v.weights[0] = 1.0f; // unweighted vertex: follow bone 0
            }
            return v;
        };

        ModelMesh out;
        out.name = node.mesh->name ? node.mesh->name : (node.name ? node.name : "mesh");
        out.skinned = skinned;
        out.material = prim.material ? static_cast<uint32_t>(prim.material - m_data->materials) : defaultMaterial();
        std::vector<uint32_t> tris = triangleList(prim, posAcc->count);
        if (flipWinding)
            for (size_t t = 0; t + 2 < tris.size(); t += 3) std::swap(tris[t + 1], tris[t + 2]);
        if (!nrm.empty()) {
            out.vertices.reserve(posAcc->count);
            for (uint32_t i = 0; i < posAcc->count; ++i) out.vertices.push_back(makeVertex(i));
            out.indices.reserve(tris.size());
            for (uint32_t i : tris)
                if (i < posAcc->count) out.indices.push_back(i);
            out.indices.resize(out.indices.size() / 3 * 3);
        } else {
            // No normals: glTF asks for flat ones, so every triangle gets
            // its own three corners.
            for (size_t t = 0; t + 2 < tris.size(); t += 3) {
                if (tris[t] >= posAcc->count || tris[t + 1] >= posAcc->count || tris[t + 2] >= posAcc->count) continue;
                ModelVertex c[3] = { makeVertex(tris[t]), makeVertex(tris[t + 1]), makeVertex(tris[t + 2]) };
                const glm::vec3 n = glm::cross(c[1].position - c[0].position, c[2].position - c[0].position);
                const float len = glm::length(n);
                for (ModelVertex& v : c) {
                    v.normal = len > 1e-12f ? n / len : glm::vec3(0, 1, 0);
                    out.indices.push_back(static_cast<uint32_t>(out.vertices.size()));
                    out.vertices.push_back(v);
                }
            }
        }
        if (!out.indices.empty()) m_model.meshes.push_back(std::move(out));
    }

    // ---- animations

    struct Channel {
        const cgltf_animation_sampler* sampler = nullptr;
        std::vector<float> times, values;
    };
    struct BoneTracks {
        Channel translation, rotation, scale;
    };

    // The value of one channel at time t: `width` floats (3 or 4).
    static void sample(const Channel& c, float t, int width, bool isRotation, float* out) {
        const std::vector<float>& tm = c.times;
        const bool cubic = c.sampler->interpolation == cgltf_interpolation_type_cubic_spline;
        const size_t stride = cubic ? 3u : 1u; // cubic: in-tangent, value, out-tangent per key
        auto value = [&](size_t key, size_t part) { return &c.values[(key * stride + part) * static_cast<size_t>(width)]; };
        const size_t valuePart = cubic ? 1u : 0u;
        if (t <= tm.front() || tm.size() == 1) {
            std::copy_n(value(0, valuePart), width, out);
            return;
        }
        if (t >= tm.back()) {
            std::copy_n(value(tm.size() - 1, valuePart), width, out);
            return;
        }
        const size_t k = static_cast<size_t>(std::upper_bound(tm.begin(), tm.end(), t) - tm.begin()) - 1;
        const float dt = tm[k + 1] - tm[k];
        const float u = dt > 0.0f ? (t - tm[k]) / dt : 0.0f;
        if (c.sampler->interpolation == cgltf_interpolation_type_step) {
            std::copy_n(value(k, 0), width, out);
            return;
        }
        if (cubic) {
            const float u2 = u * u, u3 = u2 * u;
            const float h00 = 2 * u3 - 3 * u2 + 1, h10 = u3 - 2 * u2 + u, h01 = -2 * u3 + 3 * u2, h11 = u3 - u2;
            const float* p0 = value(k, 1);
            const float* m0 = value(k, 2);
            const float* p1 = value(k + 1, 1);
            const float* m1 = value(k + 1, 0);
            for (int i = 0; i < width; ++i) out[i] = h00 * p0[i] + h10 * dt * m0[i] + h01 * p1[i] + h11 * dt * m1[i];
            if (isRotation) {
                glm::quat q(out[3], out[0], out[1], out[2]);
                q = glm::normalize(q);
                out[0] = q.x, out[1] = q.y, out[2] = q.z, out[3] = q.w;
            }
            return;
        }
        const float* a = value(k, 0);
        const float* b = value(k + 1, 0);
        if (isRotation) {
            const glm::quat qa(a[3], a[0], a[1], a[2]), qb(b[3], b[0], b[1], b[2]);
            const glm::quat q = glm::slerp(qa, qb, u);
            out[0] = q.x, out[1] = q.y, out[2] = q.z, out[3] = q.w;
            return;
        }
        for (int i = 0; i < width; ++i) out[i] = a[i] + (b[i] - a[i]) * u;
    }

    void readAnimations() {
        for (cgltf_size ai = 0; ai < m_data->animations_count; ++ai) {
            const cgltf_animation& src = m_data->animations[ai];
            std::vector<BoneTracks> tracks(m_model.bones.size());
            float begin = 0.0f, end = 0.0f;
            bool any = false;
            for (cgltf_size ci = 0; ci < src.channels_count; ++ci) {
                const cgltf_animation_channel& ch = src.channels[ci];
                auto it = ch.target_node ? m_boneIndex.find(ch.target_node) : m_boneIndex.end();
                if (it == m_boneIndex.end() || !ch.sampler || !ch.sampler->input || !ch.sampler->output) continue;
                Channel* dst = nullptr;
                BoneTracks& bt = tracks[static_cast<size_t>(it->second)];
                if (ch.target_path == cgltf_animation_path_type_translation) dst = &bt.translation;
                else if (ch.target_path == cgltf_animation_path_type_rotation) dst = &bt.rotation;
                else if (ch.target_path == cgltf_animation_path_type_scale) dst = &bt.scale;
                if (!dst) continue; // morph weights: not read yet
                dst->sampler = ch.sampler;
                dst->times = unpackFloats(ch.sampler->input);
                dst->values = unpackFloats(ch.sampler->output);
                const size_t width = ch.target_path == cgltf_animation_path_type_rotation ? 4u : 3u;
                const size_t stride = ch.sampler->interpolation == cgltf_interpolation_type_cubic_spline ? 3u : 1u;
                if (dst->times.empty() || dst->values.size() < dst->times.size() * stride * width) {
                    *dst = Channel{};
                    continue;
                }
                begin = any ? std::min(begin, dst->times.front()) : dst->times.front();
                end = any ? std::max(end, dst->times.back()) : dst->times.back();
                any = true;
            }
            if (!any) continue;
            ModelAnimation anim;
            anim.name = src.name ? src.name : "animation" + std::to_string(ai);
            anim.sampleRate = m_options.animationSampleRate;
            anim.duration = end - begin;
            const int frameCount = std::max(1, static_cast<int>(anim.duration * anim.sampleRate + 0.5f) + 1);
            anim.frames.resize(static_cast<size_t>(frameCount));
            for (int f = 0; f < frameCount; ++f) {
                const float t = begin + static_cast<float>(f) / anim.sampleRate;
                std::vector<glm::mat4>& locals = anim.frames[static_cast<size_t>(f)];
                locals.resize(m_model.bones.size());
                for (size_t b = 0; b < m_model.bones.size(); ++b) {
                    const cgltf_node* node = m_boneNode[b];
                    const BoneTracks& bt = tracks[b];
                    if (!bt.translation.sampler && !bt.rotation.sampler && !bt.scale.sampler) {
                        locals[b] = m_model.bones[b].localRest;
                        continue;
                    }
                    // The node's own rest TRS, then the animated parts over it.
                    float tr[3] = { 0, 0, 0 }, rot[4] = { 0, 0, 0, 1 }, sc[3] = { 1, 1, 1 };
                    if (node->has_matrix) {
                        const glm::mat4 m = glm::make_mat4(node->matrix);
                        const glm::vec3 col0(m[0]), col1(m[1]), col2(m[2]);
                        glm::vec3 s(glm::length(col0), glm::length(col1), glm::length(col2));
                        if (glm::determinant(glm::mat3(m)) < 0.0f) s.x = -s.x;
                        const glm::quat q = glm::quat_cast(glm::mat3(col0 / s.x, col1 / s.y, col2 / s.z));
                        tr[0] = m[3][0], tr[1] = m[3][1], tr[2] = m[3][2];
                        rot[0] = q.x, rot[1] = q.y, rot[2] = q.z, rot[3] = q.w;
                        sc[0] = s.x, sc[1] = s.y, sc[2] = s.z;
                    } else {
                        if (node->has_translation) std::copy_n(node->translation, 3, tr);
                        if (node->has_rotation) std::copy_n(node->rotation, 4, rot);
                        if (node->has_scale) std::copy_n(node->scale, 3, sc);
                    }
                    if (bt.translation.sampler) sample(bt.translation, t, 3, false, tr);
                    if (bt.rotation.sampler) sample(bt.rotation, t, 4, true, rot);
                    if (bt.scale.sampler) sample(bt.scale, t, 3, false, sc);
                    const glm::quat q = glm::normalize(glm::quat(rot[3], rot[0], rot[1], rot[2]));
                    glm::mat4 local = glm::mat4_cast(q);
                    local[0] *= sc[0];
                    local[1] *= sc[1];
                    local[2] *= sc[2];
                    local[3] = glm::vec4(tr[0], tr[1], tr[2], 1.0f);
                    // A top-level bone's local is its world transform; its
                    // non-bone ancestors (none here: every ancestor of a
                    // joint is a bone) would come first.
                    if (m_model.bones[b].parent < 0 && node->parent) local = worldMatrix(node->parent) * local;
                    locals[b] = local;
                }
            }
            m_model.animations.push_back(std::move(anim));
        }
    }

    const std::string& m_path;
    const ModelLoadOptions& m_options;
    cgltf_data* m_data;
    std::vector<fs::path> m_dirs;
    ModelData m_model;
    static constexpr uint32_t kNone = ~0u;
    uint32_t m_defaultMaterial = kNone;
    std::vector<const cgltf_node*> m_roots;
    std::unordered_map<const cgltf_node*, int> m_boneIndex;
    std::vector<const cgltf_node*> m_boneNode;
    std::unordered_map<cgltf_size, std::string> m_imageFiles;
    std::unordered_map<cgltf_size, std::pair<std::string, std::string>> m_splitFiles;
};

} // namespace

ModelData loadGltfModel(const std::string& path, const ModelLoadOptions& options) {
    std::vector<uint8_t> bytes;
    std::string readError;
    if (!cooked::readAssetFile(path, bytes, &readError)) throw std::runtime_error("loadModel: " + readError);
    cgltf_options opts{};
    opts.file.read = readFile;
    opts.file.release = releaseFile;
    cgltf_data* data = nullptr;
    cgltf_result r = cgltf_parse(&opts, bytes.data(), bytes.size(), &data);
    if (r != cgltf_result_success) fail(path, resultText(r));
    std::unique_ptr<cgltf_data, void (*)(cgltf_data*)> guard(data, cgltf_free);
    for (cgltf_size i = 0; i < data->extensions_required_count; ++i) {
        const std::string ext = data->extensions_required[i];
        if (ext == "KHR_draco_mesh_compression" || ext == "EXT_meshopt_compression" || ext == "KHR_meshopt_compression")
            fail(path, "needs " + ext + ", which kk-engine doesn't decode yet; export it without mesh compression");
        if (ext == "KHR_texture_basisu" || ext == "EXT_texture_webp")
            fail(path, "needs " + ext + " (KTX2/WebP images only), which kk-engine doesn't read yet; export PNG or JPEG textures");
    }
    r = cgltf_load_buffers(&opts, data, path.c_str());
    if (r != cgltf_result_success) fail(path, std::string("its buffers: ") + resultText(r));
    r = cgltf_validate(data);
    if (r != cgltf_result_success) fail(path, resultText(r));
    return Importer(path, options, data).run();
}

} // namespace kke::detail
