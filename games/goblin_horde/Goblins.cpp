// The goblins: Synty SIDEKICK Goblin Fighters (the modular .sk characters,
// merged into one skinned model each with kke::loadSidekickCharacter), made
// crowd-cheap with kke::simplifyModel, animated with the Goblin Locomotion
// pack's Sidekick clips, and driven by the engine AI core.
#include "HordeModule.h"

#include "Flinch.h"

#include "kke/Application.h"
#include "kke/AssetCatalog.h"
#include "kke/Log.h"
#include "kke/MeshLod.h"
#include "kke/Sidekick.h"
#include "kke/modules/RigidBodyModule.h"

#include <SDL3/SDL.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <future>
#include <map>

namespace horde {

namespace fs = std::filesystem;

namespace {

// A clawing swipe: slow to start (you can see it coming), weak alone,
// dangerous from six sides.
kke::AttackDesc swipe() {
    kke::AttackDesc a = kke::AttackDesc::light();
    a.name = "swipe";
    a.windup = 0.45f;
    a.active = 0.14f;
    a.recovery = 0.5f;
    a.damage = 5.0f;
    a.staminaCost = 0.0f;
    a.poiseDamage = 9.0f;
    a.reach = 0.75f;
    a.height = 1.0f;
    a.radius = 0.45f;
    a.knockback = 0.8f;
    a.hitStun = 0.2f;
    a.chip = 0.1f;
    a.guardDamage = 7.0f;
    return a;
}

float yawOf(const glm::vec3& v) { return glm::degrees(std::atan2(v.x, v.z)); }

// The clips a goblin needs, from the Goblin Locomotion pack (Sidekick
// versions, in place: not the _RM root-motion ones).
struct ClipFile {
    const char* tag;
    const char* file;
};
constexpr ClipFile kClips[] = {
    { "|idle", "A_MOD_GBL_Idle_Standing_Neut.fbx" },  { "|walk", "A_MOD_GBL_Walk_F_Neut.fbx" },
    { "|run", "A_MOD_GBL_Run_F_Neut.fbx" },           { "|sprint", "A_MOD_GBL_Sprint_F_Neut.fbx" },
    { "|swipe", "A_MOD_GBL_Idle_Fidget_Swipe_Neut.fbx" }, { "|menace", "A_MOD_GBL_Idle_Fidget_Menacing_Neut.fbx" },
};

} // namespace

void HordeModule::loadGoblins() {
    auto log = kke::log::get(name());
    const char* base = SDL_GetBasePath();
    const std::string packDir = kke::findAssetFolder("assets/synty", { "KKE_ASSETS_DIR", "KKE_SYNTY_DIR" }, base ? base : "");
    if (packDir.empty()) {
        log->info("no asset folder (assets/synty or KKE_ASSETS_DIR): goblins can't be shown, the waves still come");
        return;
    }
    // The .sk files and the clips, found by name anywhere under the packs.
    std::vector<fs::path> sk;
    std::map<std::string, fs::path> clipPath;
    std::error_code ec;
    // A pack that isn't there is the player's choice (info, and said once);
    // an asset missing from a pack that is there is a warning.
    bool havePack[2] = { false, false };
    const char* packs[2] = { "SIDEKICK_Goblin_Fighters", "ANIMATION_Goblin_Locomotion" };
    for (int i = 0; i < 2; ++i) {
        const char* pack = packs[i];
        const fs::path root = fs::path(packDir) / pack;
        if (!fs::exists(root, ec)) {
            log->info("{} not found under {}: the goblins need it", pack, packDir);
            continue;
        }
        havePack[i] = true;
        for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec); it != fs::recursive_directory_iterator();
             it.increment(ec)) {
            const fs::path& p = it->path();
            const std::string file = p.filename().string();
            if (p.extension() == ".sk" && p.string().find("GoblinFighters") != std::string::npos) sk.push_back(p);
            for (const ClipFile& c : kClips)
                if (file == c.file && p.string().find("Sidekick") != std::string::npos) clipPath[c.tag] = p;
        }
    }
    std::sort(sk.begin(), sk.end());
    const size_t want = static_cast<size_t>(std::clamp(static_cast<int>(std::getenv("KKE_HORDE_VARIANTS") ? std::atoi(std::getenv("KKE_HORDE_VARIANTS")) : 5), 1, 16));
    if (sk.size() > want) sk.resize(want);
    std::vector<std::pair<std::string, kke::ModelData>> clips;
    kke::ModelLoadOptions animOnly;
    animOnly.allowNoMeshes = true;
    for (const ClipFile& c : kClips) {
        auto it = clipPath.find(c.tag);
        if (it == clipPath.end()) {
            if (havePack[1]) log->warn("goblin clip {} not found (ANIMATION_Goblin_Locomotion)", c.file);
            continue;
        }
        try {
            clips.emplace_back(c.tag, kke::loadModel(it->second.string(), animOnly));
        } catch (const std::exception& e) {
            log->warn("{}: {}", c.file, e.what());
        }
    }

    const float ratio = std::clamp(std::getenv("KKE_HORDE_LOD") ? static_cast<float>(std::atof(std::getenv("KKE_HORDE_LOD"))) : 0.2f, 0.02f, 1.0f);
    kke::SidekickLoadOptions lo;
    // Too small to see in a crowd.
    lo.skipTypes = { "Tongue", "Teeth", "EyebrowLeft", "EyebrowRight" };
    kke::SimplifyOptions so;
    so.acrossSeams = true;
    so.prune = true;
    // Each look is ~30 part files to read, merge and simplify: all at
    // once, on their own threads (pure CPU; the GPU upload stays here).
    struct Loaded {
        std::string name, error;
        kke::ModelData lod;
        size_t fullTris = 0, tris = 0;
        std::vector<std::string> missing;
    };
    std::vector<std::future<Loaded>> jobs;
    for (const fs::path& file : sk)
        jobs.push_back(std::async(std::launch::async, [file, lo, so, ratio]() {
            Loaded r;
            r.name = file.stem().string();
            try {
                const kke::SidekickCharacter ch = kke::readSidekickCharacter(file.string());
                r.name = ch.name;
                kke::ModelData full = kke::loadSidekickCharacter(ch, lo, &r.missing);
                for (const kke::ModelMesh& m : full.meshes) r.fullTris += m.indices.size() / 3;
                r.lod = ratio < 1.0f ? kke::simplifyModel(full, ratio, so) : std::move(full);
                for (const kke::ModelMesh& m : r.lod.meshes) r.tris += m.indices.size() / 3;
            } catch (const std::exception& e) {
                r.error = e.what();
            }
            return r;
        }));
    for (auto& job : jobs) {
        Loaded r = job.get();
        if (!r.error.empty()) {
            log->warn("{}: {}", r.name, r.error);
            continue;
        }
        for (const std::string& m : r.missing) log->warn("{}: part {} not found", r.name, m);
        m_triangles += r.tris;
        m_fullTriangles += r.fullTris;
        kke::ModelData& lod = r.lod;
        Variant v;
        v.name = r.name;
        v.rig.bones = lod.bones;
        v.rig.boundsMin = lod.boundsMin;
        v.rig.boundsMax = lod.boundsMax;
        for (const auto& [tag, data] : clips) {
            const size_t before = v.rig.animations.size();
            kke::appendClipsByBoneName(v.rig, data);
            if (v.rig.animations.size() > before) {
                v.rig.animations.resize(before + 1); // one clip per file
                v.rig.animations.back().name = tag;
            }
        }
        lod.animations.clear();
        v.model = m_models->add(std::move(lod), "horde/" + r.name);
        if (!v.model) continue;
        v.set = std::make_unique<kke::AnimationSet>(v.rig);
        v.yaw = -yawOf(kke::modelForward(v.rig));
        const std::string spine = kke::canonicalBoneName("spine_01");
        for (size_t b = 0; b < v.rig.bones.size(); ++b)
            if (kke::canonicalBoneName(v.rig.bones[b].name) == spine) v.spine = static_cast<int>(b);
        m_variants.push_back(std::move(v));
    }
    if (m_variants.empty() && havePack[0]) log->warn("no goblin could be loaded (SIDEKICK_Goblin_Fighters): the goblins are invisible");
    else if (m_variants.empty()) log->info("no SIDEKICK_Goblin_Fighters pack: the goblins are invisible, the waves still come");
}

