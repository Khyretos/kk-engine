#include "kke/ai/AiWorld.h"

#include "kke/DataFile.h"
#include "kke/Orders.h"
#include "kke/ai/NavMesh.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace kke::ai {

namespace {

constexpr size_t kMissing = SIZE_MAX;

float flatLength(const glm::vec3& v) { return std::sqrt(v.x * v.x + v.z * v.z); }
float flatDistance(const glm::vec3& a, const glm::vec3& b) { return flatLength(b - a); }
glm::vec3 yawForward(float yawDegrees) {
    const float r = glm::radians(yawDegrees);
    return { std::sin(r), 0.0f, std::cos(r) };
}
// 1 at distance 0, 0 at `range` and beyond.
float closeness(float distance, float range) { return range > 0.0f ? std::clamp(1.0f - distance / range, 0.0f, 1.0f) : 0.0f; }

bool listHas(const std::vector<std::string>& list, const std::string& id) {
    return std::find(list.begin(), list.end(), id) != list.end();
}

// Behaviours that let the herd pull an animal along.
bool herdBehavior(const std::string& b) {
    return b == "wander" || b == "graze" || b == "idle" || b == "flee" || b == "regroup" || b == "drink" || b == "watch";
}

const std::string& behaviorOf(const UtilityAction& a) { return a.behavior.empty() ? a.name : a.behavior; }

} // namespace

AiWorld::AiWorld(uint64_t seed) : m_rng(seed ? seed : 1), m_seed(seed ? seed : 1) {
    for (Species& s : builtinSpecies()) defineSpecies(std::move(s));
}

float AiWorld::random01() {
    // splitmix64: tiny, fast and the same on every platform.
    m_rng += 0x9E3779B97F4A7C15ull;
    uint64_t z = m_rng;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z ^= z >> 31;
    return float(z >> 40) / float(1ull << 24);
}

// ---------------------------------------------------------------------------
// Species

int AiWorld::speciesIndex(const std::string& id) const {
    for (size_t i = 0; i < m_species.size(); ++i)
        if (m_species[i].id == id) return int(i);
    return -1;
}

void AiWorld::defineSpecies(Species s) {
    if (s.label.empty()) s.label = s.id;
    if (s.actions.empty()) s.actions = defaultActions(s);
    const int i = speciesIndex(s.id);
    if (i >= 0) {
        m_species[size_t(i)] = std::move(s);
        // Agents keep their index; their action list may have changed shape.
        for (Agent& a : m_agents)
            if (a.species == i) {
                a.action = -1;
                a.cooldownUntil.assign(m_species[size_t(i)].actions.size(), 0.0f);
            }
    } else {
        m_species.push_back(std::move(s));
    }
}

const Species* AiWorld::species(const std::string& id) const {
    const int i = speciesIndex(id);
    return i >= 0 ? &m_species[size_t(i)] : nullptr;
}

const Species* AiWorld::speciesOf(AgentId id) const {
    const Agent* a = agent(id);
    return a && a->species >= 0 ? &m_species[size_t(a->species)] : nullptr;
}

std::vector<std::string> AiWorld::speciesIds() const {
    std::vector<std::string> out;
    for (const Species& s : m_species) out.push_back(s.id);
    return out;
}

int AiWorld::loadSpecies(const std::filesystem::path& file, std::string* error) {
    nlohmann::json j;
    if (!datafile::loadPath(file, j, error)) return 0;
    const nlohmann::json* list = &j;
    if (j.is_object() && j.contains("species")) list = &j["species"];
    std::vector<Species> parsed;
    auto one = [&](const nlohmann::json& o) {
        // Start from the built-in of the same id, so a file can tweak one number.
        Species s;
        if (o.is_object() && o.contains("id") && o["id"].is_string())
            if (const Species* base = species(o["id"].get<std::string>())) {
                s = *base;
                s.actions.clear();
            }
        if (!speciesFromJson(o, s, error)) return false;
        parsed.push_back(std::move(s));
        return true;
    };
    if (list->is_array()) {
        for (const nlohmann::json& o : *list)
            if (!one(o)) return 0;
    } else if (!one(*list)) {
        return 0;
    }
    for (Species& s : parsed) defineSpecies(std::move(s));
    return int(parsed.size());
}

// ---------------------------------------------------------------------------
// Agents

size_t AiWorld::indexOf(AgentId id) const {
    auto it = m_index.find(id);
    return it == m_index.end() ? kMissing : it->second;
}

bool AiWorld::has(AgentId id) const { return indexOf(id) != kMissing; }

Agent* AiWorld::agent(AgentId id) {
    const size_t i = indexOf(id);
    return i == kMissing ? nullptr : &m_agents[i];
}

const Agent* AiWorld::agent(AgentId id) const {
    const size_t i = indexOf(id);
    return i == kMissing ? nullptr : &m_agents[i];
}

bool AiWorld::addAgent(AgentId id, const std::string& speciesId, const glm::vec3& position, float yawDegrees) {
    const int si = speciesIndex(speciesId);
    if (id == 0 || si < 0 || has(id)) return false;
    const Species& s = m_species[size_t(si)];
    Agent a;
    a.id = id;
    a.species = si;
    a.position = position;
    a.home = position;
    a.yaw = yawDegrees;
    a.mood = s.temperament;
    for (const NeedDef& n : s.needs) a.needs.push_back({ n.name, n.start });
    a.cooldownUntil.assign(s.actions.size(), 0.0f);
    // Spread everyone's ticks over the interval so they don't all think
    // in the same frame.
    a.perceiveTimer = random01() * perceptionInterval;
    a.thinkTimer = random01() * thinkInterval;
    a.wanderAngle = (random01() * 2.0f - 1.0f) * 3.14159f;
    a.restless = random01();
    a.restlessTimer = 4.0f + random01() * 8.0f;
    a.lastNoise = m_noiseSerial;
    m_index[id] = m_agents.size();
    m_agents.push_back(std::move(a));
    return true;
}

bool AiWorld::addActor(AgentId id, const std::string& speciesId, const glm::vec3& position) {
    if (!addAgent(id, speciesId, position)) return false;
    m_agents.back().actor = true;
    return true;
}

