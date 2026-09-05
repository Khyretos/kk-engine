// Tests for kke::loadTetMeshFromFile — the runtime side of the CGAL
// content pipeline (see README "Content pipeline: CGAL
// tetrahedralization"). Deliberately tests against real temporary
// files, matching test_game_manifest.cpp's own convention, and
// includes one fixture written by hand to match the exact shape of a
// real kke_tetrahedralizer output file, verified against the tool's
// actual output while building this pipeline (not just assumed to
// match).

#include "kke/TetMeshAsset.h"
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace {

class TetMeshAssetTest : public ::testing::Test {
protected:
    void SetUp() override {
        root = fs::temp_directory_path() / "kke_tet_mesh_asset_test";
        fs::remove_all(root);
        fs::create_directories(root);
    }
    void TearDown() override {
        fs::remove_all(root);
    }

    fs::path write(const std::string& name, const std::string& content) {
        fs::path p = root / name;
        std::ofstream(p) << content;
        return p;
    }

    fs::path root;
};

// A minimal, valid two-vertex-short-of-a-real-mesh fixture — just
// enough to exercise the format (one tet, four verts), matching the
// exact JSON shape kke_tetrahedralizer actually writes.
const char* kValidSingleTet = R"({
  "vertices": [
    [0.0, 0.0, 0.0],
    [1.0, 0.0, 0.0],
    [0.0, 1.0, 0.0],
    [0.0, 0.0, 1.0]
  ],
  "tets": [
    [0, 1, 2, 3]
  ]
})";

TEST_F(TetMeshAssetTest, LoadsValidSingleTet) {
    fs::path p = write("valid.ktet.json", kValidSingleTet);
    kke::TetMeshData data = kke::loadTetMeshFromFile(p.string());

    ASSERT_EQ(data.vertices.size(), 4u);
    ASSERT_EQ(data.tets.size(), 1u);
    EXPECT_FLOAT_EQ(data.vertices[1].x, 1.0f);
    EXPECT_EQ(data.tets[0][0], 0u);
    EXPECT_EQ(data.tets[0][3], 3u);
}

TEST_F(TetMeshAssetTest, MissingFileThrows) {
    EXPECT_THROW(kke::loadTetMeshFromFile((root / "does_not_exist.json").string()), std::runtime_error);
}

TEST_F(TetMeshAssetTest, MalformedJsonThrows) {
    fs::path p = write("malformed.json", "{ this is not valid json ][");
    EXPECT_THROW(kke::loadTetMeshFromFile(p.string()), std::runtime_error);
}

TEST_F(TetMeshAssetTest, MissingRequiredKeysThrows) {
    fs::path p = write("missing_keys.json", R"({"vertices": [[0,0,0]]})");
    EXPECT_THROW(kke::loadTetMeshFromFile(p.string()), std::runtime_error);
}

TEST_F(TetMeshAssetTest, OutOfRangeTetIndexThrows) {
    // A tet referencing vertex index 5 when only 4 vertices (0-3) exist
    // -- exactly the "internally inconsistent file" case the loader's
    // own doc comment promises to catch, not silently read garbage.
    fs::path p = write("bad_index.json", R"({
        "vertices": [[0,0,0],[1,0,0],[0,1,0],[0,0,1]],
        "tets": [[0,1,2,5]]
    })");
    EXPECT_THROW(kke::loadTetMeshFromFile(p.string()), std::runtime_error);
}

TEST_F(TetMeshAssetTest, EmptyMeshThrows) {
    fs::path p = write("empty.json", R"({"vertices": [], "tets": []})");
    EXPECT_THROW(kke::loadTetMeshFromFile(p.string()), std::runtime_error);
}

TEST_F(TetMeshAssetTest, WrongShapedVertexThrows) {
    // A vertex with 2 components instead of 3 -- malformed in a way
    // that's syntactically valid JSON but semantically wrong.
    fs::path p = write("bad_vertex.json", R"({
        "vertices": [[0,0]],
        "tets": [[0,0,0,0]]
    })");
    EXPECT_THROW(kke::loadTetMeshFromFile(p.string()), std::runtime_error);
}

} // namespace
