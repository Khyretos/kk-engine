#include "kke/PackSeal.h"

#include <monocypher.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <system_error>

#if defined(_WIN32)
#include <windows.h>
#include <bcrypt.h>
#elif defined(__APPLE__)
#include <stdlib.h> // arc4random_buf
#else
#include <sys/random.h>
#include <cerrno>
#include <cstring>
#endif

namespace kke::seal {

namespace {

constexpr const char* kFormat = "kke-seal-1";

void fail(std::string* error, const std::string& what) {
    if (error) *error = what;
}

bool osRandom(uint8_t* out, size_t size, std::string* error) {
#if defined(_WIN32)
    if (BCryptGenRandom(nullptr, out, static_cast<ULONG>(size), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
        fail(error, "BCryptGenRandom failed");
        return false;
    }
    return true;
#elif defined(__APPLE__)
    (void)error;
    arc4random_buf(out, size);
    return true;
#else
    size_t got = 0;
    while (got < size) {
        const ssize_t n = getrandom(out + got, size - got, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            fail(error, std::string("getrandom failed: ") + std::strerror(errno));
            return false;
        }
        got += static_cast<size_t>(n);
    }
    return true;
#endif
}

} // namespace

std::string toHex(const uint8_t* data, size_t size) {
    static const char* digits = "0123456789abcdef";
    std::string s(size * 2, '0');
    for (size_t i = 0; i < size; ++i) {
        s[2 * i] = digits[data[i] >> 4];
        s[2 * i + 1] = digits[data[i] & 15];
    }
    return s;
}

bool fromHex(const std::string& hex, uint8_t* out, size_t size) {
    if (hex.size() != size * 2) return false;
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (size_t i = 0; i < size; ++i) {
        const int hi = nibble(hex[2 * i]), lo = nibble(hex[2 * i + 1]);
        if (hi < 0 || lo < 0) return false;
        out[i] = static_cast<uint8_t>(hi << 4 | lo);
    }
    return true;
}

KeyPair keyPairFromSeed(const std::array<uint8_t, 32>& seed) {
    KeyPair k;
    std::array<uint8_t, 32> copy = seed; // Monocypher wipes the seed it's given
    crypto_eddsa_key_pair(k.secret.data(), k.publicKey.data(), copy.data());
    return k;
}

bool generateKeyPair(KeyPair& out, std::string* error) {
    std::array<uint8_t, 32> seed{};
    if (!osRandom(seed.data(), seed.size(), error)) return false;
    out = keyPairFromSeed(seed);
    crypto_wipe(seed.data(), seed.size());
    return true;
}

Hash hashBytes(const void* data, size_t size) {
    Hash h{};
    crypto_blake2b(h.data(), h.size(), static_cast<const uint8_t*>(data), size);
    return h;
}

bool hashFile(const std::filesystem::path& file, Hash& out, std::string* error) {
    std::ifstream f(file, std::ios::binary);
    if (!f) {
        fail(error, "can't open " + file.generic_string());
        return false;
    }
    crypto_blake2b_ctx ctx;
    crypto_blake2b_init(&ctx, out.size());
    std::vector<char> buf(1 << 16);
    while (f) {
        f.read(buf.data(), static_cast<std::streamsize>(buf.size()));
        const std::streamsize n = f.gcount();
        if (n > 0) crypto_blake2b_update(&ctx, reinterpret_cast<const uint8_t*>(buf.data()), static_cast<size_t>(n));
    }
    if (f.bad()) {
        fail(error, "read error in " + file.generic_string());
        return false;
    }
    crypto_blake2b_final(&ctx, out.data());
    return true;
}

bool build(const std::filesystem::path& root, Manifest& out, std::string* error) {
    out = Manifest{};
    std::error_code ec;
    if (!std::filesystem::is_directory(root, ec)) {
        fail(error, "not a folder: " + root.generic_string());
        return false;
    }
    std::filesystem::recursive_directory_iterator it(root, std::filesystem::directory_options::none, ec), end;
    if (ec) {
        fail(error, "can't list " + root.generic_string() + ": " + ec.message());
        return false;
    }
    for (; it != end; it.increment(ec)) {
        if (ec) {
            fail(error, "can't list " + root.generic_string() + ": " + ec.message());
            return false;
        }
        const std::filesystem::directory_entry& e = *it;
        if (e.is_symlink(ec) || !e.is_regular_file(ec)) continue;
        const std::string rel = std::filesystem::relative(e.path(), root, ec).generic_string();
        if (ec || rel.empty()) {
            fail(error, "can't make a relative path for " + e.path().generic_string());
            return false;
        }
        if (rel == kSealFileName) continue;
        Hash h{};
        if (!hashFile(e.path(), h, error)) return false;
        out.files[rel] = h;
    }
    if (ec) {
        fail(error, "can't list " + root.generic_string() + ": " + ec.message());
        return false;
    }
    return true;
}

std::string canonical(const Manifest& m) {
    std::string s = std::string(kFormat) + "\n";
    for (const auto& [path, hash] : m.files) s += path + "\t" + toHex(hash) + "\n";
    return s;
}

Hash digest(const Manifest& m) {
    const std::string c = canonical(m);
    return hashBytes(c.data(), c.size());
}

void sign(Manifest& m, const SecretKey& secret) {
    const std::string c = canonical(m);
    Signature sig{};
    crypto_eddsa_sign(sig.data(), secret.data(), reinterpret_cast<const uint8_t*>(c.data()), c.size());
    m.signature = sig;
}

bool signatureValid(const Manifest& m, const PublicKey& publicKey) {
    if (!m.signature) return false;
    const std::string c = canonical(m);
    return crypto_eddsa_check(m.signature->data(), publicKey.data(), reinterpret_cast<const uint8_t*>(c.data()), c.size()) == 0;
}

std::string toJson(const Manifest& m) {
    nlohmann::ordered_json j;
    j["format"] = kFormat;
    nlohmann::ordered_json files = nlohmann::ordered_json::object();
    for (const auto& [path, hash] : m.files) files[path] = toHex(hash);
    j["files"] = files;
    if (m.signature) j["signature"] = toHex(*m.signature);
    return j.dump(2) + "\n";
}

bool fromJson(const std::string& text, Manifest& out, std::string* error) {
    out = Manifest{};
    nlohmann::json j = nlohmann::json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        fail(error, "not valid JSON");
        return false;
    }
    if (!j.contains("format") || !j["format"].is_string() || j["format"].get<std::string>() != kFormat) {
        fail(error, std::string("not a ") + kFormat + " seal");
        return false;
    }
    if (!j.contains("files") || !j["files"].is_object()) {
        fail(error, "no \"files\" object");
        return false;
    }
    for (const auto& [path, value] : j["files"].items()) {
        Hash h{};
        if (!value.is_string() || !fromHex(value.get<std::string>(), h)) {
            fail(error, "bad hash for " + path);
            return false;
        }
        out.files[path] = h;
    }
    if (j.contains("signature")) {
        Signature sig{};
        if (!j["signature"].is_string() || !fromHex(j["signature"].get<std::string>(), sig)) {
            fail(error, "bad signature field");
            return false;
        }
        out.signature = sig;
    }
    return true;
}

bool save(const Manifest& m, const std::filesystem::path& file, std::string* error) {
    std::ofstream f(file, std::ios::binary | std::ios::trunc);
    if (!f) {
        fail(error, "can't write " + file.generic_string());
        return false;
    }
    f << toJson(m);
    f.flush();
    if (!f) {
        fail(error, "write error in " + file.generic_string());
        return false;
    }
    return true;
}

bool load(const std::filesystem::path& file, Manifest& out, std::string* error) {
    std::ifstream f(file, std::ios::binary);
    if (!f) {
        fail(error, "can't open " + file.generic_string());
        return false;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    if (f.bad()) {
        fail(error, "read error in " + file.generic_string());
        return false;
    }
    std::string why;
    if (!fromJson(ss.str(), out, &why)) {
        fail(error, file.generic_string() + ": " + why);
        return false;
    }
    return true;
}

Report verify(const std::filesystem::path& root, const PublicKey& publicKey) {
    Report r;
    Manifest sealed;
    std::error_code ec;
    if (!std::filesystem::exists(root / kSealFileName, ec)) {
        r.error = "no " + std::string(kSealFileName) + " in " + root.generic_string();
        return r;
    }
    r.sealFound = true;
    if (!load(root / kSealFileName, sealed, &r.error)) return r;
    r.signatureOk = signatureValid(sealed, publicKey);
    Manifest actual;
    if (!build(root, actual, &r.error)) return r;
    for (const auto& [path, hash] : sealed.files) {
        auto it = actual.files.find(path);
        if (it == actual.files.end()) r.missing.push_back(path);
        else if (it->second != hash) r.modified.push_back(path);
    }
    for (const auto& [path, hash] : actual.files)
        if (!sealed.files.count(path)) r.added.push_back(path);
    return r;
}

std::string Report::summary() const {
    if (!error.empty()) return "seal not checked: " + error;
    if (ok()) return "seal ok";
    std::string s = signatureOk ? "seal broken:" : "seal signature INVALID:";
    auto list = [&](const char* what, const std::vector<std::string>& v) {
        if (v.empty()) return;
        s += " " + std::to_string(v.size()) + " " + what + " (" + v.front() + (v.size() > 1 ? ", ..." : "") + ")";
    };
    list("modified", modified);
    list("missing", missing);
    list("added", added);
    return s;
}

} // namespace kke::seal