bool AiWorld::remove(AgentId id) {
    const size_t i = indexOf(id);
    if (i == kMissing) return false;
    m_index.erase(id);
    if (i != m_agents.size() - 1) {
        m_agents[i] = std::move(m_agents.back());
        m_index[m_agents[i].id] = i;
    }
    m_agents.pop_back();
    m_scent.forget(id);
    for (Agent& a : m_agents) {
        for (const Awareness& m : a.memory)
            if (m.id == id && m.spotted) emit({ AiEvent::Kind::Lost, a.id, id, m.lastPosition, {} });
        a.memory.erase(std::remove_if(a.memory.begin(), a.memory.end(), [id](const Awareness& m) { return m.id == id; }), a.memory.end());
        a.attitudeOverrides.erase(id);
        if (a.focus == id) a.focus = 0;
        if (a.order.target == id && a.order.kind != Order::Kind::None) a.order = {};
    }
    return true;
}

void AiWorld::setTransform(AgentId id, const glm::vec3& position, const glm::vec3& velocity, float yawDegrees) {
    if (Agent* a = agent(id)) {
        a->position = position;
        a->velocity = velocity;
        a->yaw = yawDegrees;
    }
}

void AiWorld::setEnabled(AgentId id, bool enabled) {
    if (Agent* a = agent(id)) {
        a->enabled = enabled;
        if (!enabled) {
            a->velocity = glm::vec3(0.0f);
            a->desiredVelocity = glm::vec3(0.0f);
        }
    }
}

void AiWorld::setTeam(AgentId id, uint32_t team) {
    if (Agent* a = agent(id)) a->team = team;
}

void AiWorld::setAttitude(AgentId who, AgentId towards, Attitude att) {
    if (Agent* a = agent(who)) a->attitudeOverrides[towards] = att;
}

void AiWorld::clearAttitude(AgentId who, AgentId towards) {
    if (Agent* a = agent(who)) a->attitudeOverrides.erase(towards);
}

Attitude AiWorld::attitude(AgentId who, AgentId towards) const {
    const Agent* a = agent(who);
    const Agent* b = agent(towards);
    if (!a || !b || who == towards) return Attitude::Ignore;
    if (auto it = a->attitudeOverrides.find(towards); it != a->attitudeOverrides.end()) return it->second;
    if (a->team && b->team) return a->team == b->team ? Attitude::Friendly : Attitude::Hostile;
    if (a->species == b->species) return Attitude::Friendly;
    const Species& s = m_species[size_t(a->species)];
    const std::string& other = m_species[size_t(b->species)].id;
    auto match = [&](const std::vector<std::string>& list) { return listHas(list, other); };
    auto any = [&](const std::vector<std::string>& list) { return listHas(list, "*"); };
    if (match(s.friends)) return Attitude::Friendly;
    if (match(s.hunts)) return Attitude::Hunt;
    if (match(s.hostileTo)) return Attitude::Hostile;
    if (match(s.fears)) return a->mood.aggression > 0.6f ? Attitude::Hostile : Attitude::Fear;
    if (any(s.hunts)) return Attitude::Hunt;
    if (any(s.hostileTo)) return Attitude::Hostile;
    if (any(s.fears)) return a->mood.aggression > 0.6f ? Attitude::Hostile : Attitude::Fear;
    return a->mood.curiosity > 0.25f ? Attitude::Curious : Attitude::Ignore;
}

void AiWorld::order(AgentId id, const Order& o) {
    Agent* a = agent(id);
    if (!a || a->actor) return;
    a->order = o;
    a->path.clear();
    a->thinkTimer = 0.0f; // decide now, not at the next tick
}

void AiWorld::clearOrder(AgentId id) {
    if (Agent* a = agent(id)) {
        a->order = {};
        a->thinkTimer = 0.0f;
    }
}

float AiWorld::need(AgentId id, const std::string& name) const {
    if (const Agent* a = agent(id))
        for (const auto& [n, v] : a->needs)
            if (n == name) return v;
    return -1.0f;
}

bool AiWorld::setNeed(AgentId id, const std::string& name, float value) {
    Agent* a = agent(id);
    if (!a) return false;
    value = std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0.0f;
    for (auto& [n, v] : a->needs)
        if (n == name) {
            v = value;
            return true;
        }
    a->needs.push_back({ name, value });
    return true;
}

bool AiWorld::setMood(AgentId id, const Temperament& t) {
    Agent* a = agent(id);
    if (!a) return false;
    auto c = [](float v) { return std::isfinite(v) ? std::clamp(v, 0.0f, 1.0f) : 0.0f; };
    a->mood = { c(t.boldness), c(t.curiosity), c(t.aggression), c(t.sociability) };
    return true;
}

bool AiWorld::setInput(AgentId id, const std::string& name, float value) {
    Agent* a = agent(id);
    if (!a) return false;
    value = std::isfinite(value) ? value : 0.0f;
    for (auto& [n, v] : a->inputs)
        if (n == name) {
            v = value;
            return true;
        }
    a->inputs.push_back({ name, value });
    return true;
}

// ---------------------------------------------------------------------------
// World

uint32_t AiWorld::addPlace(const std::string& kind, const glm::vec3& position, float radius) {
    const uint32_t id = m_nextPlace++;
    m_places.push_back({ id, kind, position, radius });
    return id;
}

bool AiWorld::removePlace(uint32_t id) {
    auto it = std::find_if(m_places.begin(), m_places.end(), [id](const Place& p) { return p.id == id; });
    if (it == m_places.end()) return false;
    m_places.erase(it);
    return true;
}

void AiWorld::makeNoise(const Noise& n) {
    if (!std::isfinite(n.position.x) || !std::isfinite(n.position.y) || !std::isfinite(n.position.z) || !(n.loudness > 0.0f)) return;
    m_noises.push_back({ n, m_time, ++m_noiseSerial });
}

std::vector<AiEvent> AiWorld::takeEvents() {
    std::vector<AiEvent> out;
    out.swap(m_events);
    return out;
}

// ---------------------------------------------------------------------------
// Update

void AiWorld::rebuildGrid() {
    m_grid.clear();
    for (const Agent& a : m_agents) m_grid.insert(a.id, a.position);
}

