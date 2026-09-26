#include "kke/SceneLoader.h"

#include "kke/Log.h"

#include <glm/gtc/matrix_transform.hpp>

#include <filesystem>
#include <map>

namespace kke {

namespace {

// The object's own pack first (when the scene names one), then the
// scene's pack order, then any pack.
const CatalogAsset* findAsset(const AssetCatalog& catalog, const SceneObject& o, const SceneFile& scene) {
    if (o.pack.empty()) return catalog.find(o.asset, scene.packs);
    std::vector<std::string> prefer{ o.pack };
    prefer.insert(prefer.end(), scene.packs.begin(), scene.packs.end());
    return catalog.find(o.asset, prefer);
}

void addGround(const SceneFile& scene, const glm::vec3& origin, RigidWorld& world, LoadedScene& out) {
    if (scene.groundSize.x <= 0.0f || scene.groundSize.y <= 0.0f) return;
    RigidWorld::BodyDesc g;
    g.motion = RigidWorld::Motion::Static;
    g.halfExtents = glm::vec3(scene.groundSize.x * 0.5f, 0.25f, scene.groundSize.y * 0.5f);
    g.position = origin + glm::vec3(0.0f, -0.25f, 0.0f);
    g.material = 1;
    out.bodies.push_back(world.add(g));
}

void addCollision(const SceneObject& o, const ModelData& model, const glm::mat4& t, RigidWorld& world, LoadedScene& out) {
    const ModelData* d = &model;
    if (o.collision == SceneObject::Collision::None) return;
    RigidWorld::BodyDesc b;
    b.motion = RigidWorld::Motion::Static;
    b.material = 1;
    if (o.collision == SceneObject::Collision::Box) {
        // Bounds as an oriented box (scale and yaw only, as placed).
        const glm::vec3 c = (d->boundsMin + d->boundsMax) * 0.5f;
        b.shape = RigidWorld::Shape::Box;
        b.halfExtents = glm::max((d->boundsMax - d->boundsMin) * 0.5f * glm::abs(o.scale), glm::vec3(0.02f));
        b.position = glm::vec3(t * glm::vec4(c, 1.0f));
        b.rotation = glm::angleAxis(glm::radians(o.yaw), glm::vec3(0, 1, 0));
    } else {
        // The model's own triangles, in world space (static meshes are
        // already in model space).
        b.shape = RigidWorld::Shape::Mesh;
        for (const ModelMesh& m : d->meshes) {
            if (m.skinned) continue;
            const uint32_t base = static_cast<uint32_t>(b.points.size());
            for (const ModelVertex& v : m.vertices) b.points.push_back(glm::vec3(t * glm::vec4(v.position, 1.0f)));
            for (uint32_t i : m.indices) b.indices.push_back(base + i);
        }
        if (b.indices.size() < 3) return;
        out.collisionTriangles += b.indices.size() / 3;
    }
    RigidWorld::BodyId body = world.add(b);
    if (body != RigidWorld::kNoBody) out.bodies.push_back(body);
}

} // namespace

ModelLoadOptions packLoadOptions(const AssetCatalog& catalog, const CatalogAsset& asset) {
    ModelLoadOptions opts;
    opts.loadAnimations = false;
    if (const CatalogPack* pack = catalog.pack(asset.pack)) {
        opts.textureSearchPaths = pack->textureDirs;
        // Some meshes point at another pack's atlas: use this one's.
        opts.fallbackTexture = pack->defaultTexture;
        opts.assetTexture = catalog.namedTexture(asset);
        opts.atlasForUntextured = !pack->defaultTexture.empty();
        const CatalogAsset* a = &asset; // the catalog outlives the load
        opts.textureForMaterial = [&catalog, a](const std::string& m) { return catalog.namedTexture(*a, m); };
    }
    return opts;
}

LoadedScene loadScene(const SceneFile& scene, const AssetCatalog& catalog, ModelModule& models, RigidWorld* world, const glm::vec3& origin) {
    LoadedScene out;
    out.origin = origin;
    std::map<std::string, ModelModule::ModelId> loaded; // asset -> model (each file loads once)
    const glm::mat4 shift = glm::translate(glm::mat4(1.0f), origin);
    if (world) addGround(scene, origin, *world, out);
    for (const SceneObject& o : scene.objects) {
        ModelModule::ModelId id = 0;
        const std::string key = o.pack + "/" + o.asset;
        if (auto it = loaded.find(key); it != loaded.end()) id = it->second;
        else {
            const CatalogAsset* asset = findAsset(catalog, o, scene);
            if (asset) {
                id = models.load(asset->path, packLoadOptions(catalog, *asset));
            }
            loaded[key] = id;
            if (!id) out.missing.push_back(o.asset);
        }
        if (!id) continue;
        const ModelData* d = models.model(id);
        // A texture variant (the sandbox's per-object "Texture"): found by
        // file name in the asset's pack, so the scene works on any machine.
        std::string texture;
        if (!o.texture.empty()) {
            const CatalogAsset* asset = findAsset(catalog, o, scene);
            if (const CatalogPack* pack = asset ? catalog.pack(asset->pack) : nullptr)
                for (const std::string& v : pack->textureVariants)
                    if (std::filesystem::path(v).filename() == o.texture) { texture = v; break; }
            if (texture.empty())
                log::get("Scene")->warn("scene '{}': texture '{}' for '{}' isn't in its pack; using the model's own", scene.name, o.texture, o.asset);
        }
        for (int gz = 0; gz < o.gridCount.y; ++gz)
            for (int gx = 0; gx < o.gridCount.x; ++gx) {
                const glm::mat4 t = shift * SceneFile::placement(o, d->boundsMin, d->boundsMax, { gx, gz });
                out.instances.push_back(models.spawn(id, t));
                if (!texture.empty()) models.setTextureOverride(out.instances.back(), texture);
                if (world) addCollision(o, *d, t, *world, out);
            }
    }
    log::get("Scene")->info("scene '{}': {} instances, {} collision bodies ({} triangles){}", scene.name, out.instances.size(), out.bodies.size(),
                            out.collisionTriangles, out.missing.empty() ? "" : ", " + std::to_string(out.missing.size()) + " asset(s) missing");
    for (const std::string& m : out.missing) log::get("Scene")->warn("scene '{}': asset '{}' isn't in any installed pack", scene.name, m);
    return out;
}

LoadedScene loadSceneCollision(const SceneFile& scene, const AssetCatalog& catalog, RigidWorld& world, const glm::vec3& origin) {
    LoadedScene out;
    out.origin = origin;
    addGround(scene, origin, world, out);
    std::map<std::string, ModelData> cache;
    const glm::mat4 shift = glm::translate(glm::mat4(1.0f), origin);
    for (const SceneObject& o : scene.objects) {
        if (o.collision == SceneObject::Collision::None) continue;
        const std::string key = o.pack + "/" + o.asset;
        auto it = cache.find(key);
        if (it == cache.end()) {
            ModelData d;
            if (const CatalogAsset* a = findAsset(catalog, o, scene)) {
                try {
                    ModelLoadOptions opts;
                    opts.loadAnimations = false;
                    d = loadModel(a->path, opts);
                } catch (const std::exception&) {}
            }
            if (d.meshes.empty()) out.missing.push_back(o.asset);
            it = cache.emplace(key, std::move(d)).first;
        }
        if (it->second.meshes.empty()) continue;
        for (int gz = 0; gz < o.gridCount.y; ++gz)
            for (int gx = 0; gx < o.gridCount.x; ++gx)
                addCollision(o, it->second, shift * SceneFile::placement(o, it->second.boundsMin, it->second.boundsMax, { gx, gz }), world, out);
    }
    return out;
}

void unloadScene(LoadedScene& scene, ModelModule& models, RigidWorld* world) {
    for (ModelModule::InstanceId i : scene.instances) models.remove(i);
    if (world)
        for (RigidWorld::BodyId b : scene.bodies) world->remove(b);
    scene = LoadedScene{};
}

} // namespace kke
