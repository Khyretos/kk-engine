#include "kke/ai/AiWorld.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>

namespace kke::ai {

namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}

UtilityAction action(std::string name, std::string behavior, float weight, std::vector<Consideration> cs, float momentum = 0.15f) {
    UtilityAction a;
    a.name = std::move(name);
    a.behavior = std::move(behavior);
    a.weight = weight;
    a.considerations = std::move(cs);
    a.momentum = momentum;
    return a;
}

} // namespace

const char* attitudeName(Attitude a) {
    switch (a) {
    case Attitude::Ignore: return "ignore";
    case Attitude::Friendly: return "friendly";
    case Attitude::Curious: return "curious";
    case Attitude::Fear: return "fear";
    case Attitude::Hostile: return "hostile";
    case Attitude::Hunt: return "hunt";
    }
    return "ignore";
}

bool attitudeFromName(const std::string& nameIn, Attitude& out) {
    const std::string name = lower(nameIn);
    for (Attitude a : { Attitude::Ignore, Attitude::Friendly, Attitude::Curious, Attitude::Fear, Attitude::Hostile, Attitude::Hunt }) {
        if (name == attitudeName(a)) {
            out = a;
            return true;
        }
    }
    return false;
}

const char* orderKindName(Order::Kind k) {
    switch (k) {
    case Order::Kind::None: return "none";
    case Order::Kind::MoveTo: return "moveto";
    case Order::Kind::Follow: return "follow";
    case Order::Kind::Attack: return "attack";
    case Order::Kind::Hold: return "hold";
    case Order::Kind::Flee: return "flee";
    case Order::Kind::Interact: return "interact";
    }
    return "none";
}

bool orderKindFromName(const std::string& nameIn, Order::Kind& out) {
    const std::string name = lower(nameIn);
    for (Order::Kind k : { Order::Kind::None, Order::Kind::MoveTo, Order::Kind::Follow, Order::Kind::Attack, Order::Kind::Hold,
                           Order::Kind::Flee, Order::Kind::Interact }) {
        if (name == orderKindName(k)) {
            out = k;
            return true;
        }
    }
    return false;
}

const char* eventKindName(AiEvent::Kind k) {
    switch (k) {
    case AiEvent::Kind::Spotted: return "Spotted";
    case AiEvent::Kind::Lost: return "Lost";
    case AiEvent::Kind::Heard: return "Heard";
    case AiEvent::Kind::Scared: return "Scared";
    case AiEvent::Kind::Calmed: return "Calmed";
    case AiEvent::Kind::Attack: return "Attack";
    case AiEvent::Kind::Arrived: return "Arrived";
    case AiEvent::Kind::ActionChanged: return "ActionChanged";
    }
    return "Spotted";
}

std::vector<UtilityAction> defaultActions(const Species& s) {
    const Curve on = Curve::step(0.5f);
    std::vector<UtilityAction> out{
        // Orders beat everything the agent wants by itself.
        action("order_moveto", "goto", 3.0f, { { "order_moveto", on } }),
        action("order_follow", "follow", 3.0f, { { "order_follow", on } }),
        action("order_attack", "attack", 3.0f, { { "order_attack", on } }),
        action("order_hold", "hold", 3.0f, { { "order_hold", on } }),
        action("order_flee", "flee", 3.0f, { { "order_flee", on } }),
        action("order_interact", "interact", 3.0f, { { "order_interact", on } }),
        // Survival: running from what it fears and fighting what it hates
        // outrank eating and wandering.
        action("flee", "flee", 2.0f, { { "fear", Curve::logistic(0.3f, 12.0f) } }, 0.35f),
        action("fight", "fight", 1.8f, { { "hostile", Curve::logistic(0.3f, 12.0f) } }, 0.3f),
    };
    if (!s.hunts.empty())
        out.push_back(action("hunt", "hunt", 1.5f, { { "prey", Curve::logistic(0.25f, 12.0f) }, { "hunger", Curve::linear(0.7f, 0.3f) } }, 0.3f));
    out.push_back(action("investigate", "investigate", 0.9f, { { "curious", Curve::logistic(0.3f, 10.0f) }, { "fear", Curve::inverse() } }));
    out.push_back(action("watch", "watch", 0.7f, { { "alert", Curve::linear() } }));
    if (!s.eats.empty())
        out.push_back(action("graze", "graze", 1.0f, { { "hunger", Curve::logistic(0.45f, 10.0f) }, { "food", Curve::step(0.5f) } }, 0.4f));
    if (!s.drinks.empty())
        out.push_back(action("drink", "drink", 1.0f, { { "thirst", Curve::logistic(0.5f, 10.0f) }, { "water", Curve::step(0.5f) } }, 0.4f));
    out.push_back(action("rest", "rest", 0.9f, { { "tiredness", Curve::logistic(0.7f, 12.0f) } }, 0.5f));
    if (s.flocks) out.push_back(action("regroup", "regroup", 0.6f, { { "alone", Curve::linear() } }));
    out.push_back(action("wander", "wander", 0.35f, { { "restless", Curve::linear() } }));
    out.push_back(action("idle", "idle", 0.3f, { { "restless", Curve::inverse() } }));
    return out;
}