void AiWorld::update(float dt) {
    if (!(dt > 0.0f)) return;
    dt = std::min(dt, 0.25f); // a hitch doesn't teleport anyone
    m_time += dt;
    rebuildGrid();
    m_scent.update(dt);
    for (const Agent& a : m_agents) {
        const Species& s = m_species[size_t(a.species)];
        if (s.scent > 0.0f && a.enabled) m_scent.emit(a.id, a.position, s.scent);
    }
    for (Agent& a : m_agents) {
        if (a.actor || !a.enabled) continue;
        const Species& s = m_species[size_t(a.species)];
        for (auto& [name, value] : a.needs)
            for (const NeedDef& d : s.needs)
                if (d.name == name) value = std::min(1.0f, value + d.perSecond * dt);
        a.perceiveTimer -= dt;
        if (a.perceiveTimer <= 0.0f) {
            perceive(a, perceptionInterval - a.perceiveTimer);
            a.perceiveTimer += perceptionInterval;
            if (a.perceiveTimer <= 0.0f) a.perceiveTimer = perceptionInterval;
        }
        a.restlessTimer -= dt;
        if (a.restlessTimer <= 0.0f) {
            a.restless = random01();
            a.restlessTimer = 5.0f + random01() * 10.0f;
        }
        a.thinkTimer -= dt;
        if (a.thinkTimer <= 0.0f) {
            think(a);
            a.thinkTimer += thinkInterval;
            if (a.thinkTimer <= 0.0f) a.thinkTimer = thinkInterval;
        }
        act(a, dt);
    }
    // Every agent has had a perception tick since these were made.
    const double keep = double(perceptionInterval) * 1.5;
    m_noises.erase(std::remove_if(m_noises.begin(), m_noises.end(), [&](const TimedNoise& n) { return m_time - n.time > keep; }),
                   m_noises.end());
}

void AiWorld::perceive(Agent& a, float dt) {
    const Species& s = m_species[size_t(a.species)];
    const Senses& se = s.senses;
    const glm::vec3 eye = a.position + glm::vec3(0.0f, se.eyeHeight, 0.0f);
    const glm::vec3 fwd = yawForward(a.yaw);
    struct Stim {
        AgentId id;
        float strength;
        glm::vec3 position, velocity;
        uint8_t by;
    };
    std::vector<Stim> stims;
    auto addStim = [&](AgentId id, float strength, const glm::vec3& pos, const glm::vec3& vel, uint8_t by) {
        for (Stim& st : stims)
            if (st.id == id) {
                if (strength > st.strength) {
                    st.strength = strength;
                    st.position = pos;
                    st.velocity = vel;
                }
                st.by = uint8_t(st.by | by);
                return;
            }
        stims.push_back({ id, strength, pos, vel, by });
    };

    // Everyone near enough to see, hear walking or bump into.
    const float radius = std::max({ se.sightRange, se.touchRange, 6.0f * se.hearing, 1.0f });
    m_query.clear();
    m_grid.query(a.position, radius, [this](uint32_t id) { return m_agents[m_index.at(id)].position; }, m_query);
    for (uint32_t id : m_query) {
        if (id == a.id) continue;
        const Agent& o = m_agents[m_index.at(id)];
        const Species& os = m_species[size_t(o.species)];
        const float speed = flatLength(o.velocity);
        Conspicuity c = o.conspicuity;
        if (o.autoConspicuity) {
            // Standing still is harder to notice; running easier.
            c.visibility = speed < 0.2f ? 0.75f : speed > os.walkSpeed * 1.3f ? 1.25f : 1.0f;
            c.size = std::clamp(std::sqrt(os.radius / 0.4f), 0.7f, 1.4f);
        }
        const glm::vec3 target = o.position + glm::vec3(0.0f, std::max(os.radius, 0.3f), 0.0f);
        float sight = sightStrength(eye, fwd, se, target, c);
        if (sight > 0.0f && lineOfSight && !lineOfSight(eye, target)) sight = 0.0f;
        const float touch = touchStrength(a.position, se, o.position);
        const float steps = speed < 0.3f ? 0.0f : speed > os.walkSpeed * 1.3f ? os.footstepsRun : os.footstepsWalk;
        const float hear = steps > 0.0f ? hearingStrength(a.position, se, Noise{ o.position, steps, o.id, 0 }) * 0.8f : 0.0f;
        uint8_t by = SenseNone;
        if (sight > 0.0f) by |= SenseSight;
        if (touch > 0.0f) by |= SenseTouch;
        if (hear > 0.0f) by |= SenseHearing;
        const float strength = std::max({ sight, touch, hear });
        if (strength > 0.0f) addStim(o.id, strength, o.position, o.velocity, by);
    }

    // Noises made since this agent last listened.
    for (const TimedNoise& tn : m_noises) {
        if (tn.serial <= a.lastNoise || tn.noise.source == a.id) continue;
        const float h = hearingStrength(a.position, se, tn.noise);
        if (h <= 0.0f) continue;
        emit({ AiEvent::Kind::Heard, a.id, tn.noise.source, tn.noise.position, {} });
        if (tn.noise.source && has(tn.noise.source)) addStim(tn.noise.source, h, tn.noise.position, glm::vec3(0.0f), SenseHearing);
    }
    if (!m_noises.empty()) a.lastNoise = m_noises.back().serial;

    // Scent trails: known roughly where, weaker than seeing.
    if (se.smell > 0.0f && m_scent.size() > 0) {
        std::vector<ScentField::Smelled> smelled;
        m_scent.smell(a.position, se, smelled);
        for (const ScentField::Smelled& sm : smelled)
            if (sm.source != a.id && has(sm.source)) addStim(sm.source, sm.strength * 0.7f, sm.position, glm::vec3(0.0f), SenseSmell);
    }

    // Feed awareness: stimulated ones rise, the rest fade.
    for (const Stim& st : stims) {
        auto it = std::find_if(a.memory.begin(), a.memory.end(), [&](const Awareness& m) { return m.id == st.id; });
        if (it == a.memory.end()) {
            Awareness m;
            m.id = st.id;
            a.memory.push_back(m);
            it = a.memory.end() - 1;
        }
        it->lastPosition = st.position;
        it->lastVelocity = st.velocity;
    }
    for (Awareness& m : a.memory) {
        float stimulus = 0.0f;
        m.sensedBy = SenseNone;
        for (const Stim& st : stims)
            if (st.id == m.id) {
                stimulus = st.strength;
                m.sensedBy = st.by;
            }
        const int change = updateAwareness(m, stimulus, dt, float(m_time), se);
        if (change > 0) emit({ AiEvent::Kind::Spotted, a.id, m.id, m.lastPosition, {} });
        else if (change < 0) emit({ AiEvent::Kind::Lost, a.id, m.id, m.lastPosition, {} });
    }
    a.memory.erase(std::remove_if(a.memory.begin(), a.memory.end(),
                                  [&](const Awareness& m) { return m.level <= 0.0f && float(m_time) - m.lastSensedAt > se.memorySeconds; }),
                   a.memory.end());
}

