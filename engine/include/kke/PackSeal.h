#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace kke::seal {

// Tamper evidence for game data (docs/ANTI_CHEAT.md "Sealed game data").
//
// A seal is a list of every file in a game's data folder with its
// BLAKE2b-256 hash, signed with the developer's private key (EdDSA,
// Monocypher). The game ships the seal and has the matching public key
// compiled in; at start it checks both. A changed texture, a Lua script
// with "damage = 9999", a level with a wall removed: the file's hash no
// longer matches and the signature can't be redone without the private
// key, which never leaves the developer's machine or CI.
//
// What this is and isn't: on a player's own PC, someone determined can
// patch the check itself out of the executable. The seal's real weight
// is (1) the manifest digest a client sends when it joins, so a server
// can turn away clients whose data differs from its own, (2) catching
// broken downloads and stale mods, and (3) knowing which marketplace
// content really comes from its author. Pair it with server authority
// (kke/net/Authority.h): a server that decides what matters doesn't
// care what the client's files say.

using Hash = std::array<uint8_t, 32>;
using PublicKey = std::array<uint8_t, 32>;
using SecretKey = std::array<uint8_t, 64>; // Monocypher's: seed + public key
using Signature = std::array<uint8_t, 64>;

constexpr const char* kSealFileName = "kke.seal"; // never sealed itself

struct KeyPair {
    SecretKey secret{};
    PublicKey publicKey{};
};

// Bytes from the OS's secure random source (getrandom, arc4random,
// BCryptGenRandom); false (with `error`) when that isn't available.
bool randomBytes(uint8_t* out, size_t size, std::string* error = nullptr);
// A fresh key pair from the OS's secure random source; false (with
// `error`) when that isn't available.
bool generateKeyPair(KeyPair& out, std::string* error = nullptr);
// The key pair a 32-byte seed makes (the same seed, the same keys).
KeyPair keyPairFromSeed(const std::array<uint8_t, 32>& seed);

Hash hashBytes(const void* data, size_t size);
bool hashFile(const std::filesystem::path& file, Hash& out, std::string* error = nullptr);

struct Manifest {
    std::map<std::string, Hash> files;       // path relative to the root, '/' separators
    std::optional<Signature> signature;
};

// Hashes every regular file under `root` (recursively, symlinks not
// followed, the seal file itself skipped). False with `error` when the
// folder or a file can't be read.
bool build(const std::filesystem::path& root, Manifest& out, std::string* error = nullptr);

// The exact bytes that are signed: format line, then "path\thash\n" per
// file in path order. Stable across platforms.
std::string canonical(const Manifest& m);
// One hash for the whole data set (of canonical()): what a client sends
// a server, what two builds compare.
Hash digest(const Manifest& m);

void sign(Manifest& m, const SecretKey& secret);
bool signatureValid(const Manifest& m, const PublicKey& publicKey);

// kke.seal is JSON: {"format": "kke-seal-1", "files": {path: hex}, "signature": hex}.
std::string toJson(const Manifest& m);
bool fromJson(const std::string& json, Manifest& out, std::string* error = nullptr);
bool save(const Manifest& m, const std::filesystem::path& file, std::string* error = nullptr);
bool load(const std::filesystem::path& file, Manifest& out, std::string* error = nullptr);

struct Report {
    bool sealFound = false;
    bool signatureOk = false;
    std::vector<std::string> modified, missing, added; // relative paths
    std::string error;                                  // why it couldn't be checked at all
    bool ok() const { return sealFound && signatureOk && modified.empty() && missing.empty() && added.empty() && error.empty(); }
    std::string summary() const;                        // one line for logs
};

// Loads <root>/kke.seal, checks its signature with `publicKey`, then
// every file under `root` against it. Never throws.
Report verify(const std::filesystem::path& root, const PublicKey& publicKey);

std::string toHex(const uint8_t* data, size_t size);
template <size_t N> std::string toHex(const std::array<uint8_t, N>& a) { return toHex(a.data(), N); }
// False when `hex` isn't exactly 2*N hex digits.
bool fromHex(const std::string& hex, uint8_t* out, size_t size);
template <size_t N> bool fromHex(const std::string& hex, std::array<uint8_t, N>& out) { return fromHex(hex, out.data(), N); }

} // namespace kke::seal