std::vector<Species> builtinSpecies() {
    std::vector<Species> out;
    auto add = [&](Species s) { out.push_back(std::move(s)); };
    {
        Species s;
        s.id = "sheep";
        s.label = "Sheep";
        s.walkSpeed = 1.1f;
        s.runSpeed = 5.5f;
        s.radius = 0.45f;
        s.senses.sightRange = 22.0f;
        s.senses.fovDegrees = 300.0f;
        s.senses.hearing = 1.2f;
        s.temperament = { 0.15f, 0.35f, 0.0f, 0.95f };
        s.fears = { "dog", "fox", "wolf" };
        s.friends = { "farmer" };
        s.flocks = true;
        s.flockRadius = 7.0f;
        s.fearDistance = 14.0f;
        s.scent = 0.0f;
        add(s);
    }
    {
        Species s;
        s.id = "cow";
        s.label = "Cow";
        s.walkSpeed = 1.0f;
        s.runSpeed = 4.0f;
        s.acceleration = 4.0f;
        s.turnRate = 150.0f;
        s.radius = 0.8f;
        s.senses.fovDegrees = 300.0f;
        s.temperament = { 0.55f, 0.75f, 0.1f, 0.7f };
        s.fears = { "wolf" };
        s.flocks = true;
        s.flockRadius = 9.0f;
        s.flock.separationDistance = 1.5f;
        s.fearDistance = 8.0f;
        s.investigateDistance = 2.5f;
        s.scent = 0.0f;
        add(s);
    }
    {
        Species s;
        s.id = "pig";
        s.label = "Pig";
        s.walkSpeed = 0.9f;
        s.runSpeed = 3.5f;
        s.radius = 0.5f;
        s.senses.fovDegrees = 250.0f;
        s.senses.smell = 2.5f;
        s.temperament = { 0.4f, 0.65f, 0.05f, 0.4f };
        s.fears = { "dog", "wolf" };
        s.eats = { "grain", "grass" };
        s.fearDistance = 8.0f;
        s.scent = 0.0f;
        add(s);
    }
    {
        Species s;
        s.id = "chicken";
        s.label = "Chicken";
        s.walkSpeed = 0.8f;
        s.runSpeed = 3.2f;
        s.acceleration = 14.0f;
        s.turnRate = 720.0f;
        s.radius = 0.2f;
        s.senses.sightRange = 12.0f;
        s.senses.fovDegrees = 300.0f;
        s.temperament = { 0.05f, 0.3f, 0.0f, 0.6f };
        s.fears = { "dog", "fox", "cat", "wolf" };
        s.eats = { "grain" };
        s.flocks = true;
        s.flockRadius = 4.0f;
        s.flock.separationDistance = 0.3f;
        s.fearDistance = 6.0f;
        s.homeRadius = 12.0f;
        s.scent = 0.0f;
        s.footstepsWalk = 0.5f;
        s.footstepsRun = 2.0f;
        add(s);
    }
    {
        Species s;
        s.id = "horse";
        s.label = "Horse";
        s.walkSpeed = 1.6f;
        s.runSpeed = 8.0f;
        s.acceleration = 5.0f;
        s.turnRate = 180.0f;
        s.radius = 0.9f;
        s.senses.sightRange = 30.0f;
        s.senses.fovDegrees = 340.0f;
        s.temperament = { 0.35f, 0.5f, 0.1f, 0.7f };
        s.fears = { "dog", "wolf" };
        s.flocks = true;
        s.flockRadius = 12.0f;
        s.flock.separationDistance = 2.0f;
        s.fearDistance = 12.0f;
        s.scent = 0.0f;
        add(s);
    }
    {
        Species s;
        s.id = "goose";
        s.label = "Goose";
        s.walkSpeed = 0.9f;
        s.runSpeed = 2.8f;
        s.radius = 0.3f;
        s.senses.sightRange = 16.0f;
        s.temperament = { 0.8f, 0.3f, 0.85f, 0.7f };
        s.hostileTo = { "dog", "fox", "cat" };
        s.flocks = true;
        s.flockRadius = 5.0f;
        s.fearDistance = 7.0f; // how far it chases before giving up
        s.attackRange = 0.8f;
        s.attackCooldown = 1.0f;
        s.homeRadius = 15.0f;
        s.scent = 0.0f;
        add(s);
    }
    {
        Species s;
        s.id = "cat";
        s.label = "Cat";
        s.walkSpeed = 1.0f;
        s.runSpeed = 6.0f;
        s.acceleration = 16.0f;
        s.turnRate = 720.0f;
        s.radius = 0.25f;
        s.senses.fovDegrees = 200.0f;
        s.senses.smell = 1.5f;
        s.senses.hearing = 1.5f;
        s.temperament = { 0.3f, 0.8f, 0.2f, 0.0f };
        s.fears = { "dog", "fox" };
        s.hunts = { "mouse" };
        s.eats = { "milk" };
        s.fearDistance = 10.0f;
        s.homeRadius = 30.0f;
        s.footstepsWalk = 0.3f;
        s.footstepsRun = 1.0f;
        add(s);
    }
    {
        Species s;
        s.id = "dog";
        s.label = "Dog";
        s.walkSpeed = 1.5f;
        s.runSpeed = 7.0f;
        s.acceleration = 14.0f;
        s.turnRate = 540.0f;
        s.radius = 0.35f;
        s.senses.sightRange = 25.0f;
        s.senses.fovDegrees = 240.0f;
        s.senses.smell = 3.0f;
        s.senses.hearing = 1.5f;
        s.temperament = { 0.8f, 0.7f, 0.4f, 0.5f };
        s.friends = { "farmer" };
        s.hostileTo = { "fox", "wolf" };
        s.eats = { "dogfood" };
        s.footstepsWalk = 1.5f;
        s.footstepsRun = 6.0f;
        add(s);
    }
    {
        Species s;
        s.id = "fox";
        s.label = "Fox";
        s.walkSpeed = 1.2f;
        s.runSpeed = 7.0f;
        s.acceleration = 14.0f;
        s.turnRate = 540.0f;
        s.radius = 0.3f;
        s.senses.sightRange = 25.0f;
        s.senses.smell = 3.0f;
        s.senses.hearing = 1.5f;
        s.temperament = { 0.4f, 0.5f, 0.3f, 0.0f };
        s.fears = { "dog", "farmer", "wolf" };
        s.hunts = { "chicken" };
        s.eats = {};
        s.fearDistance = 18.0f;
        s.homeRadius = 60.0f;
        s.footstepsWalk = 0.6f;
        s.footstepsRun = 2.5f;
        add(s);
    }
    {
        Species s;
        s.id = "farmer";
        s.label = "Farmer";
        s.walkSpeed = 1.4f;
        s.runSpeed = 5.0f;
        s.radius = 0.35f;
        s.senses.sightRange = 30.0f;
        s.senses.fovDegrees = 180.0f;
        s.senses.smell = 0.0f;
        s.temperament = { 0.9f, 0.3f, 0.3f, 0.2f };
        s.friends = { "dog", "sheep", "cow", "pig", "chicken", "horse", "cat" };
        s.hostileTo = { "fox" };
        s.eats = {};
        s.drinks = {};
        s.needs = { { "tiredness", 1.0f / 900.0f, 0.1f } };
        s.footstepsWalk = 2.0f;
        s.footstepsRun = 6.0f;
        add(s);
    }
    return out;
}

