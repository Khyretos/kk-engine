#include "kke/VrmClothes.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <string>

// The wardrobe on VRoid's CC0 avatars (docs/ANIME.md "Clothes"): the same
// cuts as the realistic body, from the avatar's own skin.

namespace {

kke::VrmAvatar avatar(const char* file) {
    return kke::loadVrm((std::filesystem::path(KKE_SOURCE_DIR) / "assets/anime" / file).string());
}

size_t triangles(const kke::VrmAvatar& a, size_t from) {
    size_t n = 0;
    for (size_t i = from; i < a.model.meshes.size(); ++i) n += a.model.meshes[i].indices.size() / 3;
    return n;
}

size_t visibleStockTriangles(const kke::VrmAvatar& a, const kke::VrmStock& st) {
    size_t n = 0;
    for (const auto& [mesh, slot] : st.stockSlot) n += a.model.meshes[mesh].indices.size() / 3;
    return n;
}

} // namespace

TEST(VrmClothes, EveryGarmentDressesBothAvatars) {
    for (const char* file : { "HairSample_Female.vrm", "HairSample_Male.vrm" }) {
        kke::VrmAvatar a = avatar(file);
        kke::VrmStock stock;
        kke::dressVrm(a, stock, {});
        ASSERT_FALSE(stock.skinMeshes.empty()) << file;
        for (const kke::GarmentInfo& g : kke::garments()) {
            const size_t added = kke::dressVrm(a, stock, { { g.id, g.colour } });
            EXPECT_GE(added, 1u) << file << ": " << g.id;
            EXPECT_GE(triangles(a, stock.meshes), 20u) << file << ": " << g.id << " is a few triangles of cloth";
            // One toon material per material, every mesh pointing at one.
            EXPECT_EQ(a.toon.size(), a.model.materials.size()) << g.id;
            for (const kke::ModelMesh& m : a.model.meshes) EXPECT_LT(m.material, a.model.materials.size()) << g.id;
        }
    }
}

TEST(VrmClothes, StockClothesLeaveForTheGarmentsThatTakeTheirPlaceAndReturn) {
    kke::VrmAvatar a = avatar("HairSample_Female.vrm");
    kke::VrmStock stock;
    kke::dressVrm(a, stock, {});
    const size_t all = visibleStockTriangles(a, stock);
    ASSERT_GT(all, 0u);
    const size_t meshes = a.model.meshes.size(), materials = a.model.materials.size();

    kke::dressVrm(a, stock, { { "t-shirt", glm::vec3(0.5f) } });
    const size_t withShirt = visibleStockTriangles(a, stock);
    EXPECT_LT(withShirt, all) << "the avatar's own top is hidden by a top";

    kke::dressVrm(a, stock, { { "sneakers", glm::vec3(1.0f) } });
    EXPECT_GT(visibleStockTriangles(a, stock), withShirt) << "...and the top is back when only shoes are worn";

    kke::dressVrm(a, stock, {});
    EXPECT_EQ(visibleStockTriangles(a, stock), all);
    EXPECT_EQ(a.model.meshes.size(), meshes);
    EXPECT_EQ(a.model.materials.size(), materials);
    EXPECT_EQ(a.toon.size(), materials);
}

// A skirt is seen from outside: its triangles wind counter-clockwise there,
// which is the way its vertex normals point (a skirt wound the other way was
// lit from the inside).
TEST(VrmClothes, SkirtsFaceOutward) {
    kke::VrmAvatar a = avatar("HairSample_Female.vrm");
    kke::VrmStock stock;
    kke::dressVrm(a, stock, {});
    kke::dressVrm(a, stock, { { "ball gown", glm::vec3(0.9f) } });
    const kke::ModelMesh* skirt = nullptr;
    for (size_t i = stock.meshes; i < a.model.meshes.size(); ++i)
        if (a.model.meshes[i].name == "ball gown skirt") skirt = &a.model.meshes[i];
    ASSERT_NE(skirt, nullptr);
    size_t agree = 0, total = 0;
    for (size_t t = 0; t + 2 < skirt->indices.size(); t += 3) {
        const auto& v0 = skirt->vertices[skirt->indices[t]];
        const auto& v1 = skirt->vertices[skirt->indices[t + 1]];
        const auto& v2 = skirt->vertices[skirt->indices[t + 2]];
        const glm::vec3 face = glm::cross(v1.position - v0.position, v2.position - v0.position);
        const glm::vec3 normal = v0.normal + v1.normal + v2.normal;
        ++total;
        if (glm::dot(face, normal) > 0.0f) ++agree;
    }
    ASSERT_GT(total, 100u);
    EXPECT_EQ(agree, total) << "every triangle's winding matches its normals";
    // Hips and thighs carry it: weights sum to one.
    for (const kke::ModelVertex& v : skirt->vertices) EXPECT_NEAR(v.weights.x + v.weights.y + v.weights.z + v.weights.w, 1.0f, 1e-4f);
}
