// kke_license: Kreative DRM licences, for developers who choose to use
// them (kke/License.h, docs/DRM.md). Keys are kke_seal's.
//
//   kke_license machine <game>                  this machine's id for that game
//   kke_license issue <game> <licence-id> [options] > licence.json
//       --key FILE        secret key (or KKE_SEAL_KEY)
//       --machine ID      allow this machine (repeat for more; none = any)
//       --expires UNIX    end time in unix seconds (default: never)
//       --owner NAME      shown to the player
//       --extra K=V       the developer's own terms (repeatable)
//       --unlock-all      every copy may run: the end-of-life release
//   kke_license verify <licence.json> <public-key-hex> <game> [--machine ID]

#include "kke/License.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

using namespace kke;

namespace {

int usage() {
    std::fprintf(stderr,
                 "usage:\n"
                 "  kke_license machine <game>\n"
                 "  kke_license issue <game> <licence-id> [--key FILE] [--machine ID]... [--expires UNIX] [--owner NAME] [--extra K=V]... [--unlock-all]\n"
                 "  kke_license verify <licence.json> <public-key-hex> <game> [--machine ID]\n");
    return 2;
}

std::string trim(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
    return s;
}

bool readSecret(const std::string& file, seal::SecretKey& out) {
    std::string hex;
    if (!file.empty()) {
        std::ifstream f(file);
        if (!f) {
            std::fprintf(stderr, "can't open %s\n", file.c_str());
            return false;
        }
        std::ostringstream ss;
        ss << f.rdbuf();
        hex = trim(ss.str());
    } else if (const char* v = std::getenv("KKE_SEAL_KEY")) {
        hex = trim(v);
    } else {
        std::fprintf(stderr, "no secret key: pass --key FILE or set KKE_SEAL_KEY\n");
        return false;
    }
    if (!seal::fromHex(hex, out)) {
        std::fprintf(stderr, "the secret key must be 128 hex digits\n");
        return false;
    }
    return true;
}

bool parseInt(const char* s, int64_t& out) {
    char* end = nullptr;
    const long long v = std::strtoll(s, &end, 10);
    if (!end || *end || v < 0) return false;
    out = v;
    return true;
}

int64_t unixNow() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

int issue(int argc, char** argv) {
    license::License l;
    l.game = argv[2];
    l.licenseId = argv[3];
    l.issued = unixNow();
    std::string keyFile;
    for (int i = 4; i < argc; ++i) {
        const std::string a = argv[i];
        const bool hasValue = i + 1 < argc;
        if (a == "--unlock-all") l.unlockAll = true;
        else if (a == "--key" && hasValue) keyFile = argv[++i];
        else if (a == "--machine" && hasValue) l.machines.push_back(argv[++i]);
        else if (a == "--owner" && hasValue) l.owner = argv[++i];
        else if (a == "--expires" && hasValue) {
            if (!parseInt(argv[++i], l.expires)) {
                std::fprintf(stderr, "--expires takes unix seconds\n");
                return 2;
            }
        } else if (a == "--extra" && hasValue) {
            const std::string kv = argv[++i];
            const size_t eq = kv.find('=');
            if (eq == std::string::npos || eq == 0) {
                std::fprintf(stderr, "--extra takes KEY=VALUE\n");
                return 2;
            }
            l.extra[kv.substr(0, eq)] = kv.substr(eq + 1);
        } else {
            return usage();
        }
    }
    seal::SecretKey secret{};
    if (!readSecret(keyFile, secret)) return 1;
    license::sign(l, secret);
    std::fputs(license::toJson(l).c_str(), stdout);
    return 0;
}

int verifyCmd(int argc, char** argv) {
    seal::PublicKey pk{};
    if (!seal::fromHex(argv[3], pk)) {
        std::fprintf(stderr, "the public key must be 64 hex digits\n");
        return 2;
    }
    std::string machine = license::machineId(argv[4]);
    for (int i = 5; i < argc; ++i) {
        if (std::string(argv[i]) == "--machine" && i + 1 < argc) machine = argv[++i];
        else return usage();
    }
    license::License l;
    std::string error;
    if (!license::load(argv[2], l, &error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    const license::Check c = license::verify(l, pk, argv[4], machine, unixNow());
    std::printf("%s: %s\n", license::toString(c.status), c.detail.c_str());
    return c.ok() ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) return usage();
    const std::string cmd = argv[1];
    if (cmd == "machine" && argc == 3) {
        const std::string id = license::machineId(argv[2]);
        if (id.empty()) {
            std::fprintf(stderr, "no machine id readable on this system\n");
            return 1;
        }
        std::printf("%s\n", id.c_str());
        return 0;
    }
    if (cmd == "issue" && argc >= 4) return issue(argc, argv);
    if (cmd == "verify" && argc >= 5) return verifyCmd(argc, argv);
    return usage();
}
