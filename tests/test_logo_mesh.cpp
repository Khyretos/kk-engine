#include "kke/LogoMesh.h"

#include <gtest/gtest.h>

#include <cmath>
#include <set>

namespace {

float triangleArea(const std::vector<glm::vec2>& p, const std::vector<uint32_t>& tris) {
    float area = 0.0f;
    for (size_t i = 0; i + 2 < tris.size(); i += 3) {
        glm::vec2 a = p[tris[i]], b = p[tris[i + 1]], c = p[tris[i + 2]];
        area += 0.5f * ((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x));
    }
    return area;
}

} // namespace

TEST(LogoMesh, TriangulatesConcavePolygonExactly) {
    // An L shape: one reflex corner.
    std::vector<glm::vec2> l = { { 0, 0 }, { 2, 0 }, { 2, 1 }, { 1, 1 }, { 1, 2 }, { 0, 2 } };
    auto tris = kke::logo::triangulate(l);
    EXPECT_EQ(tris.size(), 3u * (l.size() - 2));
    EXPECT_NEAR(triangleArea(l, tris), kke::logo::signedArea(l), 1e-5f);
    EXPECT_NEAR(kke::logo::signedArea(l), 3.0f, 1e-5f);
}

TEST(LogoMesh, EveryOutlineIsCounterClockwiseAndFullyCovered) {
    auto pieces = kke::logo::buildPieces();
    for (const auto& piece : pieces)
        for (const auto& outline : piece.outlines) {
            const float area = kke::logo::signedArea(outline.points);
            EXPECT_GT(area, 0.0f) << piece.name;
            // Every triangle counter-clockwise, and together they cover the
            // outline exactly (no gaps, no overlaps).
            auto tris = kke::logo::triangulate(outline.points);
            EXPECT_NEAR(triangleArea(outline.points, tris), area, area * 1e-3f) << piece.name;
        }
}

TEST(LogoMesh, HasEveryPartOfTheLogo) {
    std::set<std::string> names;
    for (const auto& piece : kke::logo::buildPieces()) {
        names.insert(piece.name);
        EXPECT_FALSE(piece.indices.empty()) << piece.name;
        EXPECT_EQ(piece.indices.size() % 3, 0u) << piece.name;
        for (uint32_t i : piece.indices) ASSERT_LT(i, piece.vertices.size()) << piece.name;
        for (const auto& v : piece.vertices) {
            EXPECT_TRUE(std::isfinite(v.position.x) && std::isfinite(v.normal.x)) << piece.name;
            EXPECT_NEAR(glm::length(v.normal), 1.0f, 1e-3f) << piece.name;
            // Everything inside the logo's documented bounds.
            EXPECT_LE(std::abs(v.position.x), 1.01f) << piece.name;
            EXPECT_GE(v.position.y, -1.01f) << piece.name;
            EXPECT_LE(v.position.y, 1.5f) << piece.name;
        }
    }
    for (const char* n : { "ring_top", "ring_right", "ring_bottom", "ring_left", "crown_loop", "crown_stem", "k_left",
                           "k_right", "needle_north", "needle_south", "hub" })
        EXPECT_TRUE(names.count(n)) << n;
}

TEST(LogoMesh, FrontFacesPointAtTheViewer) {
    // The front cap of an extruded square: normals +Z, wound counter-clockwise seen from +Z.
    kke::logo::Outline square{ { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } } };
    std::vector<kke::Vertex> v;
    std::vector<uint32_t> idx;
    kke::logo::extrude(square, -0.1f, 0.1f, 0.0f, glm::vec3(1), v, idx);
    int front = 0;
    for (size_t i = 0; i + 2 < idx.size(); i += 3) {
        const auto &a = v[idx[i]], &b = v[idx[i + 1]], &c = v[idx[i + 2]];
        glm::vec3 geometric = glm::cross(b.position - a.position, c.position - a.position);
        // Winding agrees with the stored normal for every triangle.
        EXPECT_GT(glm::dot(geometric, a.normal), 0.0f);
        if (a.normal.z > 0.99f) ++front;
    }
    EXPECT_EQ(front, 2);
}

TEST(LogoMesh, EveryTriangleWindsTheWayItsNormalFaces) {
    // Back-face culling keeps exactly the faces whose winding matches
    // their normal; a mismatch would show as a hole in the intro's logo.
    for (const auto& piece : kke::logo::buildPieces()) {
        int wrong = 0;
        for (size_t i = 0; i + 2 < piece.indices.size(); i += 3) {
            const auto &a = piece.vertices[piece.indices[i]], &b = piece.vertices[piece.indices[i + 1]],
                       &c = piece.vertices[piece.indices[i + 2]];
            glm::vec3 geometric = glm::cross(b.position - a.position, c.position - a.position);
            if (glm::length(geometric) < 1e-9f) continue;
            if (glm::dot(geometric, a.normal + b.normal + c.normal) <= 0.0f) ++wrong;
        }
        EXPECT_EQ(wrong, 0) << piece.name;
    }
}