const Awareness* AiWorld::strongest(const Agent& a, Attitude kind, float* scoreOut) const {
    const Species& s = m_species[size_t(a.species)];
    const Awareness* best = nullptr;
    float bestScore = 0.0f;
    for (const Awareness& m : a.memory) {
        if (m.level <= 0.0f || attitude(a.id, m.id) != kind) continue;
        const float d = flatDistance(a.position, m.lastPosition);
        float range = s.senses.sightRange;
        if (kind == Attitude::Fear || kind == Attitude::Hostile) range = std::max(s.fearDistance * 1.5f, s.senses.sightRange * 0.7f);
        const float sc = m.level * closeness(d, range);
        if (sc > bestScore) {
            bestScore = sc;
            best = &m;
        }
    }
    if (scoreOut) *scoreOut = bestScore;
    return best;
}

const Place* AiWorld::nearestPlace(const Agent& a, const std::vector<std::string>& kinds, float maxDistance) const {
    const Place* best = nullptr;
    float bestD = maxDistance;
    for (const Place& p : m_places) {
        if (!listHas(kinds, p.kind)) continue;
        const float d = flatDistance(a.position, p.position);
        if (d < bestD) {
            bestD = d;
            best = &p;
        }
    }
    return best;
}

// ---------------------------------------------------------------- teaching

LearnedPolicy& AiWorld::policyFor(int species) {
    const Species& s = m_species[size_t(species)];
    std::vector<std::string> actions;
    for (const UtilityAction& a : s.actions) actions.push_back(a.name);
    LearnedPolicy& p = m_policies[species];
    if (p.actions() != actions) {
        // What the species' own considerations read (orders aside: they
        // weigh in on their own), in a fixed order.
        std::vector<std::string> features;
        for (const UtilityAction& a : s.actions)
            for (const Consideration& c : a.considerations)
                if (c.input.rfind("order_", 0) != 0) features.push_back(c.input);
        std::sort(features.begin(), features.end());
        features.erase(std::unique(features.begin(), features.end()), features.end());
        p.setup(std::move(features), std::move(actions));
    }
    return p;
}

std::vector<float> AiWorld::learnedFeatures(const Agent& a, const LearnedPolicy& p) const {
    std::vector<float> f;
    f.reserve(p.features().size());
    for (const std::string& n : p.features()) f.push_back(computeInput(a, n));
    return f;
}

bool AiWorld::teach(AgentId id, const std::string& action) {
    const Agent* a = agent(id);
    if (!a || a->actor) return false;
    LearnedPolicy& p = policyFor(a->species);
    const auto it = std::find(p.actions().begin(), p.actions().end(), action);
    if (it == p.actions().end()) return false;
    return p.addExample(learnedFeatures(*a, p), int(it - p.actions().begin()));
}

LearnedPolicy::Result AiWorld::learn(const std::string& species, int epochs) {
    const int s = speciesIndex(species);
    if (s < 0) return {};
    return policyFor(s).train(epochs, 0.5f, m_seed ^ uint64_t(s + 1)); // the same lesson learns the same way
}

void AiWorld::unlearn(const std::string& species, bool examples) {
    const int s = speciesIndex(species);
    auto it = s < 0 ? m_policies.end() : m_policies.find(s);
    if (it == m_policies.end()) return;
    it->second.forget();
    if (examples) it->second.clearExamples();
}

const LearnedPolicy* AiWorld::policy(const std::string& species) const {
    const int s = speciesIndex(species);
    auto it = s < 0 ? m_policies.end() : m_policies.find(s);
    return it == m_policies.end() ? nullptr : &it->second;
}

bool AiWorld::setPolicy(const std::string& species, LearnedPolicy policy) {
    const int s = speciesIndex(species);
    if (s < 0) return false;
    const LearnedPolicy& fitted = policyFor(s);
    if (policy.actions() != fitted.actions() || policy.features() != fitted.features()) return false;
    m_policies[s] = std::move(policy);
    return true;
}

float AiWorld::input(AgentId id, const std::string& name) const {
    const Agent* a = agent(id);
    return a ? computeInput(*a, name) : 0.0f;
}