HordeModule::Goblin* HordeModule::goblinByCombatant(kke::CombatantId id) {
    for (auto& g : m_goblins)
        if (g->id == id) return g.get();
    return nullptr;
}

HordeModule::Goblin* HordeModule::goblinByAgent(kke::ai::AgentId id) {
    for (auto& g : m_goblins)
        if (g->agent == id) return g.get();
    return nullptr;
}

void HordeModule::spawnGoblin(const glm::vec3& at) {
    const Wave w = waveAt(m_wave);
    auto g = std::make_unique<Goblin>();
    std::uniform_real_distribution<float> u(0.0f, 1.0f);
    g->agent = m_nextAgent++;
    g->position = at;
    g->speedScale = w.speed * (0.85f + 0.3f * u(m_rng));
    g->scale = 0.72f + 0.14f * u(m_rng);
    g->yaw = yawOf(-at);
    kke::CombatStats st = kke::CombatStats::grunt();
    st.maxHealth *= w.health;
    st.radius = 0.3f;
    st.height = 1.9f * g->scale;
    g->id = m_combat.add(1, st);
    m_ai.addAgent(g->agent, "goblin", at, g->yaw);
    m_ai.setTeam(g->agent, 2);
    if (!m_variants.empty()) {
        g->variant = std::uniform_int_distribution<int>(0, static_cast<int>(m_variants.size()) - 1)(m_rng);
        Variant& v = m_variants[static_cast<size_t>(g->variant)];
        if (!v.free.empty()) {
            g->model = v.free.back();
            v.free.pop_back();
            m_models->setVisible(g->model, true);
        } else {
            g->model = m_models->spawn(v.model, glm::mat4(1.0f));
            m_models->setOverlayEnabled(g->model, false);
        }
        m_models->setTint(g->model, glm::vec3(1.0f));
        g->anim = std::make_unique<kke::Animator>(*v.set);
        kke::Animator& a = *g->anim;
        const kke::AnimationSet& s = *v.set;
        auto clip = [&](const char* tag, const char* fallback) {
            const int c = s.find(tag);
            return c >= 0 ? c : s.find(fallback);
        };
        const kke::AttackDesc sw = swipe();
        v.states.idle = a.addClipState("idle", clip("|idle", "|idle"), true);
        v.states.walk = a.addClipState("walk", clip("|walk", "|idle"), true);
        v.states.run = a.addClipState("run", clip("|run", "|walk"), true);
        v.states.sprint = a.addClipState("sprint", clip("|sprint", "|run"), true);
        const int swipeClip = clip("|swipe", "|idle");
        v.states.swipe = a.addClipState("swipe", swipeClip, false, swipeClip >= 0 ? s.duration(swipeClip) / (sw.windup + sw.active + sw.recovery) : 1.0f);
        v.states.menace = a.addClipState("menace", clip("|menace", "|idle"), true);
        a.play(v.states.run, 0.0f);
    }
    m_goblins.push_back(std::move(g));
}

