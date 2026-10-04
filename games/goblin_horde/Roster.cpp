#include "Roster.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>

namespace horde {

namespace {

using nlohmann::json;

std::vector<std::string> strings(const json& j, const char* key) {
    std::vector<std::string> out;
    if (!j.contains(key)) return out;
    const json& v = j[key];
    if (v.is_string()) out.push_back(v.get<std::string>());
    else if (v.is_array())
        for (const json& s : v)
            if (s.is_string()) out.push_back(s.get<std::string>());
    return out;
}

ArtRef readArt(const json& j) {
    ArtRef a;
    if (!j.is_object()) return a;
    a.pack = j.value("pack", "");
    a.file = j.value("file", "");
    a.part = j.value("part", "");
    a.grip = j.value("grip", "");
    return a;
}

std::vector<ArtRef> readArts(const json& j, const char* key) {
    std::vector<ArtRef> out;
    if (!j.contains(key)) return out;
    const json& v = j[key];
    if (v.is_object()) out.push_back(readArt(v));
    else if (v.is_array())
        for (const json& a : v)
            if (ArtRef r = readArt(a); !r.empty()) out.push_back(r);
    return out;
}

Move::Kind kindOf(const std::string& k) {
    if (k == "dash") return Move::Kind::Dash;
    if (k == "combo") return Move::Kind::Combo;
    if (k == "shot") return Move::Kind::Shot;
    if (k == "lob") return Move::Kind::Lob;
    if (k == "blast") return Move::Kind::Blast;
    if (k == "slam") return Move::Kind::Slam;
    if (k == "buff") return Move::Kind::Buff;
    if (k == "heal") return Move::Kind::Heal;
    return Move::Kind::Melee;
}

FoeType readFoe(const json& j, bool boss) {
    FoeType t;
    t.id = j.value("id", "");
    t.label = j.value("label", t.id);
    t.boss = boss;
    t.share = std::max(0.0f, j.value("share", 1.0f));
    t.models = readArts(j, "models");
    t.weapons = readArts(j, "weapon");
    t.offhands = readArts(j, "offhand");
    if (j.contains("back")) t.back = readArt(j["back"]);
    if (j.contains("scale") && j["scale"].is_array() && j["scale"].size() == 2)
        t.scale = { j["scale"][0].get<float>(), j["scale"][1].get<float>() };
    t.health = j.value("health", boss ? 900.0f : 1.0f);
    t.poise = j.value("poise", 1.0f);
    t.speed = j.value("speed", 1.0f);
    t.keep = j.value("keep", 0.0f);
    if (j.contains("tint") && j["tint"].is_array() && j["tint"].size() == 3)
        t.tint = { j["tint"][0].get<float>(), j["tint"][1].get<float>(), j["tint"][2].get<float>() };
    if (j.contains("moves") && j["moves"].is_array())
        for (const json& m : j["moves"])
            if (m.is_object()) t.moves.push_back(readMove(m));
    return t;
}

} // namespace

Move readMove(const json& j) {
    Move m;
    m.id = j.value("id", "");
    m.kind = kindOf(j.value("kind", "melee"));
    m.clips = strings(j, "clip");
    m.aimClips = strings(j, "aimClip");
    m.roarClips = strings(j, "roar");
    m.range = j.value("range", m.range);
    m.minRange = j.value("minRange", 0.0f);
    m.cooldown = j.value("cooldown", m.cooldown);
    m.projectile = j.value("projectile", "");
    m.speed = j.value("speed", m.speed);
    m.splash = j.value("splash", 0.0f);
    m.homing = j.value("homing", 0.0f);
    m.count = std::max(1, j.value("count", 1));
    m.spread = j.value("spread", 0.0f);
    m.telegraph = j.value("telegraph", 0.0f);
    m.radius = j.value("radius", 0.0f);
    m.lunge = j.value("lunge", 0.0f);
    m.buff = j.value("buff", "");
    m.duration = j.value("duration", 0.0f);
    m.amount = j.value("amount", 0.0f);
    m.element = j.value("element", "");
    m.super = j.value("super", false);
    m.charge = j.value("charge", 0.0f);

    kke::AttackDesc& a = m.hit;
    a = kke::AttackDesc::light();
    a.name = m.id;
    a.windup = j.value("windup", a.windup);
    a.active = j.value("active", a.active);
    a.recovery = j.value("recovery", a.recovery);
    a.damage = j.value("damage", a.damage);
    a.staminaCost = j.value("stamina", 0.0f);
    a.poiseDamage = j.value("poise", a.poiseDamage);
    a.reach = j.value("reach", a.reach);
    a.height = j.value("height", a.height);
    // A circle on the ground (a slam) is the hit sphere too.
    a.radius = j.value("radius", a.radius);
    a.knockback = j.value("knockback", a.knockback);
    a.hitStun = j.value("stun", a.hitStun);
    a.chip = j.value("chip", a.chip);
    a.guardDamage = j.value("guard", a.guardDamage);
    a.unblockable = j.value("unblockable", false);
    a.sweep = j.value("sweep", m.kind == Move::Kind::Slam || m.kind == Move::Kind::Combo);
    return m;
}

bool Roster::readFoes(const json& j, std::string* error) {
    types.clear();
    bosses.clear();
    if (!j.is_object() || !j.contains("types") || !j["types"].is_array()) {
        if (error) *error = "no `types` list";
        return false;
    }
    for (const json& t : j["types"])
        if (t.is_object()) types.push_back(readFoe(t, false));
    if (j.contains("bosses") && j["bosses"].is_array())
        for (const json& t : j["bosses"])
            if (t.is_object()) bosses.push_back(readFoe(t, true));
    types.erase(std::remove_if(types.begin(), types.end(), [](const FoeType& t) { return t.id.empty() || t.moves.empty(); }), types.end());
    bosses.erase(std::remove_if(bosses.begin(), bosses.end(), [](const FoeType& t) { return t.id.empty() || t.moves.empty(); }), bosses.end());
    if (types.empty()) {
        if (error) *error = "no goblin type with an id and moves";
        return false;
    }
    return true;
}

bool Roster::readHeroes(const json& j, std::string* error) {
    characters.clear();
    accessories.clear();
    weapons.clear();
    if (!j.is_object()) {
        if (error) *error = "not a table";
        return false;
    }
    if (j.contains("characters") && j["characters"].is_array())
        for (const json& c : j["characters"]) {
            if (!c.is_object()) continue;
            HeroCharacter h;
            h.id = c.value("id", "");
            h.label = c.value("label", h.id);
            h.art = readArt(c);
            if (!h.id.empty()) characters.push_back(h);
        }
    if (j.contains("accessories") && j["accessories"].is_array())
        for (const json& c : j["accessories"]) {
            if (!c.is_object()) continue;
            Accessory a;
            a.id = c.value("id", "");
            a.label = c.value("label", a.id);
            a.art = readArt(c);
            a.on = c.value("on", "head");
            if (!a.id.empty()) accessories.push_back(a);
        }
    if (j.contains("weapons") && j["weapons"].is_array())
        for (const json& w : j["weapons"]) {
            if (!w.is_object()) continue;
            Weapon x;
            x.id = w.value("id", "");
            x.label = w.value("label", x.id);
            const std::string kind = w.value("kind", "onehand");
            x.kind = kind == "twohand" ? Weapon::Kind::TwoHand : kind == "bow" ? Weapon::Kind::Bow : kind == "crossbow" ? Weapon::Kind::Crossbow : Weapon::Kind::OneHand;
            if (w.contains("prop")) x.prop = readArt(w["prop"]);
            if (w.contains("arrow")) x.arrow = readArt(w["arrow"]);
            if (w.contains("combo") && w["combo"].is_array())
                for (const json& m : w["combo"])
                    if (m.is_object()) {
                        Move mv = readMove(m);
                        mv.id = x.id + "_combo" + std::to_string(x.combo.size() + 1);
                        mv.hit.name = mv.id;
                        x.combo.push_back(mv);
                    }
            auto one = [&](const char* key, Move& out) {
                if (!w.contains(key) || !w[key].is_object()) return;
                out = readMove(w[key]);
                out.id = x.id + "_" + key;
                out.hit.name = out.id;
            };
            one("heavy", x.heavy);
            one("spin", x.spin);
            one("kick", x.kick);
            x.draw = std::max(0.1f, w.value("draw", x.draw));
            if (w.contains("min") && w["min"].is_object()) {
                x.minDamage = w["min"].value("damage", x.minDamage);
                x.minSpeed = w["min"].value("speed", x.minSpeed);
            }
            if (w.contains("max") && w["max"].is_object()) {
                x.maxDamage = w["max"].value("damage", x.maxDamage);
                x.maxSpeed = w["max"].value("speed", x.maxSpeed);
            }
            x.damage = w.value("damage", x.damage);
            x.speed = w.value("speed", x.speed);
            x.reload = std::max(0.1f, w.value("reload", x.reload));
            x.headshot = w.value("headshot", x.headshot);
            if (!x.id.empty()) weapons.push_back(x);
        }
    if (characters.empty() || weapons.empty()) {
        if (error) *error = "needs at least one character and one weapon";
        return false;
    }
    if (accessories.empty()) accessories.push_back({ "none", "None", {}, "head" });
    return true;
}

bool Roster::readWaves(const json& j, std::string* error) {
    waves.clear();
    if (!j.is_object()) {
        if (error) *error = "not a table";
        return false;
    }
    attackers = std::max(1, j.value("attackers", attackers));
    breather = std::max(0.0f, j.value("breather", breather));
    bossEvery = std::max(0, j.value("bossEvery", bossEvery));
    perPlayer = std::max(0.0f, j.value("perPlayer", perPlayer));
    if (j.contains("waves") && j["waves"].is_array())
        for (const json& w : j["waves"]) {
            if (!w.is_object()) continue;
            Wave wave;
            wave.goblins = std::max(1, w.value("goblins", wave.goblins));
            wave.atOnce = std::max(1, w.value("atOnce", wave.goblins));
            wave.speed = std::clamp(w.value("speed", 1.0f), 0.2f, 3.0f);
            wave.health = std::clamp(w.value("health", 1.0f), 0.1f, 20.0f);
            if (w.contains("mix") && w["mix"].is_object())
                for (auto it = w["mix"].begin(); it != w["mix"].end(); ++it)
                    if (it.value().is_number()) wave.mix[it.key()] = std::max(0.0f, it.value().get<float>());
            waves.push_back(wave);
        }
    if (waves.empty()) {
        waves.push_back(Wave{});
        if (error) *error = "no waves";
        return false;
    }
    return true;
}

Wave Roster::waveAt(int n) const {
    if (waves.empty()) return Wave{};
    const int last = static_cast<int>(waves.size());
    if (n <= last) return waves[static_cast<size_t>(std::max(1, n) - 1)];
    // Past the list: the last wave, half again bigger each time.
    Wave w = waves.back();
    const float grow = std::pow(1.5f, static_cast<float>(n - last));
    w.goblins = static_cast<int>(static_cast<float>(w.goblins) * grow);
    w.health *= 1.0f + 0.15f * static_cast<float>(n - last);
    w.speed = std::min(w.speed * (1.0f + 0.03f * static_cast<float>(n - last)), 1.6f);
    return w;
}

int Roster::bossOf(int n) const {
    if (bossEvery <= 0 || bosses.empty() || n <= 0 || n % bossEvery != 0) return -1;
    return (n / bossEvery - 1) % static_cast<int>(bosses.size());
}

int Roster::weaponIndex(const std::string& id) const {
    for (size_t i = 0; i < weapons.size(); ++i)
        if (weapons[i].id == id) return static_cast<int>(i);
    return -1;
}

const FoeType* Roster::type(const std::string& id) const {
    for (const FoeType& t : types)
        if (t.id == id) return &t;
    return nullptr;
}

} // namespace horde
