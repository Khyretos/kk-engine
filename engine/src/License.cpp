#include "kke/License.h"

#include "kke/DataFile.h"
#include "kke/Platform.h"

#include <monocypher.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <sstream>


namespace kke::license {

namespace {

constexpr const char* kFormat = "kke-license-1";

void fail(std::string* error, const std::string& what) {
    if (error) *error = what;
}

std::string rawMachineId() { return platform::machineId(); }

// Length-prefixed, so no field can run into the next.
void field(std::string& out, const std::string& name, const std::string& value) {
    out += name + ":" + std::to_string(value.size()) + ":" + value + "\n";
}

} // namespace

const char* toString(Status s) {
    switch (s) {
    case Status::Valid: return "valid";
    case Status::BadSignature: return "bad signature";
    case Status::WrongGame: return "for another game";
    case Status::Expired: return "expired";
    case Status::NewMachine: return "not activated on this machine";
    case Status::Malformed: return "unreadable";
    }
    return "unknown";
}

std::string machineId(const std::string& game) {
    const std::string raw = rawMachineId();
    if (raw.empty()) return {};
    // Keyed hash: the game id is the key, so each game sees a different id.
    uint8_t out[16];
    crypto_blake2b_keyed(out, sizeof(out), reinterpret_cast<const uint8_t*>(game.data()), std::min<size_t>(game.size(), 64),
                         reinterpret_cast<const uint8_t*>(raw.data()), raw.size());
    return seal::toHex(out, sizeof(out));
}

std::string canonical(const License& l) {
    std::string s = std::string(kFormat) + "\n";
    field(s, "game", l.game);
    field(s, "license", l.licenseId);
    field(s, "owner", l.owner);
    field(s, "issued", std::to_string(l.issued));
    field(s, "expires", std::to_string(l.expires));
    field(s, "unlockAll", l.unlockAll ? "1" : "0");
    for (const std::string& m : l.machines) field(s, "machine", m);
    for (const auto& [k, v] : l.extra) field(s, "extra." + k, v);
    return s;
}

void sign(License& l, const seal::SecretKey& secret) {
    const std::string c = canonical(l);
    seal::Signature sig{};
    crypto_eddsa_sign(sig.data(), secret.data(), reinterpret_cast<const uint8_t*>(c.data()), c.size());
    l.signature = sig;
}

Check verify(const License& l, const seal::PublicKey& publicKey, const std::string& game, const std::string& machine, int64_t now) {
    if (!l.signature) return { Status::BadSignature, "the licence isn't signed" };
    const std::string c = canonical(l);
    if (crypto_eddsa_check(l.signature->data(), publicKey.data(), reinterpret_cast<const uint8_t*>(c.data()), c.size()) != 0)
        return { Status::BadSignature, "the licence wasn't signed by this game's developer, or was changed" };
    if (l.game != game) return { Status::WrongGame, "this licence is for \"" + l.game + "\"" };
    if (l.expires != 0 && now >= l.expires) return { Status::Expired, "this licence ended" };
    if (l.unlockAll) return { Status::Valid, "unlocked by the developer" };
    if (!l.machines.empty() && std::find(l.machines.begin(), l.machines.end(), machine) == l.machines.end())
        return { Status::NewMachine, "this licence was activated on another machine" };
    return { Status::Valid, "licence " + l.licenseId };
}

std::string toJson(const License& l) {
    nlohmann::ordered_json j;
    j["format"] = kFormat;
    j["game"] = l.game;
    j["license"] = l.licenseId;
    if (!l.owner.empty()) j["owner"] = l.owner;
    j["issued"] = l.issued;
    j["expires"] = l.expires;
    j["machines"] = l.machines;
    if (l.unlockAll) j["unlockAll"] = true;
    if (!l.extra.empty()) j["extra"] = l.extra;
    if (l.signature) j["signature"] = seal::toHex(*l.signature);
    return j.dump(2) + "\n";
}

bool fromJson(const std::string& text, License& out, std::string* error) {
    out = License{};
    nlohmann::json j;
    const bool parsed = datafile::parseAny(text, j); // JSON or YAML: the signature covers the fields, not the text
    if (!parsed || !j.is_object()) {
        fail(error, "not valid JSON or YAML");
        return false;
    }
    auto str = [&](const char* key, std::string& dst, bool required) {
        if (!j.contains(key)) return !required;
        if (!j[key].is_string()) return false;
        dst = j[key].get<std::string>();
        return true;
    };
    auto integer = [&](const char* key, int64_t& dst) {
        if (!j.contains(key)) return true;
        if (!j[key].is_number_integer()) return false;
        dst = j[key].get<int64_t>();
        return true;
    };
    std::string format;
    if (!str("format", format, true) || format != kFormat) {
        fail(error, std::string("not a ") + kFormat + " licence");
        return false;
    }
    if (!str("game", out.game, true) || !str("license", out.licenseId, true) || !str("owner", out.owner, false) ||
        !integer("issued", out.issued) || !integer("expires", out.expires)) {
        fail(error, "missing or mistyped game, license, owner, issued or expires");
        return false;
    }
    if (j.contains("machines")) {
        if (!j["machines"].is_array()) {
            fail(error, "\"machines\" must be a list");
            return false;
        }
        for (const nlohmann::json& m : j["machines"]) {
            if (!m.is_string()) {
                fail(error, "\"machines\" must be a list of strings");
                return false;
            }
            out.machines.push_back(m.get<std::string>());
        }
    }
    if (j.contains("unlockAll")) {
        if (!j["unlockAll"].is_boolean()) {
            fail(error, "\"unlockAll\" must be true or false");
            return false;
        }
        out.unlockAll = j["unlockAll"].get<bool>();
    }
    if (j.contains("extra")) {
        if (!j["extra"].is_object()) {
            fail(error, "\"extra\" must be an object of strings");
            return false;
        }
        for (const auto& [k, v] : j["extra"].items()) {
            if (!v.is_string()) {
                fail(error, "\"extra\" must be an object of strings");
                return false;
            }
            out.extra[k] = v.get<std::string>();
        }
    }
    if (j.contains("signature")) {
        seal::Signature sig{};
        if (!j["signature"].is_string() || !seal::fromHex(j["signature"].get<std::string>(), sig)) {
            fail(error, "bad signature field");
            return false;
        }
        out.signature = sig;
    }
    return true;
}

bool save(const License& l, const std::filesystem::path& file, std::string* error) {
    std::ofstream f(file, std::ios::binary | std::ios::trunc);
    if (!f) {
        fail(error, "can't write " + file.generic_string());
        return false;
    }
    f << toJson(l);
    f.flush();
    if (!f) {
        fail(error, "write error in " + file.generic_string());
        return false;
    }
    return true;
}

bool load(const std::filesystem::path& file, License& out, std::string* error) {
    std::ifstream f(file, std::ios::binary);
    if (!f) {
        fail(error, "can't open " + file.generic_string());
        return false;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    std::string why;
    if (f.bad() || !fromJson(ss.str(), out, &why)) {
        fail(error, file.generic_string() + ": " + (why.empty() ? "read error" : why));
        return false;
    }
    return true;
}

Check verifyFile(const std::filesystem::path& file, const seal::PublicKey& publicKey, const std::string& game) {
    License l;
    std::string error;
    if (!load(file, l, &error)) return { Status::Malformed, error };
    const int64_t now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    return verify(l, publicKey, game, machineId(game), now);
}

} // namespace kke::license