void HordeModule::releaseGoblin(Goblin& g) {
    if (g.ragdoll && m_ragdolls) m_ragdolls->destroyRagdoll(g.ragdoll);
    g.ragdoll = 0;
    m_ai.remove(g.agent);
    m_combat.remove(g.id);
    if (g.model && !m_variants.empty()) {
        m_models->setBoneWorldOverride(g.model, {});
        m_models->setVisible(g.model, false);
        m_variants[static_cast<size_t>(g.variant)].free.push_back(g.model);
        g.model = 0;
    }
}

void HordeModule::killGoblin(Goblin& g, const glm::vec3& push) {
    g.dead = true;
    g.deadTime = 0.0f;
    m_ai.setEnabled(g.agent, false);
    if (!m_ragdolls || !g.model || m_ragdollCap <= 0 || m_variants.empty()) return;
    // Too many down at once: the oldest stops simulating and lies still.
    while (static_cast<int>(m_ragdolled.size()) >= m_ragdollCap) {
        Goblin* old = goblinByAgent(m_ragdolled.front());
        m_ragdolled.pop_front();
        if (old && old->ragdoll) {
            m_ragdolls->destroyRagdoll(old->ragdoll);
            old->ragdoll = 0;
            old->frozen = true;
        }
    }
    const Variant& v = m_variants[static_cast<size_t>(g.variant)];
    const glm::mat4 instance = m_models->transform(g.model);
    std::vector<glm::mat4> world = m_models->boneWorld(g.model);
    for (glm::mat4& b : world) b = instance * b;
    std::string missing;
    g.ragdollDesc = kke::buildHumanoidRagdoll(v.rig, world, 30.0f, &missing);
    if (g.ragdollDesc.bodies.empty()) {
        kke::log::get(name())->warn("{}: no ragdoll, the skeleton has no '{}' bone", v.name, missing);
        m_ragdollCap = 0; // same skeleton every time: don't ask again
        return;
    }
    g.binding = kke::bindSkeletonToRagdoll(v.rig, world, g.ragdollDesc);
    g.ragdoll = m_ragdolls->createRagdoll(g.ragdollDesc, g.velocity * 0.5f + push * 0.4f);
    if (!g.ragdoll) return;
    for (const char* b : { "torso", "head" }) m_ragdolls->pushRagdollBody(g.ragdoll, g.ragdollDesc.findBody(b), push);
    m_ragdolled.push_back(g.agent);
}