float AiWorld::computeInput(const Agent& a, const std::string& name) const {
    for (const auto& [n, v] : a.inputs)
        if (n == name) return v;
    for (const auto& [n, v] : a.needs)
        if (n == name) return v;
    const Species& s = m_species[size_t(a.species)];
    const Order::Kind ok = a.order.kind;
    if (name.rfind("order_", 0) == 0) {
        Order::Kind k;
        return orderKindFromName(name.substr(6), k) && k == ok && ok != Order::Kind::None ? 1.0f : 0.0f;
    }
    if (name == "fear") {
        float sc = 0.0f;
        strongest(a, Attitude::Fear, &sc);
        // Bold animals need a closer, clearer threat to run.
        return std::clamp(sc * (1.0f - a.mood.boldness * 0.8f) * 1.4f, 0.0f, 1.0f);
    }
    if (name == "hostile") {
        float sc = 0.0f;
        strongest(a, Attitude::Hostile, &sc);
        return std::clamp(sc * (0.4f + a.mood.aggression), 0.0f, 1.0f);
    }
    if (name == "threat") {
        float f = 0.0f, h = 0.0f;
        strongest(a, Attitude::Fear, &f);
        strongest(a, Attitude::Hostile, &h);
        return std::max(f, h);
    }
    if (name == "prey") {
        float sc = 0.0f;
        strongest(a, Attitude::Hunt, &sc);
        return sc;
    }
    if (name == "curious") {
        float sc = 0.0f;
        const Awareness* m = strongest(a, Attitude::Curious, &sc);
        if (!m) return 0.0f;
        // The novelty wears off after a good look.
        float novelty = 1.0f;
        if (a.action >= 0 && behaviorOf(s.actions[size_t(a.action)]) == "investigate")
            novelty = std::clamp(1.0f - (a.actionTime - 8.0f) / 8.0f, 0.0f, 1.0f);
        return std::clamp(m->level * a.mood.curiosity * 1.6f * novelty, 0.0f, 1.0f);
    }
    if (name == "alert") {
        // Something is there but it's not clear what: stop and look.
        float best = 0.0f;
        for (const Awareness& m : a.memory) {
            if (m.spotted || m.level <= 0.05f) continue;
            const Attitude att = attitude(a.id, m.id);
            if (att == Attitude::Friendly || att == Attitude::Ignore) continue;
            best = std::max(best, m.level / 0.6f);
        }
        return std::clamp(best, 0.0f, 1.0f);
    }
    if (name == "food") {
        if (!s.eats.empty() && listHas(s.eats, "grass")) {
            bool anyGrassPlace = false;
            for (const Place& p : m_places) anyGrassPlace = anyGrassPlace || p.kind == "grass";
            if (!anyGrassPlace) return 1.0f; // no fields marked: the ground is grass
        }
        return nearestPlace(a, s.eats, s.homeRadius * 2.0f) ? 1.0f : 0.0f;
    }
    if (name == "water") return nearestPlace(a, s.drinks, s.homeRadius * 2.0f) ? 1.0f : 0.0f;
    if (name == "alone") {
        // How far it has drifted from its herd: 0 in the middle of it, 1 a
        // flock radius or more beyond the edge (or with nobody near).
        if (!s.flocks) return 0.0f;
        glm::vec3 centre(0.0f);
        int mates = 0;
        bool anyOther = false;
        for (const Agent& o : m_agents) {
            if (o.id == a.id || o.species != a.species) continue;
            anyOther = true;
            if (flatDistance(a.position, o.position) > s.flockRadius * 4.0f) continue;
            centre += o.position;
            ++mates;
        }
        if (!anyOther) return 0.0f; // the only one
        if (mates == 0) return a.mood.sociability;
        centre /= float(mates);
        const float d = flatDistance(a.position, centre);
        return std::clamp((d - s.flockRadius * 0.5f) / s.flockRadius, 0.0f, 1.0f) * a.mood.sociability;
    }
    if (name == "restless") return a.restless;
    if (name == "leader_far") {
        if (ok != Order::Kind::Follow) return 0.0f;
        const Agent* l = agent(a.order.target);
        return l ? std::clamp(flatDistance(a.position, l->position) / std::max(a.order.distance * 2.0f, 0.1f), 0.0f, 1.0f) : 0.0f;
    }
    if (name == "boldness") return a.mood.boldness;
    if (name == "curiosity") return a.mood.curiosity;
    if (name == "aggression") return a.mood.aggression;
    if (name == "sociability") return a.mood.sociability;
    return 0.0f;
}

void AiWorld::think(Agent& a) {
    const Species& s = m_species[size_t(a.species)];
    if (a.cooldownUntil.size() != s.actions.size()) a.cooldownUntil.assign(s.actions.size(), 0.0f);
    const InputFn inputs = [this, &a](const std::string& n) { return computeInput(a, n); };
    // What it was taught, as a lean on top of its instincts.
    std::vector<float> lean;
    if (auto it = m_policies.find(a.species); it != m_policies.end() && it->second.trained() && learnedWeight > 0.0f) {
        lean = it->second.predict(learnedFeatures(a, it->second));
        for (float& v : lean) v *= learnedWeight;
    }
    const Choice c = chooseAction(s.actions, inputs, a.action, [&](int i) { return float(m_time) < a.cooldownUntil[size_t(i)]; }, lean);
    const int previous = a.action;
    const std::string prevBehavior = previous >= 0 ? behaviorOf(s.actions[size_t(previous)]) : std::string("idle");
    a.actionScore = c.score;
    if (c.index != previous) {
        if (previous >= 0) a.cooldownUntil[size_t(previous)] = float(m_time) + s.actions[size_t(previous)].cooldown;
        a.action = c.index;
        a.actionTime = 0.0f;
        a.path.clear();
    }
    const std::string behavior = a.action >= 0 ? behaviorOf(s.actions[size_t(a.action)]) : std::string("idle");

    // What it's attending to.
    const AgentId oldFocus = a.focus;
    a.focus = 0;
    auto pick = [&](Attitude kind) {
        if (const Awareness* m = strongest(a, kind)) a.focus = m->id;
    };
    if (behavior == "flee") {
        if (a.order.kind == Order::Kind::Flee) a.focus = a.order.target;
        else pick(Attitude::Fear);
    } else if (behavior == "fight" || behavior == "hold") {
        pick(Attitude::Hostile);
        if (!a.focus && behavior == "fight") pick(Attitude::Fear);
    } else if (behavior == "hunt") {
        pick(Attitude::Hunt);
    } else if (behavior == "investigate") {
        pick(Attitude::Curious);
    } else if (behavior == "watch") {
        float best = 0.0f;
        for (const Awareness& m : a.memory) {
            const Attitude att = attitude(a.id, m.id);
            if (att == Attitude::Friendly || att == Attitude::Ignore) continue;
            if (m.level > best) {
                best = m.level;
                a.focus = m.id;
            }
        }
    } else if (behavior == "follow" || behavior == "attack" || behavior == "interact") {
        a.focus = a.order.target;
    }

    if (c.index != previous) {
        emit({ AiEvent::Kind::ActionChanged, a.id, a.focus, a.position, a.action >= 0 ? s.actions[size_t(a.action)].name : "" });
        if (behavior == "flee" && prevBehavior != "flee") {
            a.fleeTimer = 0.0f;
            emit({ AiEvent::Kind::Scared, a.id, a.focus, a.position, {} });
        } else if (behavior != "flee" && prevBehavior == "flee") {
            emit({ AiEvent::Kind::Calmed, a.id, oldFocus, a.position, {} });
        }
    }
}

void AiWorld::neighbours(const Agent& a, float radius, bool sameSpeciesOnly, std::vector<Neighbour>& out) const {
    m_query.clear();
    m_grid.query(a.position, radius, [this](uint32_t id) { return m_agents[m_index.at(id)].position; }, m_query);
    for (uint32_t id : m_query) {
        if (id == a.id) continue;
        const Agent& o = m_agents[m_index.at(id)];
        // Disabled = carried, ragdolled, asleep: nothing to keep clear of
        // (a dog bringing a ball back doesn't back away from it).
        if (!o.enabled) continue;
        if (sameSpeciesOnly && o.species != a.species) continue;
        out.push_back({ o.position, o.velocity, m_species[size_t(o.species)].radius });
    }
}

