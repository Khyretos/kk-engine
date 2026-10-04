#pragma once

// Everything in the fight that is data (data/foes.yml, heroes.yml,
// waves.yml): the goblin types and bosses with their moves, the
// characters, accessories and weapons a player picks, the waves. Pure
// parsing, no engine state: games/goblin_horde/README.md "Data".

#include "kke/Combat.h"

#include <nlohmann/json_fwd.hpp>

#include <glm/glm.hpp>

#include <map>
#include <string>
#include <vector>

namespace horde {

// A model or prop in an art pack: `file` without extension, `part` a mesh
// part of it (Goblin War Camp keeps every goblin in one Characters.fbx).
struct ArtRef {
    std::string pack, file, part;
    std::string grip; // props: "" (a one-handed weapon), "twohand", "staff", "bow", "crossbow"
    bool empty() const { return file.empty(); }
};

// One thing a fighter can do: the swing (kke::AttackDesc), the clip it
// plays, and what kind of move it is.
struct Move {
    enum class Kind { Melee, Dash, Combo, Shot, Lob, Blast, Slam, Buff, Heal };
    std::string id;
    Kind kind = Kind::Melee;
    kke::AttackDesc hit;
    std::vector<std::string> clips;    // the first one the rig has (Combo: one per hit)
    std::vector<std::string> aimClips; // held while winding up a shot
    std::vector<std::string> roarClips;// a super attack's wind-up
    float range = 1.5f, minRange = 0.0f, cooldown = 2.0f;
    std::string projectile;            // arrow, bolt, fireball, zap, boulder
    float speed = 20.0f, splash = 0.0f, homing = 0.0f;
    int count = 1;
    float spread = 0.0f;               // Lob: metres the shots scatter round the mark
    float telegraph = 0.0f;            // s the mark shows on the ground before it lands
    float radius = 0.0f;               // Lob, Blast, Slam, Buff: metres
    float lunge = 0.0f;                // Dash: metres it carries the fighter
    std::string buff;                  // Buff: haste, stoneskin
    float duration = 0.0f, amount = 0.0f;
    std::string element;               // Blast: fire
    bool super = false;                // a boss's super attack (a bigger warning)
    float charge = 0.0f;               // a player's spin: s heavy is held first
    float total() const { return hit.windup + hit.active + hit.recovery; }
};

struct FoeType {
    std::string id, label;
    bool boss = false;
    float share = 1.0f;
    std::vector<ArtRef> models, weapons, offhands;
    ArtRef back;
    glm::vec2 scale{ 0.8f, 0.8f };
    float health = 1.0f, poise = 1.0f, speed = 1.0f, keep = 0.0f;
    glm::vec3 tint{ 1.0f };
    std::vector<Move> moves;
};

struct HeroCharacter {
    std::string id, label;
    ArtRef art; // empty: the UAL mannequin
};

struct Accessory {
    std::string id, label;
    ArtRef art;
    std::string on = "head"; // head, back
};

struct Weapon {
    enum class Kind { OneHand, TwoHand, Bow, Crossbow };
    std::string id, label;
    Kind kind = Kind::OneHand;
    ArtRef prop, arrow;
    std::vector<Move> combo;
    Move heavy, spin, kick;
    // Bow: draw time and damage / speed from a quick release to a full draw.
    float draw = 1.0f;
    float minDamage = 8.0f, maxDamage = 40.0f, minSpeed = 20.0f, maxSpeed = 60.0f;
    // Crossbow: the same every time.
    float damage = 30.0f, speed = 60.0f, reload = 1.6f;
    float headshot = 2.0f;
    bool ranged() const { return kind == Kind::Bow || kind == Kind::Crossbow; }
};

struct Wave {
    int goblins = 8;     // in the wave
    int atOnce = 8;      // alive at the same time (capped by KKE_HORDE_MAX)
    float speed = 1.0f;  // run speed multiplier
    float health = 1.0f; // health multiplier
    std::map<std::string, float> mix; // type -> how common (missing: the type's share)
};

struct Roster {
    std::vector<FoeType> types, bosses;
    std::vector<HeroCharacter> characters;
    std::vector<Accessory> accessories;
    std::vector<Weapon> weapons;
    std::vector<Wave> waves;
    int attackers = 4;
    float breather = 5.0f;
    int bossEvery = 5;
    float perPlayer = 0.6f;

    // Each reads its file's JSON (kke::datafile); false + `error` if it
    // isn't what's expected. Unknown keys are ignored.
    bool readFoes(const nlohmann::json& j, std::string* error);
    bool readHeroes(const nlohmann::json& j, std::string* error);
    bool readWaves(const nlohmann::json& j, std::string* error);

    // Wave n (1-based); past the list the last one, bigger each time.
    Wave waveAt(int n) const;
    // Does wave n bring a boss, and which (index into bosses; -1 none).
    int bossOf(int n) const;
    int weaponIndex(const std::string& id) const;
    const FoeType* type(const std::string& id) const;
};

Move readMove(const nlohmann::json& j);

} // namespace horde