// ---------------------------------------------------------------------------
// JSON / YAML

namespace {

template <typename T>
void get(const nlohmann::json& j, const char* key, T& out) {
    auto it = j.find(key);
    if (it != j.end() && !it->is_null()) out = it->get<T>();
}

bool curveFromJson(const nlohmann::json& j, Curve& c, std::string* error) {
    std::string kind = "linear";
    get(j, "curve", kind);
    if (!curveKindFromName(kind, c.kind)) {
        if (error) *error = "unknown curve \"" + kind + "\" (linear, inverse, quadratic, logistic, logit, step, constant)";
        return false;
    }
    if (lower(kind) == "inverse") c.invert = true;
    if (c.kind == Curve::Kind::Logistic) c = Curve::logistic();
    if (c.kind == Curve::Kind::Step) c = Curve::step();
    if (c.kind == Curve::Kind::Quadratic) c = Curve::quadratic();
    get(j, "m", c.m);
    get(j, "steepness", c.m);
    get(j, "slope", c.m);
    get(j, "k", c.k);
    get(j, "exponent", c.k);
    get(j, "c", c.c);
    get(j, "mid", c.c);
    get(j, "threshold", c.c);
    get(j, "b", c.b);
    get(j, "value", c.b);
    get(j, "invert", c.invert);
    return true;
}

nlohmann::json curveToJson(const Curve& c) {
    return { { "curve", curveKindName(c.kind) }, { "m", c.m }, { "k", c.k }, { "c", c.c }, { "b", c.b }, { "invert", c.invert } };
}

} // namespace

