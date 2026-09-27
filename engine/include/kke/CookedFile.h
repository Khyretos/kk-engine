#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace kke::cooked {

// Cooked art (docs/COOKED_ART.md): how a game made with KKE ships art it
// may use but not redistribute as source files, such as Synty packs.
// Their licence allows the art inside a game, not the FBX and textures
// themselves for anyone to lift out, which is what the engine's plain
// asset folders would hand over.
//
// A cooked file keeps its name and folder, so everything that finds art
// by name (AssetCatalog, findAssetFolder, texture search) is unchanged;
// its contents are encrypted (XChaCha20-Poly1305, Monocypher) with the
// key of the checkout that cooked it (KKE_ART_KEY_FILE, never
// committed). Only builds with that key can read it. The loaders that
// read art (models, textures, Sidekick .sk files) go through
// readAssetFile(), which reads plain and cooked files alike.

// The first bytes of every cooked file.
inline constexpr char kMagic[8] = { 'K', 'K', 'E', 'C', 'O', 'O', 'K', '1' };

// The whole file, decrypted when cooked. False (with `error`) when it
// can't be read, or it is cooked with another checkout's key or damaged.
// With KKE_ASSET_TRACE=<file> set, every path read is appended to that
// file (tools/packaging/bake_with_art.sh records which art a game uses).
bool readAssetFile(const std::string& path, std::vector<uint8_t>& out, std::string* error = nullptr);

// Plain bytes -> a cooked file's bytes (a fresh random nonce each time),
// and back. False when the OS has no secure random source, or the bytes
// aren't cooked with this build's key.
bool cook(const std::vector<uint8_t>& plain, std::vector<uint8_t>& out, std::string* error = nullptr);
bool uncook(const std::vector<uint8_t>& cooked, std::vector<uint8_t>& out, std::string* error = nullptr);

bool isCooked(const std::vector<uint8_t>& bytes);
bool isCookedFile(const std::string& path);

} // namespace kke::cooked
