#pragma once

#include "kke/PackSeal.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace kke::license {

// Kreative DRM (docs/DRM.md): optional licences for developers who want
// them. The engine never turns this on by itself and never requires it;
// a game without it runs everywhere, copied or not.
//
// A licence is a small signed file: this licence id, for this game, on
// these machines (or any), until this date (or forever). The developer's
// activation service signs it with the developer's private key (EdDSA,
// the same keys as kke_seal); the game checks it with the public key
// compiled in. After one activation it works offline, forever.
//
// It is a lock on the front door, not a vault: someone determined can
// patch the check out of their copy. What it gives the developer is the
// camera: which licences were activated, how often, and on how many
// machines. The developer decides what to do about a new machine (ask
// "is this you?", allow N machines, or nothing).
//
// And the developer keeps the game alive: an unlock licence (unlockAll)
// signed with the same key makes every copy valid without a server, for
// when the activation service shuts down.
struct License {
    std::string game;                  // the game's id (GameManifest::id)
    std::string licenseId;             // the key the player bought
    std::string owner;                 // optional, shown to the player
    int64_t issued = 0;                // unix seconds
    int64_t expires = 0;               // unix seconds; 0 = never
    std::vector<std::string> machines; // machineId()s allowed; empty = any machine
    bool unlockAll = false;            // "every copy may run": the end-of-life release
    std::map<std::string, std::string> extra; // the developer's own terms (edition, platform, ...)
    std::optional<seal::Signature> signature;
};

enum class Status { Valid, BadSignature, WrongGame, Expired, NewMachine, Malformed };
const char* toString(Status s);

struct Check {
    Status status = Status::Malformed;
    std::string detail;
    bool ok() const { return status == Status::Valid; }
};

// This machine, as seen by one game: a hash of the OS's machine id (Linux
// /etc/machine-id, Windows MachineGuid, macOS hardware UUID) salted with
// the game id, so two games (or two developers) can't match players up,
// and the raw id never leaves the machine. Empty if none can be read.
std::string machineId(const std::string& game);

// The exact bytes that are signed (stable across platforms).
std::string canonical(const License& l);
void sign(License& l, const seal::SecretKey& secret);
// Checks signature, game, expiry, then machine. `now` in unix seconds.
Check verify(const License& l, const seal::PublicKey& publicKey, const std::string& game, const std::string& machine, int64_t now);

std::string toJson(const License& l);
bool fromJson(const std::string& json, License& out, std::string* error = nullptr);
bool save(const License& l, const std::filesystem::path& file, std::string* error = nullptr);
bool load(const std::filesystem::path& file, License& out, std::string* error = nullptr);

// Loads `file` and verifies it for this machine and the current time; a
// missing or unreadable file is Malformed with the reason.
Check verifyFile(const std::filesystem::path& file, const seal::PublicKey& publicKey, const std::string& game);

} // namespace kke::license
