// kke_seal: seals a game's data folder so tampering shows
// (kke/PackSeal.h, docs/ANTI_CHEAT.md "Sealed game data").
//
//   kke_seal keygen <secret-key-file>      new key pair; prints the public key
//   kke_seal sign <folder> [secret-key-file]  writes <folder>/kke.seal
//                                            (key from the file, or the
//                                            KKE_SEAL_KEY variable in CI)
//   kke_seal verify <folder> <public-key-hex> exit 0 when intact
//   kke_seal digest <folder>                 the data set's digest (hex)
//
// The secret key file holds 128 hex digits. Keep it out of the repo: in
// CI, store it as a secret and pass it as KKE_SEAL_KEY.

#include "kke/PackSeal.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace kke::seal;

namespace {

int usage() {
    std::fprintf(stderr,
                 "usage:\n"
                 "  kke_seal keygen <secret-key-file>\n"
                 "  kke_seal sign <folder> [secret-key-file]   (or KKE_SEAL_KEY=<hex>)\n"
                 "  kke_seal verify <folder> <public-key-hex>\n"
                 "  kke_seal digest <folder>\n");
    return 2;
}

std::string trim(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
    return s;
}

bool readSecret(const char* file, SecretKey& out, std::string& error) {
    std::string hex;
    if (file) {
        std::ifstream f(file);
        if (!f) {
            error = std::string("can't open ") + file;
            return false;
        }
        std::ostringstream ss;
        ss << f.rdbuf();
        hex = trim(ss.str());
    } else if (const char* v = std::getenv("KKE_SEAL_KEY")) {
        hex = trim(v);
    } else {
        error = "no secret key: pass a key file or set KKE_SEAL_KEY";
        return false;
    }
    if (!fromHex(hex, out)) {
        error = "the secret key must be 128 hex digits";
        return false;
    }
    return true;
}

int keygen(const char* file) {
    std::error_code ec;
    if (std::filesystem::exists(file, ec)) {
        std::fprintf(stderr, "%s exists: not overwriting a key\n", file);
        return 1;
    }
    KeyPair k;
    std::string error;
    if (!generateKeyPair(k, &error)) {
        std::fprintf(stderr, "keygen failed: %s\n", error.c_str());
        return 1;
    }
    {
        std::ofstream f(file, std::ios::trunc);
        if (!f) {
            std::fprintf(stderr, "can't write %s\n", file);
            return 1;
        }
        f << toHex(k.secret) << "\n";
        f.flush();
        if (!f) {
            std::fprintf(stderr, "write error in %s\n", file);
            return 1;
        }
    }
    std::filesystem::permissions(file, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                                 std::filesystem::perm_options::replace, ec);
    if (ec) std::fprintf(stderr, "note: couldn't restrict %s to its owner: %s\n", file, ec.message().c_str());
    std::printf("public key: %s\n", toHex(k.publicKey).c_str());
    std::printf("secret key written to %s (keep it private, never commit it)\n", file);
    return 0;
}

int sign(const char* folder, const char* keyFile) {
    SecretKey secret{};
    std::string error;
    if (!readSecret(keyFile, secret, error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    Manifest m;
    if (!build(folder, m, &error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    kke::seal::sign(m, secret);
    const std::filesystem::path out = std::filesystem::path(folder) / kSealFileName;
    if (!save(m, out, &error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    std::printf("sealed %zu file(s) in %s\ndigest %s\n", m.files.size(), out.generic_string().c_str(), toHex(digest(m)).c_str());
    return 0;
}

int verifyCmd(const char* folder, const char* publicHex) {
    PublicKey pk{};
    if (!fromHex(publicHex, pk)) {
        std::fprintf(stderr, "the public key must be 64 hex digits\n");
        return 2;
    }
    const Report r = verify(folder, pk);
    std::printf("%s\n", r.summary().c_str());
    for (const std::string& p : r.modified) std::printf("  modified %s\n", p.c_str());
    for (const std::string& p : r.missing) std::printf("  missing  %s\n", p.c_str());
    for (const std::string& p : r.added) std::printf("  added    %s\n", p.c_str());
    return r.ok() ? 0 : 1;
}

int digestCmd(const char* folder) {
    Manifest m;
    std::string error;
    if (!build(folder, m, &error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    std::printf("%s\n", toHex(digest(m)).c_str());
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) return usage();
    const std::string cmd = argv[1];
    if (cmd == "keygen" && argc == 3) return keygen(argv[2]);
    if (cmd == "sign" && (argc == 3 || argc == 4)) return sign(argv[2], argc == 4 ? argv[3] : nullptr);
    if (cmd == "verify" && argc == 4) return verifyCmd(argv[2], argv[3]);
    if (cmd == "digest" && argc == 3) return digestCmd(argv[2]);
    return usage();
}
