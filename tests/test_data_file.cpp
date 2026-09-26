// JSON and YAML data files, interchangeably (kke/DataFile.h): the same
// value from either, and the newest file winning when both exist.

#include "kke/ContentPacks.h"
#include "kke/DataFile.h"
#include "kke/GameManifest.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>

namespace datafile = kke::datafile;
namespace fs = std::filesystem;
using nlohmann::json;
using datafile::Format;

namespace {

struct TempDir {
    fs::path path;
    TempDir() {
        std::random_device rd;
        path = fs::temp_directory_path() / ("kke_datafile_" + std::to_string(rd()) + std::to_string(rd()));
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

void write(const fs::path& file, const std::string& text) {
    fs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << text;
}

// Sets a file's modification time `seconds` from now, so "newest" doesn't
// depend on how fast the test writes.
void age(const fs::path& file, int seconds) {
    fs::last_write_time(file, fs::file_time_type::clock::now() + std::chrono::seconds(seconds));
}

json yaml(const std::string& text) {
    json out;
    std::string error;
    EXPECT_TRUE(datafile::parse(text, Format::Yaml, out, &error)) << error;
    return out;
}

} // namespace

TEST(DataFile, YamlReadsLikeTheSameJson) {
    const json fromYaml = yaml(R"(
id: com.example.frost
title: Frost
version: 1.10        # stays "1.10": 1.1 would be another version
build: 7
ratio: 0.5
code: "007"
zip: 007
enabled: true
norway: no           # YAML 1.2: text, not false
nothing: ~
empty:
quoted: "true"
dependencies:
  - lib >= 1.2
  - "? hd_textures"
nested: {a: [1, 2], b: {c: x}}
)");
    const json expected = json::parse(R"({
        "id": "com.example.frost", "title": "Frost", "version": "1.10", "build": 7, "ratio": 0.5,
        "code": "007", "zip": "007", "enabled": true, "norway": "no", "nothing": null, "empty": null,
        "quoted": "true", "dependencies": ["lib >= 1.2", "? hd_textures"], "nested": {"a": [1, 2], "b": {"c": "x"}} })");
    EXPECT_EQ(fromYaml, expected) << fromYaml.dump(2);
}

TEST(DataFile, RoundTripsThroughBothFormats) {
    const json data = json::parse(R"({"s": "1.2", "t": "true", "e": "", "n": null, "i": -3, "d": 2.5, "b": false,
                                      "a": ["x: y", "- dash", "#hash"], "o": {"123": "key that looks like a number"}})");
    for (Format f : { Format::Json, Format::Yaml }) {
        json back;
        std::string error;
        ASSERT_TRUE(datafile::parse(datafile::dump(data, f), f, back, &error)) << error << "\n" << datafile::dump(data, f);
        EXPECT_EQ(back, data) << datafile::dump(data, f);
    }
}

TEST(DataFile, BadFilesSayWhy) {
    json out;
    std::string error;
    EXPECT_FALSE(datafile::parse("a: [1, 2", Format::Yaml, out, &error));
    EXPECT_NE(error.find("YAML"), std::string::npos);
    EXPECT_FALSE(datafile::parse("{", Format::Json, out, &error));
    EXPECT_NE(error.find("JSON"), std::string::npos);
    EXPECT_FALSE(datafile::parse("? [a]\n: b\n", Format::Yaml, out, &error)) << "non-text keys";
    EXPECT_FALSE(datafile::loadFile("pack.toml", out, &error));
    EXPECT_EQ(datafile::formatOf("A.YAML"), Format::Yaml);
    EXPECT_FALSE(datafile::formatOf("a.txt"));
}

