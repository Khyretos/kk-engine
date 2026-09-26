#pragma once

#include <nlohmann/json.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace kke::datafile {

// Data files that can be JSON or YAML, interchangeably: pack.json or
// pack.yml, mods.json or mods.yaml, game.json or game.yml. Whatever the
// author prefers; the engine reads both into the same JSON value.
//
// When a folder has more than one (pack.json and pack.yml), they're all
// read: identical ones are fine; if they differ, the most recently
// modified file wins and a warning names both, so an edit to either one
// is never silently ignored.
//
// YAML is read as YAML 1.2's core schema: true/false (not yes/no),
// null or ~, numbers, everything else is text. Quoted values are always
// text. A number that wouldn't survive as a number ("1.10", "007") stays
// text, so versions keep their spelling.

enum class Format { Json, Yaml };

// By extension: .json, .yml, .yaml (any case). Nothing for others.
std::optional<Format> formatOf(const std::filesystem::path& file);

bool parse(const std::string& text, Format format, nlohmann::json& out, std::string* error = nullptr);
std::string dump(const nlohmann::json& data, Format format);

// Reads one file, format from its extension.
bool loadFile(const std::filesystem::path& file, nlohmann::json& out, std::string* error = nullptr);
// Writes one file (format from its extension) via a temporary file and a
// rename, so a crash never leaves half a file.
bool saveFile(const std::filesystem::path& file, const nlohmann::json& data, std::string* error = nullptr);

// <folder>/<stem>.json, .yml and .yaml: the ones that exist, in that order.
std::vector<std::filesystem::path> variants(const std::filesystem::path& folder, const std::string& stem);
bool exists(const std::filesystem::path& folder, const std::string& stem);
// Is `fileName` one of `stem`'s names ("pack.yml" for "pack")?
bool isVariant(const std::string& fileName, const std::string& stem);

struct Loaded {
    nlohmann::json data;
    std::filesystem::path file;  // the file that was used
    std::string warning;         // set when the variants differed (also logged)
};

// Loads <folder>/<stem> in whichever format is there, as described above.
// False with `error` when there's none, or the one that wins can't be
// read (an older, readable twin is not used behind the author's back).
bool load(const std::filesystem::path& folder, const std::string& stem, Loaded& out, std::string* error = nullptr);

// Where to save <folder>/<stem> so the next load reads it back: the file
// load() would use, or <stem>.json when there's none yet.
std::filesystem::path saveTarget(const std::filesystem::path& folder, const std::string& stem);

// A text field that YAML may have turned into a number (version: 1.2).
std::string text(const nlohmann::json& object, const char* key, const std::string& fallback = {});

} // namespace kke::datafile