glm::vec3 AiWorld::followPath(Agent& a, const glm::vec3& goal, float arriveRadius, float speed, float dt) {
    const Species& s = m_species[size_t(a.species)];
    Mover m{ a.position, a.velocity, speed, s.acceleration, s.radius };
    if (!m_nav || !m_nav->valid()) return arrive(m, goal, std::max(arriveRadius, 1.5f));
    a.repathTimer -= dt;
    if (a.path.empty() || flatDistance(goal, a.pathGoal) > 1.0f || a.repathTimer <= 0.0f) {
        NavMesh::Path p;
        a.path.clear();
        a.pathIndex = 0;
        if (m_nav->findPath(a.position, goal, p)) a.path = std::move(p.points);
        a.pathGoal = goal;
        a.repathTimer = 1.0f + random01() * 0.5f;
        if (a.path.empty()) return arrive(m, goal, 1.5f);
    }
    while (a.pathIndex + 1 < a.path.size() && flatDistance(a.position, a.path[a.pathIndex]) < std::max(0.5f, s.radius)) ++a.pathIndex;
    const glm::vec3 next = a.path[std::min(a.pathIndex, a.path.size() - 1)];
    if (a.pathIndex + 1 >= a.path.size()) return arrive(m, next, std::max(arriveRadius, 1.5f));
    return seek(m, next);
}

bool AiWorld::pickFleeGoal(Agent& a, const glm::vec3& threat, glm::vec3& out) {
    const Species& s = m_species[size_t(a.species)];
    glm::vec3 away = a.position - threat;
    away.y = 0.0f;
    if (flatLength(away) < 1e-3f) away = yawForward(a.yaw);
    away /= flatLength(away);
    const float dist = s.fearDistance + 4.0f;
    // Straight away first, then fanning out: a fence behind a sheep sends
    // it running along the fence, not into it.
    static const float angles[] = { 0.0f, 30.0f, -30.0f, 60.0f, -60.0f, 90.0f, -90.0f, 120.0f, -120.0f };
    float bestScore = -1.0f;
    for (float deg : angles) {
        const float r = glm::radians(deg);
        const glm::vec3 dir{ away.x * std::cos(r) - away.z * std::sin(r), 0.0f, away.x * std::sin(r) + away.z * std::cos(r) };
        const glm::vec3 candidate = a.position + dir * dist;
        glm::vec3 onMesh;
        if (!m_nav->nearestPoint(candidate, onMesh, { 3.0f, 6.0f, 3.0f })) continue;
        float frac = 1.0f;
        m_nav->walkable(a.position, onMesh, &frac);
        // Prefer far from the threat and a clear run; mildly prefer home.
        const glm::vec3 end = a.position + (onMesh - a.position) * frac;
        const float score = flatDistance(end, threat) + frac * 4.0f - flatDistance(end, a.home) * 0.05f;
        if (score > bestScore) {
            bestScore = score;
            out = frac > 0.9f ? onMesh : end;
        }
        if (deg == 0.0f && frac > 0.95f) break;
    }
    return bestScore >= 0.0f;
}