bool speciesFromJson(const nlohmann::json& j, Species& s, std::string* error) {
    if (!j.is_object()) {
        if (error) *error = "a species must be an object";
        return false;
    }
    try {
        get(j, "id", s.id);
        if (s.id.empty()) {
            if (error) *error = "a species needs an \"id\"";
            return false;
        }
        get(j, "label", s.label);
        if (s.label.empty()) s.label = s.id;
        get(j, "walkSpeed", s.walkSpeed);
        get(j, "runSpeed", s.runSpeed);
        get(j, "acceleration", s.acceleration);
        get(j, "turnRate", s.turnRate);
        get(j, "radius", s.radius);
        if (auto it = j.find("senses"); it != j.end() && it->is_object()) {
            const nlohmann::json& se = *it;
            get(se, "sightRange", s.senses.sightRange);
            get(se, "fov", s.senses.fovDegrees);
            get(se, "fovDegrees", s.senses.fovDegrees);
            get(se, "touchRange", s.senses.touchRange);
            get(se, "hearing", s.senses.hearing);
            get(se, "smell", s.senses.smell);
            get(se, "awarenessGain", s.senses.awarenessGain);
            get(se, "awarenessDecay", s.senses.awarenessDecay);
            get(se, "memorySeconds", s.senses.memorySeconds);
            get(se, "eyeHeight", s.senses.eyeHeight);
        }
        if (auto it = j.find("temperament"); it != j.end() && it->is_object()) {
            get(*it, "boldness", s.temperament.boldness);
            get(*it, "curiosity", s.temperament.curiosity);
            get(*it, "aggression", s.temperament.aggression);
            get(*it, "sociability", s.temperament.sociability);
        }
        get(j, "fears", s.fears);
        get(j, "hunts", s.hunts);
        get(j, "friends", s.friends);
        get(j, "hostileTo", s.hostileTo);
        get(j, "eats", s.eats);
        get(j, "drinks", s.drinks);
        get(j, "fearDistance", s.fearDistance);
        get(j, "investigateDistance", s.investigateDistance);
        get(j, "attackRange", s.attackRange);
        get(j, "attackCooldown", s.attackCooldown);
        get(j, "homeRadius", s.homeRadius);
        get(j, "flocks", s.flocks);
        get(j, "flockRadius", s.flockRadius);
        if (auto it = j.find("flock"); it != j.end() && it->is_object()) {
            get(*it, "separation", s.flock.separation);
            get(*it, "alignment", s.flock.alignment);
            get(*it, "cohesion", s.flock.cohesion);
            get(*it, "separationDistance", s.flock.separationDistance);
        }
        get(j, "footstepsWalk", s.footstepsWalk);
        get(j, "footstepsRun", s.footstepsRun);
        get(j, "scent", s.scent);
        if (auto it = j.find("needs"); it != j.end() && it->is_array()) {
            s.needs.clear();
            for (const nlohmann::json& n : *it) {
                NeedDef d;
                get(n, "name", d.name);
                get(n, "perSecond", d.perSecond);
                get(n, "start", d.start);
                if (d.name.empty()) {
                    if (error) *error = "a need needs a \"name\"";
                    return false;
                }
                s.needs.push_back(d);
            }
        }
        if (auto it = j.find("actions"); it != j.end() && it->is_array()) {
            s.actions.clear();
            for (const nlohmann::json& a : *it) {
                UtilityAction ua;
                get(a, "name", ua.name);
                get(a, "behavior", ua.behavior);
                get(a, "weight", ua.weight);
                get(a, "momentum", ua.momentum);
                get(a, "cooldown", ua.cooldown);
                if (ua.name.empty()) {
                    if (error) *error = "an action needs a \"name\"";
                    return false;
                }
                if (auto ct = a.find("considerations"); ct != a.end() && ct->is_array()) {
                    for (const nlohmann::json& c : *ct) {
                        Consideration con;
                        get(c, "input", con.input);
                        if (!curveFromJson(c, con.curve, error)) return false;
                        ua.considerations.push_back(con);
                    }
                }
                s.actions.push_back(std::move(ua));
            }
        }
    } catch (const nlohmann::json::exception& e) {
        if (error) *error = std::string("species \"") + s.id + "\": " + e.what();
        return false;
    }
    return true;
}