TEST(DataFile, NewestWinsWhenBothExistAndDiffer) {
    TempDir t;
    datafile::Loaded loaded;
    std::string error;
    EXPECT_FALSE(datafile::load(t.path, "pack", loaded, &error));
    EXPECT_EQ(datafile::saveTarget(t.path, "pack"), t.path / "pack.json");

    write(t.path / "pack.json", R"({"id": "a", "title": "From JSON"})");
    write(t.path / "pack.yml", "id: a\ntitle: From YAML\n");
    age(t.path / "pack.json", -60);
    ASSERT_TRUE(datafile::load(t.path, "pack", loaded, &error)) << error;
    EXPECT_EQ(loaded.data["title"], "From YAML");
    EXPECT_EQ(loaded.file, t.path / "pack.yml");
    EXPECT_NE(loaded.warning.find("pack.json"), std::string::npos) << loaded.warning;
    EXPECT_NE(loaded.warning.find("pack.yml"), std::string::npos);
    EXPECT_EQ(datafile::saveTarget(t.path, "pack"), t.path / "pack.yml");

    // Edit the JSON one: now it's the newest and wins.
    age(t.path / "pack.json", 60);
    ASSERT_TRUE(datafile::load(t.path, "pack", loaded, &error));
    EXPECT_EQ(loaded.data["title"], "From JSON");

    // The same content in both: no warning.
    write(t.path / "pack.yml", "id: a\ntitle: From JSON\n");
    ASSERT_TRUE(datafile::load(t.path, "pack", loaded, &error));
    EXPECT_TRUE(loaded.warning.empty()) << loaded.warning;

    // The newest is broken: an error, not the older file behind the author's back.
    write(t.path / "pack.yml", "id: [\n");
    age(t.path / "pack.yml", 120);
    EXPECT_FALSE(datafile::load(t.path, "pack", loaded, &error));
    EXPECT_NE(error.find("pack.yml"), std::string::npos) << error;
}

TEST(DataFile, SaveWritesTheFormatOfTheName) {
    TempDir t;
    const json data = { { "id", "x" }, { "version", "1.10" } };
    ASSERT_TRUE(datafile::saveFile(t.path / "mods.yaml", data));
    json back;
    ASSERT_TRUE(datafile::loadFile(t.path / "mods.yaml", back));
    EXPECT_EQ(back, data);
    EXPECT_FALSE(fs::exists(t.path / "mods.yaml.tmp"));
    EXPECT_FALSE(datafile::saveFile(t.path / "mods.txt", data));
}

TEST(DataFile, PacksModListsAndGamesInYaml) {
    namespace packs = kke::packs;
    TempDir t;
    write(t.path / "mods" / "axe" / "pack.yml", R"(
id: axe
title: Better axe
version: 1.2
dependencies: [lib >= 1.0]
)");
    write(t.path / "mods" / "axe" / "scripts" / "axe.lua", "damage = 10");
    write(t.path / "mods" / "lib" / "pack.yaml", "id: lib\ntitle: Lib\nversion: 1\n");
    std::vector<packs::Problem> problems;
    const auto found = packs::discover({ { t.path / "mods", "mods" } }, &problems);
    ASSERT_EQ(found.size(), 2u);
    EXPECT_TRUE(problems.empty());
    EXPECT_EQ(found[0].version, "1.2");

    write(t.path / "mods.yml", "format: kke-modlist-1\npacks:\n  - {id: lib, enabled: false}\n");
    packs::ModList list;
    ASSERT_TRUE(packs::ModList::load(t.path / "mods.json", list)) << "mods.yml answers for mods.json";
    EXPECT_FALSE(list.enabled("lib"));
    const packs::MountPlan plan = packs::resolve(found, list, {});
    EXPECT_TRUE(plan.order.empty()) << "axe needs lib, which is off";

    const packs::Mount mount(t.path / "data", packs::resolve(found, {}, {}));
    EXPECT_FALSE(mount.exists("pack.yml")) << "a pack's own manifest isn't data, in any format";
    EXPECT_TRUE(mount.exists("scripts/axe.lua"));

    write(t.path / "game" / "game.yml", "id: com.example.game\ntitle: Game\nversion: 0.3\ntags: [demo]\n");
    const kke::GameManifest g = kke::loadGameManifest((t.path / "game").string());
    EXPECT_EQ(g.id, "com.example.game");
    EXPECT_EQ(g.version, "0.3");
    EXPECT_EQ(g.tags, std::vector<std::string>{ "demo" });
}