void AiWorld::act(Agent& a, float dt) {
    const Species& s = m_species[size_t(a.species)];
    a.actionTime += dt;
    a.attackTimer = std::max(0.0f, a.attackTimer - dt);
    const std::string behavior = a.action >= 0 ? behaviorOf(s.actions[size_t(a.action)]) : std::string("idle");
    Mover m{ a.position, a.velocity, s.walkSpeed, s.acceleration, s.radius };
    glm::vec3 steer(0.0f);
    float speed = s.walkSpeed;
    a.hasLookAt = false;
    std::string anim = "walk";

    auto focusPos = [&](AgentId id, glm::vec3& out) {
        if (const Agent* o = agent(id)) {
            // Orders and what it's sensing right now: where it really is.
            // Otherwise the last place it was sensed.
            for (const Awareness& mem : a.memory)
                if (mem.id == id) {
                    out = mem.sensedBy != SenseNone ? o->position : mem.lastPosition;
                    return true;
                }
            out = o->position;
            return true;
        }
        return false;
    };
    auto brake = [&]() { return arrive(m, a.position, 1.0f); };
    auto tryAttack = [&](AgentId target, const glm::vec3& targetPos) {
        const Species* ts = speciesOf(target);
        const float reach = s.attackRange + s.radius + (ts ? ts->radius : 0.3f);
        if (flatDistance(a.position, targetPos) <= reach) {
            if (a.attackTimer <= 0.0f) {
                emit({ AiEvent::Kind::Attack, a.id, target, targetPos, {} });
                a.attackTimer = s.attackCooldown;
            }
            return true;
        }
        return false;
    };
    const bool attacking = a.attackTimer > s.attackCooldown - 0.4f;

    if (behavior == "idle") {
        steer = brake();
        anim = "idle";
    } else if (behavior == "wander") {
        if (flatDistance(a.position, a.home) > s.homeRadius) steer = seek(m, a.home);
        else steer = wander(m, a.wanderAngle, dt, random01());
        speed = s.walkSpeed * 0.8f;
    } else if (behavior == "graze" || behavior == "drink") {
        const bool eat = behavior == "graze";
        const Place* p = nearestPlace(a, eat ? s.eats : s.drinks, s.homeRadius * 2.0f);
        const char* needName = eat ? "hunger" : "thirst";
        if (p && flatDistance(a.position, p->position) > p->radius) {
            steer = followPath(a, p->position, p->radius * 0.5f, s.walkSpeed, dt);
        } else {
            steer = brake();
            anim = eat ? "eat" : "drink";
            for (auto& [n, v] : a.needs)
                if (n == needName) v = std::max(0.0f, v - dt / 15.0f);
            if (p) {
                a.lookAt = p->position;
                a.hasLookAt = true;
            }
        }
    } else if (behavior == "rest") {
        steer = brake();
        anim = "rest";
        for (auto& [n, v] : a.needs)
            if (n == "tiredness") v = std::max(0.0f, v - dt / 25.0f);
    } else if (behavior == "flee") {
        glm::vec3 threat;
        bool haveThreat = false;
        if (a.order.kind == Order::Kind::Flee && !a.order.target) {
            threat = a.order.position;
            haveThreat = true;
        } else {
            haveThreat = focusPos(a.focus, threat);
        }
        speed = s.runSpeed;
        m.maxSpeed = speed;
        anim = "run";
        if (haveThreat) {
            if (m_nav && m_nav->valid()) {
                a.fleeTimer -= dt;
                if (a.fleeTimer <= 0.0f || flatDistance(a.position, a.fleeGoal) < 1.5f) {
                    if (!pickFleeGoal(a, threat, a.fleeGoal)) a.fleeGoal = a.position;
                    a.fleeTimer = 0.8f;
                }
                steer = followPath(a, a.fleeGoal, 0.5f, speed, dt) + flee(m, threat, s.fearDistance * 0.4f) * 0.5f;
            } else {
                const Agent* t = agent(a.focus);
                steer = evade(m, threat, t ? t->velocity : glm::vec3(0.0f));
                // Don't run off the map: past the home range, curve back round.
                if (flatDistance(a.position, a.home) > s.homeRadius * 1.5f) steer += seek(m, a.home) * 0.6f;
            }
            // A flee order ends once far enough away.
            if (a.order.kind == Order::Kind::Flee && flatDistance(a.position, threat) > s.fearDistance) a.order = {};
        } else {
            steer = brake();
            anim = "alert";
            if (a.order.kind == Order::Kind::Flee) a.order = {};
        }
    } else if (behavior == "fight" || behavior == "hunt" || behavior == "attack") {
        glm::vec3 target;
        const AgentId id = behavior == "attack" ? a.order.target : a.focus;
        if (focusPos(id, target)) {
            speed = s.runSpeed;
            m.maxSpeed = speed;
            anim = "run";
            const Agent* t = agent(id);
            if (tryAttack(id, target)) {
                steer = brake();
                anim = "attack";
            } else if (behavior == "fight" && a.order.kind == Order::Kind::None && flatDistance(a.position, a.home) > s.homeRadius + s.fearDistance) {
                // Chased it out of its patch: good enough, go back.
                steer = followPath(a, a.home, 2.0f, s.walkSpeed, dt);
                anim = "walk";
            } else if (m_nav && m_nav->valid() && flatDistance(a.position, target) > 4.0f) {
                steer = followPath(a, target, 0.5f, speed, dt);
            } else {
                steer = pursue(m, target, t ? t->velocity : glm::vec3(0.0f));
            }
            a.lookAt = target;
            a.hasLookAt = true;
        } else {
            if (behavior == "attack") a.order = {};
            steer = brake();
            anim = "idle";
        }
        if (attacking) anim = "attack";
    } else if (behavior == "investigate") {
        glm::vec3 target;
        if (focusPos(a.focus, target)) {
            a.lookAt = target;
            a.hasLookAt = true;
            const float d = flatDistance(a.position, target);
            if (d > s.investigateDistance + 0.3f) {
                const glm::vec3 stopAt = target + (a.position - target) * (s.investigateDistance / std::max(d, 0.01f));
                steer = followPath(a, stopAt, 0.5f, s.walkSpeed * 0.7f, dt);
                speed = s.walkSpeed * 0.7f;
            } else {
                steer = brake();
                anim = "sniff";
            }
        } else {
            steer = brake();
            anim = "idle";
        }
    } else if (behavior == "watch") {
        glm::vec3 target;
        steer = brake();
        anim = "alert";
        if (focusPos(a.focus, target)) {
            a.lookAt = target;
            a.hasLookAt = true;
        }
    } else if (behavior == "regroup") {
        std::vector<Neighbour> mates;
        neighbours(a, s.flockRadius * 4.0f, true, mates);
        if (!mates.empty()) {
            glm::vec3 centre(0.0f);
            for (const Neighbour& n : mates) centre += n.position;
            centre /= float(mates.size());
            steer = followPath(a, centre, s.flockRadius * 0.3f, s.walkSpeed * 1.3f, dt);
            speed = s.walkSpeed * 1.3f;
        } else {
            steer = brake();
            anim = "idle";
        }
    } else if (behavior == "goto") {
        const float radius = std::min(a.order.distance, 1.0f);
        speed = a.order.run ? s.runSpeed : s.walkSpeed;
        if (a.order.run) anim = "run";
        if (flatDistance(a.position, a.order.position) <= radius) {
            emit({ AiEvent::Kind::Arrived, a.id, 0, a.order.position, {} });
            const glm::vec3 at = a.order.position;
            a.order = {};
            a.order.kind = Order::Kind::Hold;
            a.order.position = at;
            steer = brake();
            anim = "idle";
        } else {
            steer = followPath(a, a.order.position, radius, speed, dt);
        }
    } else if (behavior == "follow") {
        glm::vec3 leader;
        if (focusPos(a.order.target, leader)) {
            // Beside and a little behind, on the side it is already on,
            // and out of the leader's way (kke::followSlot): a companion
            // never walks in front of whoever it follows.
            const Agent* l = agent(a.order.target);
            const glm::vec3 leaderVel = l ? l->velocity : glm::vec3(0.0f);
            FollowSettings fs;
            fs.distance = a.order.distance;
            const glm::vec3 slot = followSlot(leader, leaderVel, l ? l->yaw : 0.0f, a.position, fs);
            const float d = flatDistance(a.position, leader);
            const float toSlot = flatDistance(a.position, slot);
            if (toSlot > 0.6f || inLeadersWay(leader, leaderVel, a.position, fs)) {
                speed = d > a.order.distance * 3.0f || a.order.run || glm::length(leaderVel) > s.walkSpeed * 1.3f ? s.runSpeed : s.walkSpeed * 1.2f;
                if (speed >= s.runSpeed) anim = "run";
                steer = followPath(a, slot, 0.3f, speed, dt);
            } else {
                steer = brake();
                anim = "idle";
                a.lookAt = leader;
                a.hasLookAt = d > 0.5f;
            }
        } else {
            a.order = {};
            steer = brake();
            anim = "idle";
        }
    } else if (behavior == "hold") {
        const float d = flatDistance(a.position, a.order.position);
        if (d > 0.6f) {
            speed = a.order.run ? s.runSpeed : s.walkSpeed;
            steer = followPath(a, a.order.position, 0.3f, speed, dt);
        } else {
            steer = brake();
            anim = "idle";
        }
        glm::vec3 target;
        if (a.focus && focusPos(a.focus, target)) {
            a.lookAt = target;
            a.hasLookAt = true;
            if (tryAttack(a.focus, target)) anim = "attack";
            else if (d <= 0.6f) anim = "alert";
        }
        if (attacking) anim = "attack";
    } else if (behavior == "interact") {
        glm::vec3 target;
        if (focusPos(a.order.target, target)) {
            speed = a.order.run ? s.runSpeed : s.walkSpeed;
            if (a.order.run) anim = "run";
            const Species* ts = speciesOf(a.order.target);
            const float reach = std::max(a.order.distance * 0.4f, s.radius + (ts ? ts->radius : 0.3f) + 0.3f);
            if (flatDistance(a.position, target) <= reach) {
                emit({ AiEvent::Kind::Arrived, a.id, a.order.target, target, {} });
                a.order = {};
                steer = brake();
                anim = "idle";
            } else {
                steer = followPath(a, target, 0.3f, speed, dt);
            }
        } else {
            a.order = {};
            steer = brake();
            anim = "idle";
        }
    } else {
        steer = brake();
        anim = "idle";
    }

    move(a, steer, speed, dt);
    const float moving = flatLength(a.velocity);
    if (anim == "walk" || anim == "run") {
        if (moving < 0.15f) anim = "idle";
        else anim = moving > s.walkSpeed * 1.4f ? "run" : "walk";
    }
    a.anim = anim;
}

