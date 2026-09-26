// kke_model_info <file> — what the engine sees in a model file: mesh
// parts, triangles, materials/textures, bones, animation clips (name,
// length). For checking a pack before building with it.
#include "kke/ModelAsset.h"

#include <cstdio>
#include <exception>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: kke_model_info <model.fbx|.obj> [--bones]\n");
        return 2;
    }
    try {
        kke::ModelData m = kke::loadModel(argv[1]);
        std::printf("%s\n  %zu mesh part(s), %zu triangles, %zu material(s), %zu bone(s), %zu animation(s)\n", argv[1], m.meshes.size(),
                    m.triangleCount(), m.materials.size(), m.bones.size(), m.animations.size());
        std::printf("  bounds (%.2f %.2f %.2f) .. (%.2f %.2f %.2f)\n", m.boundsMin.x, m.boundsMin.y, m.boundsMin.z, m.boundsMax.x, m.boundsMax.y,
                    m.boundsMax.z);
        for (const auto& mat : m.materials) std::printf("  material '%s' texture '%s'\n", mat.name.c_str(), mat.albedoTexture.c_str());
        if (argc > 2)
            for (size_t b = 0; b < m.bones.size(); ++b) std::printf("  bone %zu '%s' parent %d\n", b, m.bones[b].name.c_str(), m.bones[b].parent);
        for (const auto& a : m.animations) std::printf("  clip '%s' %.2f s\n", a.name.c_str(), a.duration);
    } catch (const std::exception& e) {
        std::printf("error: %s\n", e.what());
        return 1;
    }
    return 0;
}