void HordeModule::updateGoblins(float dt) {
    using S = kke::Combatant::State;
    const glm::vec3 hero = m_rigid->world().characterPosition(m_hero.body);
    const bool heroAlive = m_combat.get(m_hero.id).alive();

    // Who's at the king: the nearest `attackers` go in, the rest wait.
    std::vector<std::pair<float, Goblin*>> byDistance;
    for (auto& g : m_goblins)
        if (!g->dead) byDistance.push_back({ glm::length(glm::vec2(g->position.x - hero.x, g->position.z - hero.z)), g.get() });
    std::sort(byDistance.begin(), byDistance.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (size_t i = 0; i < byDistance.size(); ++i) {
        Goblin& g = *byDistance[i].second;
        const kke::Combatant& c = m_combat.get(g.id);
        m_ai.setInput(g.agent, "crowded", i < static_cast<size_t>(m_attackers) ? 0.0f : 1.0f);
        // The queue fans out in rings: the next in line closest.
        if (i >= static_cast<size_t>(m_attackers)) g.waitRadius = 3.2f + 0.8f * std::sqrt(static_cast<float>(i) - static_cast<float>(m_attackers));
        m_ai.setInput(g.agent, "morale", heroAlive ? m_morale : 1.0f);
        m_ai.setInput(g.agent, "health", c.healthFraction());
    }
    m_ai.update(dt);
    for (const kke::ai::AiEvent& e : m_ai.takeEvents())
        if (e.kind == kke::ai::AiEvent::Kind::Attack)
            if (Goblin* g = goblinByAgent(e.who)) g->strike = true;

    std::uniform_real_distribution<float> u(0.0f, 1.0f);
    for (auto& gp : m_goblins) {
        Goblin& g = *gp;
        if (g.dead) {
            g.deadTime += dt;
            continue;
        }
        kke::Combatant& c = m_combat.get(g.id);
        const kke::ai::Agent* a = m_ai.agent(g.agent);
        g.tactic = m_ai.actionName(g.agent);
        const glm::vec3 toHero(hero.x - g.position.x, 0.0f, hero.z - g.position.z);
        const float dist = glm::length(toHero);
        const glm::vec3 heroDir = dist > 1e-3f ? toHero / dist : glm::vec3(0.0f, 0.0f, 1.0f);

        if (g.strike) {
            g.strike = false;
            if (c.canAct() && heroAlive && dist < 1.6f) {
                g.yaw = yawOf(heroDir);
                c.place(g.position, heroDir);
                c.attack(swipe());
            }
        }

        glm::vec3 want(0.0f);
        switch (c.state()) {
        case S::Idle:
            if (g.tactic == "wait_turn") {
                // Jeer from a few metres out, drifting round him.
                const glm::vec3 side(heroDir.z, 0.0f, -heroDir.x);
                want = heroDir * std::clamp((dist - g.waitRadius) * 1.5f, -1.5f, 2.5f) + side * ((g.agent % 2 == 0) ? 0.5f : -0.5f);
            } else if (a) {
                want = a->desiredVelocity * g.speedScale;
            }
            break;
        case S::Windup:
            want = heroDir * 0.6f;
            break;
        default:
            break;
        }
        g.velocity += (want - g.velocity) * std::min(1.0f, 10.0f * dt);
        g.push *= std::exp(-5.0f * dt);
        g.position += (g.velocity + g.push) * dt;
        g.position.y = 0.0f;
        // Never through the ruins, never inside the king.
        for (const glm::vec4& o : m_obstacles) {
            const float hx = o.z + 0.3f, hz = o.w + 0.3f;
            const float dx = g.position.x - o.x, dz = g.position.z - o.y;
            if (std::abs(dx) < hx && std::abs(dz) < hz) {
                const float px = hx - std::abs(dx), pz = hz - std::abs(dz);
                if (px < pz) g.position.x += dx < 0.0f ? -px : px;
                else g.position.z += dz < 0.0f ? -pz : pz;
            }
        }
        const glm::vec2 fromHero(g.position.x - hero.x, g.position.z - hero.z);
        const float fh = glm::length(fromHero);
        if (fh < 0.7f && fh > 1e-4f) {
            const glm::vec2 p = fromHero / fh * 0.7f;
            g.position.x = hero.x + p.x;
            g.position.z = hero.z + p.y;
        }
        // Face where it goes; at the king when close.
        const float speed = glm::length(glm::vec2(g.velocity.x, g.velocity.z));
        float wantYaw = g.yaw;
        if (c.state() == S::Windup || c.state() == S::Active || (dist < 4.0f && g.tactic != "break")) wantYaw = yawOf(heroDir);
        else if (speed > 0.3f) wantYaw = yawOf(g.velocity);
        const float d = std::fmod(wantYaw - g.yaw + 540.0f, 360.0f) - 180.0f;
        g.yaw += d * std::min(1.0f, 10.0f * dt);
        const glm::vec3 facing(std::sin(glm::radians(g.yaw)), 0.0f, std::cos(glm::radians(g.yaw)));
        c.place(g.position, facing);
        m_ai.setTransform(g.agent, g.position, g.velocity, g.yaw);
        if (g.hurtFlash > 0.0f) {
            g.hurtFlash = std::max(0.0f, g.hurtFlash - dt * 5.0f);
            if (g.model) m_models->setTint(g.model, glm::mix(glm::vec3(1.0f), glm::vec3(2.2f, 0.6f, 0.5f), g.hurtFlash));
        }
    }

    // The dead: lie as ragdolls a while, then still, then sink away.
    // Runaways past the trees are gone too.
    for (size_t i = 0; i < m_goblins.size();) {
        Goblin& g = *m_goblins[i];
        const bool fled = !g.dead && g.tactic == "break" && glm::length(glm::vec2(g.position.x, g.position.z)) > 30.0f;
        if (g.dead && g.ragdoll && g.deadTime > 5.0f) {
            m_ragdolls->destroyRagdoll(g.ragdoll);
            g.ragdoll = 0;
            g.frozen = true;
            m_ragdolled.erase(std::remove(m_ragdolled.begin(), m_ragdolled.end(), g.agent), m_ragdolled.end());
        }
        if ((g.dead && g.deadTime > 9.0f) || fled) {
            releaseGoblin(g);
            m_goblins.erase(m_goblins.begin() + static_cast<std::ptrdiff_t>(i));
            continue;
        }
        ++i;
    }
}

void HordeModule::animateGoblin(Goblin& g, float dt) {
    if (!g.model || !g.anim || m_variants.empty()) return;
    using S = kke::Combatant::State;
    const Variant& v = m_variants[static_cast<size_t>(g.variant)];
    if (g.dead) {
        if (g.ragdoll) {
            std::vector<glm::mat4> bodies;
            if (m_ragdolls->ragdollBodyTransforms(g.ragdoll, bodies))
                m_models->setBoneWorldOverride(g.model, kke::poseFromRagdoll(v.rig, g.binding, bodies, glm::inverse(m_models->transform(g.model))));
        } else if (!g.frozen) {
            // No ragdoll (none left or none possible): topple over.
            const float t = std::min(1.0f, g.deadTime * 2.5f);
            const glm::mat4 xf = glm::rotate(glm::scale(glm::rotate(glm::translate(glm::mat4(1.0f), g.position), glm::radians(g.yaw + v.yaw), glm::vec3(0, 1, 0)),
                                                        glm::vec3(g.scale)),
                                             glm::radians(-85.0f * t), glm::vec3(1, 0, 0));
            m_models->setTransform(g.model, xf);
        }
        if (g.deadTime > 7.0f) {
            // Into the ground.
            glm::mat4 xf = m_models->transform(g.model);
            xf = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -0.35f * dt, 0.0f)) * xf;
            m_models->setTransform(g.model, xf);
        }
        return;
    }
    const kke::Combatant& c = m_combat.get(g.id);
    const glm::mat4 xf = glm::scale(glm::rotate(glm::translate(glm::mat4(1.0f), g.position), glm::radians(g.yaw + v.yaw), glm::vec3(0, 1, 0)), glm::vec3(g.scale));
    m_models->setTransform(g.model, xf);

    kke::Animator& a = *g.anim;
    const int state = static_cast<int>(c.state());
    const bool entered = state != g.lastState;
    g.lastState = state;
    if (c.state() == S::Windup) {
        if (entered) a.play(v.states.swipe, 0.08f, true);
    } else if (c.state() == S::Idle || c.state() == S::Stunned) {
        const bool swiping = a.current() == v.states.swipe && !a.finished();
        if (!swiping) {
            const float speed = glm::length(glm::vec2(g.velocity.x, g.velocity.z)) / g.scale;
            int want = v.states.idle;
            if (speed > 4.2f) want = v.states.sprint;
            else if (speed > 2.2f) want = v.states.run;
            else if (speed > 0.35f) want = v.states.walk;
            else if (g.tactic == "wait_turn") want = v.states.menace;
            if (a.current() != want) a.play(want, 0.25f);
        }
    }
    a.update(dt);
    std::vector<glm::mat4>* locals = m_models->boneLocals(g.model);
    if (!locals) return;
    if (g.flinch > 0.0f) {
        kke::Pose pose = a.pose();
        const glm::vec3 awayModel = glm::vec3(glm::inverse(xf) * glm::vec4(g.flinchDir, 0.0f));
        applyFlinch(v.rig, pose, v.spine, awayModel, g.flinch, 30.0f);
        g.flinch = std::max(0.0f, g.flinch - dt * 3.5f);
        kke::poseToLocals(pose, *locals);
    } else {
        kke::poseToLocals(a.pose(), *locals);
    }
}

} // namespace horde