void AiWorld::move(Agent& a, const glm::vec3& steering, float speedLimit, float dt) {
    const Species& s = m_species[size_t(a.species)];
    const std::string behavior = a.action >= 0 ? behaviorOf(s.actions[size_t(a.action)]) : std::string("idle");
    Mover m{ a.position, a.velocity, speedLimit, s.acceleration, s.radius };

    glm::vec3 total = steering;
    // Sent to a spot, it turns onto it as hard as its legs allow: steering
    // is the change of velocity wanted, which acceleration alone would
    // make in a second, and at a run that second drifts a couple of
    // metres (soldiers still running from their last order crossed the
    // barrier they were sent behind). So it is made in speed/acceleration
    // seconds instead, as quick as the species can.
    if ((a.order.kind == Order::Kind::MoveTo || a.order.kind == Order::Kind::Hold) && speedLimit > 0.0f)
        total *= std::max(1.0f, s.acceleration / speedLimit);
    // Never walk through each other; herds also pull together.
    std::vector<Neighbour> near;
    neighbours(a, std::max(s.flockRadius, 3.0f), false, near);
    if (!near.empty()) {
        std::vector<Neighbour> close;
        for (const Neighbour& n : near)
            if (flatDistance(a.position, n.position) < s.radius + n.radius + 1.0f) close.push_back(n);
        total += separation(m, close, 0.3f) * 1.5f;
        if (s.flocks && herdBehavior(behavior)) {
            std::vector<Neighbour> mates;
            neighbours(a, s.flockRadius, true, mates);
            if (!mates.empty()) {
                FlockWeights w = s.flock;
                const float social = a.mood.sociability;
                w.cohesion *= social;
                w.alignment *= social * (behavior == "flee" ? 2.0f : 1.0f);
                // Standing still, only keep a polite distance.
                if (behavior == "idle" || behavior == "graze" || behavior == "drink" || behavior == "watch") {
                    w.alignment = 0.0f;
                    w.cohesion *= 0.3f;
                }
                total += flock(m, mates, w) * 0.6f;
            }
        }
    }
    if (!m_obstacles.empty()) {
        // Going to a spot right next to an obstacle (cover behind a crate, a
        // place at a wall), from the spot's side of it: that obstacle isn't
        // in the way, so it doesn't push the agent off its spot. From the
        // far side it is still walked around.
        const bool toSpot = a.order.kind == Order::Kind::MoveTo || a.order.kind == Order::Kind::Hold;
        if (toSpot) {
            const glm::vec3 goal = a.order.position;
            std::vector<CircleObstacle> inWay;
            for (const CircleObstacle& o : m_obstacles) {
                const bool hugged = flatDistance(goal, o.centre) - o.radius < s.radius + 0.6f;
                const glm::vec3 toAgent = a.position - o.centre, toGoal = goal - o.centre;
                const bool goalSide = toAgent.x * toGoal.x + toAgent.z * toGoal.z > 0.0f;
                if (!(hugged && goalSide)) inWay.push_back(o);
            }
            if (!inWay.empty()) total += avoidObstacles(m, inWay, 1.0f);
        } else {
            total += avoidObstacles(m, m_obstacles, 1.0f);
        }
    }

    const glm::vec3 before = a.position;
    integrate(m, total, dt);
    glm::vec3 after = m.position;
    if (m_nav && m_nav->valid()) {
        after = m_nav->moveAlongSurface(before, after);
    } else if (groundHeight) {
        float y = after.y;
        if (groundHeight(after.x, after.z, y)) after.y = y;
    }
    a.position = after;
    a.velocity = (after - before) / dt;
    a.velocity.y = 0.0f;
    a.desiredVelocity = m.velocity;

    // Face where it's going, or what it's looking at when standing.
    glm::vec3 face(0.0f);
    if (flatLength(a.velocity) > 0.2f) face = a.velocity;
    else if (a.hasLookAt) face = a.lookAt - a.position;
    if (flatLength(face) > 1e-3f) {
        const float want = glm::degrees(std::atan2(face.x, face.z));
        const float diff = std::remainder(want - a.yaw, 360.0f);
        const float step = s.turnRate * dt;
        a.yaw += std::clamp(diff, -step, step);
        a.yaw = std::remainder(a.yaw, 360.0f);
    }
}

// ---------------------------------------------------------------------------
// Debug

std::vector<float> AiWorld::actionScores(AgentId id) const {
    const Agent* a = agent(id);
    if (!a) return {};
    const Species& s = m_species[size_t(a->species)];
    return scoreAll(s.actions, [this, a](const std::string& n) { return computeInput(*a, n); });
}

std::string AiWorld::actionName(AgentId id) const {
    const Agent* a = agent(id);
    if (!a || a->action < 0) return a && a->actor ? "player" : "idle";
    return m_species[size_t(a->species)].actions[size_t(a->action)].name;
}

std::string AiWorld::describe(AgentId id) const {
    const Agent* a = agent(id);
    if (!a) return "(none)";
    const Species& s = m_species[size_t(a->species)];
    char buf[256];
    std::snprintf(buf, sizeof(buf), "%s %u %s %.2f fear %.2f hunger %.2f focus %u anim %s", s.id.c_str(), a->id, actionName(id).c_str(),
                  double(a->actionScore), double(computeInput(*a, "fear")), double(computeInput(*a, "hunger")), a->focus, a->anim.c_str());
    return buf;
}

} // namespace kke::ai
