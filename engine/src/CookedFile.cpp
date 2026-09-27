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
constexpr size_t kTag = sizeof(kArtBuildTag), kNonce = 24, kMac = 16, kHeader = sizeof(kMagic) + kTag + kNonce + kMac;
static_assert(kTag == 8, "the build tag is 8 bytes (engine/CMakeLists.txt)");

// This build's key, joined from its two halves only while it is used and
// wiped after, so it never sits whole in the binary or in memory
// (engine/CMakeLists.txt makes the halves).
class BuildKey {
public:
    BuildKey() {
        // Read through volatile, or the optimiser joins the halves at
        // compile time and the whole key lands in the binary after all.
        const volatile unsigned char* masked = kArtKeyMasked;
        const volatile unsigned char* mask = kArtKeyMask;
        for (size_t i = 0; i < sizeof(m_key); ++i) m_key[i] = static_cast<uint8_t>(masked[i] ^ mask[i]);
    }
    ~BuildKey() { crypto_wipe(m_key, sizeof(m_key)); }
    BuildKey(const BuildKey&) = delete;
    BuildKey& operator=(const BuildKey&) = delete;
    const uint8_t* data() const { return m_key; }

private:
    uint8_t m_key[32];
};
static_assert(sizeof(kArtKeyMasked) == 32 && sizeof(kArtKeyMask) == 32, "the art key is 32 bytes");

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
    uint8_t* tag = out.data() + sizeof(kMagic);
    std::memcpy(tag, kArtBuildTag, kTag);
    uint8_t* nonce = tag + kTag;
    uint8_t* mac = nonce + kNonce;
    if (!seal::randomBytes(nonce, kNonce, error)) return false;
    const BuildKey key;
    // The tag is authenticated too: a file can't be relabelled for another build.
    crypto_aead_lock(out.data() + kHeader, mac, key.data(), nonce, tag, kTag, plain.data(), plain.size());
    return true;
}

bool uncook(const std::vector<uint8_t>& cooked, std::vector<uint8_t>& out, std::string* error) {
    if (!isCooked(cooked)) {
        if (error) *error = "not a cooked file";
        return false;
    }
    const uint8_t* tag = cooked.data() + sizeof(kMagic);
    const uint8_t* nonce = tag + kTag;
    const uint8_t* mac = nonce + kNonce;
    if (std::memcmp(tag, kArtBuildTag, kTag) != 0) {
        if (error) *error = "cooked for another build (each bake has its own key: cook and build together)";
        return false;
    }
    out.resize(cooked.size() - kHeader);
    const BuildKey key;
    if (crypto_aead_unlock(out.data(), mac, key.data(), nonce, tag, kTag, cooked.data() + kHeader, out.size()) != 0) {
        crypto_wipe(out.data(), out.size());
        out.clear();
        if (error) *error = "damaged, or cooked with another checkout's key";
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
