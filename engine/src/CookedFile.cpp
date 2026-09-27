#include "kke/CookedFile.h"
#include "kke/PackSeal.h"

#include "kke_art_key.h" // generated from KKE_ART_KEY_FILE (engine/CMakeLists.txt)

#include <SDL3/SDL.h>
#include <monocypher.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <system_error>
#include <utility>

namespace kke::cooked {

namespace {
constexpr size_t kNonce = 24, kMac = 16, kHeader = sizeof(kMagic) + kNonce + kMac;

void trace(const std::string& path) {
    const char* file = SDL_getenv("KKE_ASSET_TRACE");
    if (!file || !*file) return;
    static std::mutex mutex;
    std::error_code ec;
    const std::filesystem::path abs = std::filesystem::absolute(std::filesystem::path(path), ec);
    const std::u8string u = (ec ? std::filesystem::path(path) : abs).lexically_normal().generic_u8string();
    std::lock_guard<std::mutex> lock(mutex);
    std::ofstream out(std::filesystem::path(std::u8string(file, file + std::strlen(file))), std::ios::app | std::ios::binary);
    out << std::string(u.begin(), u.end()) << "\n";
}
} // namespace

bool isCooked(const std::vector<uint8_t>& bytes) {
    return bytes.size() >= kHeader && std::memcmp(bytes.data(), kMagic, sizeof(kMagic)) == 0;
}

bool isCookedFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary); // narrow, like the loaders it replaces
    char head[sizeof(kMagic)] = {};
    return in.read(head, sizeof(head)) && std::memcmp(head, kMagic, sizeof(kMagic)) == 0;
}

bool cook(const std::vector<uint8_t>& plain, std::vector<uint8_t>& out, std::string* error) {
    out.assign(kHeader + plain.size(), 0);
    std::memcpy(out.data(), kMagic, sizeof(kMagic));
    uint8_t* nonce = out.data() + sizeof(kMagic);
    uint8_t* mac = nonce + kNonce;
    if (!seal::randomBytes(nonce, kNonce, error)) return false;
    crypto_aead_lock(out.data() + kHeader, mac, kKkeArtKey, nonce, nullptr, 0, plain.data(), plain.size());
    return true;
}

bool uncook(const std::vector<uint8_t>& cooked, std::vector<uint8_t>& out, std::string* error) {
    if (!isCooked(cooked)) {
        if (error) *error = "not a cooked file";
        return false;
    }
    const uint8_t* nonce = cooked.data() + sizeof(kMagic);
    const uint8_t* mac = nonce + kNonce;
    out.resize(cooked.size() - kHeader);
    if (crypto_aead_unlock(out.data(), mac, kKkeArtKey, nonce, nullptr, 0, cooked.data() + kHeader, out.size()) != 0) {
        out.clear();
        if (error) *error = "cooked with a different key (another checkout's build) or damaged";
        return false;
    }
    return true;
}

bool readAssetFile(const std::string& path, std::vector<uint8_t>& out, std::string* error) {
    std::ifstream in(path, std::ios::binary | std::ios::ate); // narrow, like the loaders it replaces
    if (!in) {
        if (error) *error = "can't open '" + path + "'";
        return false;
    }
    const std::streamoff size = in.tellg();
    std::vector<uint8_t> bytes(size > 0 ? static_cast<size_t>(size) : 0);
    in.seekg(0);
    if (!bytes.empty() && !in.read(reinterpret_cast<char*>(bytes.data()), size)) {
        if (error) *error = "can't read '" + path + "'";
        return false;
    }
    trace(path);
    if (!isCooked(bytes)) {
        out = std::move(bytes);
        return true;
    }
    std::string why;
    if (!uncook(bytes, out, &why)) {
        if (error) *error = "'" + path + "': " + why;
        return false;
    }
    return true;
}

} // namespace kke::cooked