nlohmann::json speciesToJson(const Species& s) {
    nlohmann::json j;
    j["id"] = s.id;
    j["label"] = s.label;
    j["walkSpeed"] = s.walkSpeed;
    j["runSpeed"] = s.runSpeed;
    j["acceleration"] = s.acceleration;
    j["turnRate"] = s.turnRate;
    j["radius"] = s.radius;
    j["senses"] = { { "sightRange", s.senses.sightRange }, { "fov", s.senses.fovDegrees }, { "touchRange", s.senses.touchRange },
                    { "hearing", s.senses.hearing }, { "smell", s.senses.smell }, { "awarenessGain", s.senses.awarenessGain },
                    { "awarenessDecay", s.senses.awarenessDecay }, { "memorySeconds", s.senses.memorySeconds },
                    { "eyeHeight", s.senses.eyeHeight } };
    j["temperament"] = { { "boldness", s.temperament.boldness }, { "curiosity", s.temperament.curiosity },
                         { "aggression", s.temperament.aggression }, { "sociability", s.temperament.sociability } };
    j["fears"] = s.fears;
    j["hunts"] = s.hunts;
    j["friends"] = s.friends;
    j["hostileTo"] = s.hostileTo;
    j["eats"] = s.eats;
    j["drinks"] = s.drinks;
    j["fearDistance"] = s.fearDistance;
    j["investigateDistance"] = s.investigateDistance;
    j["attackRange"] = s.attackRange;
    j["attackCooldown"] = s.attackCooldown;
    j["homeRadius"] = s.homeRadius;
    j["flocks"] = s.flocks;
    j["flockRadius"] = s.flockRadius;
    j["flock"] = { { "separation", s.flock.separation }, { "alignment", s.flock.alignment }, { "cohesion", s.flock.cohesion },
                   { "separationDistance", s.flock.separationDistance } };
    j["footstepsWalk"] = s.footstepsWalk;
    j["footstepsRun"] = s.footstepsRun;
    j["scent"] = s.scent;
    j["needs"] = nlohmann::json::array();
    for (const NeedDef& n : s.needs) j["needs"].push_back({ { "name", n.name }, { "perSecond", n.perSecond }, { "start", n.start } });
    j["actions"] = nlohmann::json::array();
    for (const UtilityAction& a : s.actions) {
        nlohmann::json ja = { { "name", a.name }, { "behavior", a.behavior }, { "weight", a.weight }, { "momentum", a.momentum },
                              { "cooldown", a.cooldown } };
        ja["considerations"] = nlohmann::json::array();
        for (const Consideration& c : a.considerations) {
            nlohmann::json jc = curveToJson(c.curve);
            jc["input"] = c.input;
            ja["considerations"].push_back(jc);
        }
        j["actions"].push_back(ja);
    }
    return j;
}

} // namespace kke::ai
