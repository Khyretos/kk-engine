#pragma once

#include "Art.h"
#include "Roster.h"

#include "kke/AnimRig.h"
#include "kke/Animator.h"
#include "kke/CameraRig.h"
#include "kke/Capabilities.h"
#include "kke/CharacterIk.h"
#include "kke/Combat.h"
#include "kke/Module.h"
#include "kke/Ragdoll.h"
#include "kke/RigidWorld.h"
#include "kke/ai/AiWorld.h"
#include "kke/ai/NavMesh.h"
#include "kke/modules/ModelModule.h"

#include <RmlUi/Core/DataModelHandle.h>

#include <deque>
#include <memory>
#include <random>
#include <set>
#include <string>
#include <vector>

namespace Rml {
class ElementDocument;
}

namespace kke {
class InputModule;
class InputMap;
class LobbyModule;
class NetModule;
class RigidBodyModule;
class DynamicMeshRenderer;
class SphereImpostorRenderer;
class ParticleEffects;
namespace net {
struct GameEventMsg;
}
} // namespace kke

namespace horde {

// Goblin Horde: hold the old fort against waves of goblins, alone or
// with up to three friends at the same screen and more online.
//
//  - Players pick who they are in the start menu (kke::LobbyModule):
//    character, skin, accessory, weapon. Sword and great axe: a three-hit
//    combo, a heavy swing, and holding heavy charges a spin that hits all
//    round. Bow: hold to draw, the longer the harder, aim for the head.
//    Crossbow: the same bolt every time, then a slow reload. The pause
//    menu's Inventory swaps weapons.
//  - Five goblin types and the bosses are data (data/foes.yml): grunts,
//    archers, battle shamans, war shamans who buff and heal the others,
//    brutes; a Fantasy Rivals giant every few waves (waves.yml), each with
//    a ranged attack, a combo and a super attack, all marked on the ground
//    before they land.
//  - Their minds are the engine AI core (kke::ai::AiWorld,
//    data/goblin.yml): charge, wait their turn round a player who's
//    already surrounded, break when the wave's morale goes; paths go round
//    the ruins on a Recast navmesh.
//  - Hits, blocks, stamina and knockdowns are kke::CombatWorld.
//  - Online (kke::NetModule): the host runs the horde and sends where
//    every goblin is; each screen runs its own players and tells the host
//    what they hit (README.md "Online").
//
// Environment: KKE_HORDE_BOT=1 (player 1 fights by himself),
// KKE_HORDE_QUIT=<s> (quit after that long, a report every 10 s),
// KKE_HORDE_MAX=<n> (goblins alive at once, default 60),
// KKE_HORDE_WAVE=<n> (start wave), KKE_HORDE_WEAPON=<id> (player 1's
// weapon), KKE_HORDE_BOSS=1 (a boss in the first wave),
// KKE_HORDE_RAGDOLLS=<n>, KKE_HORDE_LINEUP=1 (every goblin type and boss
// in a row, showing each move in turn: for checking the art),
// KKE_HORDE_POSE=<clip> (every goblin plays that UAL clip).
class HordeModule : public kke::Module {
public:
    HordeModule();
    ~HordeModule() override;
    const char* name() const override { return "Horde"; }
    std::vector<kke::ModuleDependency> dependencies() const override;
    void init(kke::Application& app) override;
    void update(const kke::UpdateContext& ctx) override;
    void render(const kke::RenderContext& ctx) override;
    void renderShadow(const kke::ShadowRenderContext& ctx) override;
    void renderTranslucent(const kke::RenderContext& ctx) override;
    void onEvent(const SDL_Event& e) override;
    void shutdown() override;

    // ---- Shared by the parts (public so the helpers in each .cpp reach them)

    // One animated character in the world: the model, what it holds and
    // wears, its animator and the states it plays.
    struct Puppet {
        const Look* look = nullptr;
        kke::ModelModule::InstanceId model = 0, right = 0, left = 0, head = 0, back = 0;
        glm::mat4 rightGrip{ 1.0f }, leftGrip{ 1.0f }, headAt{ 1.0f }, backAt{ 1.0f };
        bool twoHanded = false;
        std::unique_ptr<kke::Animator> anim;
        int idle = -1, move = -1, hit = -1, knock = -1, getUp = -1, death = -1, block = -1, roll = -1, cheer = -1;
        int strafeL = -1, strafeR = -1, back_ = -1;
        std::vector<int> moves;        // a state per move (foes) or per weapon move (heroes)
        int lastState = -1;
        float flinch = 0.0f;
        glm::vec3 flinchDir{ 0.0f };
        float hurtFlash = 0.0f;
        glm::vec3 tint{ 1.0f };
        glm::mat4 xf{ 1.0f };          // the model's transform this frame
        glm::mat4 rightAt{ 1.0f };     // the right-hand prop in the world this frame
        glm::vec3 headPos{ 0.0f }, handPos{ 0.0f }, offhandPos{ 0.0f };
        std::vector<glm::mat4> bones;  // model space, this frame
    };

    // A player: here (a seat in the lobby, an input map), online
    // (another screen's, drawn from what it sends) or the bot.
    struct Hero {
        int seat = -1, player = 0;     // lobby seat, InputModule player
        int slot = 0;                  // this screen's players in order (split screen, online local slot)
        bool remote = false;
        int netId = -1;                // network player id (-1 offline)
        std::string name;
        glm::vec3 color{ 1.0f };
        int character = 0, skin = 0, accessory = 0, weapon = 0;
        kke::CombatantId id = 0;
        kke::RigidWorld::CharacterId body = 0;
        kke::ai::AgentId agent = 0;
        Puppet look;
        std::unique_ptr<kke::CharacterIk> ik;
        glm::vec3 facing{ 0.0f, 0.0f, 1.0f }, push{ 0.0f }, wish{ 0.0f };
        glm::vec3 position{ 0.0f }, velocity{ 0.0f }; // remote heroes: as sent
        // Melee
        int combo = 0;                 // hits done in this combo (0..3)
        bool queued = false;           // light pressed during a swing: the next one follows
        float sinceSwing = 9.0f;
        bool wasDown = false;
        float upGrace = 0.0f;          // s left after getting up: no hits, goblins hold off (no knockdown lock)
        float heavyHeld = -1.0f;       // s heavy has been held (-1: not held)
        bool charged = false;
        float spin = 0.0f;             // degrees turned in a spin
        int moveState = -1;            // the move being played (index into look.moves)
        std::string attack;            // the attack's name (for the network and the HUD)
        // Ranged
        bool aiming = false, drawing = false;
        float draw = 0.0f, reload = 0.0f;
        glm::vec3 aimPoint{ 0.0f };
        // Camera, HUD, score
        kke::CameraRig rig;
        kke::Camera camera;
        bool bot = false;
        int kills = 0;
        float downTime = 0.0f;         // s since going down (back next wave)
        int netState = 0, netFlags = 0;
        float netProgress = 0.0f;
    };

    // A goblin or a boss.
    struct Foe {
        const FoeType* type = nullptr;
        int typeIndex = 0;
        uint16_t netId = 0;
        kke::ai::AgentId agent = 0;
        kke::CombatantId id = 0;
        Puppet look;
        glm::vec3 position{ 0.0f }, velocity{ 0.0f }, push{ 0.0f };
        float yaw = 0.0f; // degrees, 0 = +z
        float speedScale = 1.0f, scale = 1.0f;
        float waitRadius = 3.4f;
        std::string tactic;
        int target = -1; // index into m_heroes
        std::vector<float> cooldown; // per move
        int move = -1;               // the move under way (index into type->moves)
        int comboHit = 0;
        float moveTime = 0.0f;
        bool fired = false;
        glm::vec3 aimAt{ 0.0f };
        int markId = 0;
        bool strike = false;
        float haste = 0.0f, stoneskin = 0.0f;
        bool dead = false;
        float deadTime = 0.0f;
        bool frozen = false;
        kke::IRagdollPhysics::RagdollHandle ragdoll = 0;
        kke::RagdollDesc ragdollDesc;
        kke::RagdollSkinBinding binding;
        float nextThink = 0.0f;      // s before it picks another move
        bool roared = false;         // a super attack: done roaring, swinging now
        // What it looks like (the same on every screen online).
        int lookIndex = 0, skin = 0, weaponIndex = -1, offhandIndex = -1;
        // Online copies (a client): where the host says it is.
        glm::vec3 netPos{ 0.0f };
        float netYaw = 0.0f;
        int netMove = -1, netState = 0, netComboHit = 0;
        float health = 1.0f;         // 0..1 (the HUD; a client's copy can't die by itself)
    };

    // Something flying: an arrow, a bolt, a fireball, a boulder.
    struct Shot {
        std::string kind;
        glm::vec3 position{ 0.0f }, velocity{ 0.0f };
        float gravity = 9.8f;
        int team = 1;               // 0: the players', 1: the horde's
        int hero = -1;              // the player who shot it (theirs)
        kke::CombatantId from = 0;
        kke::AttackDesc hit;
        float splash = 0.0f, homing = 0.0f, headshot = 1.0f;
        int target = -1;            // homing: a hero index
        int markId = 0;             // a lob: the warning it lands on
        float life = 6.0f;
        bool stuck = false, local = true; // local: this screen decides what it hits
        kke::ModelModule::InstanceId model = 0;
        float stuckTime = 0.0f;
    };

    // A warning on the ground: where something is about to land.
    struct Mark {
        int id = 0;
        enum class Shape { Circle, Line, Cone } shape = Shape::Circle;
        glm::vec3 center{ 0.0f }, dir{ 0.0f, 0.0f, 1.0f };
        float radius = 1.0f, length = 0.0f;
        float time = 0.0f, total = 1.0f;
        glm::vec3 color{ 1.0f, 0.25f, 0.1f };
        bool super = false;
    };

    // Something that bursts after its mark (a firestorm).
    struct Blast {
        glm::vec3 center{ 0.0f };
        float radius = 2.0f, delay = 1.0f;
        kke::AttackDesc hit;
        std::string element;
        int team = 1;
    };

private:
    enum class Phase { Lobby, Intro, Fighting, Cleared, Overrun };

    // ---- HordeModule.cpp: the flow
    void defineActions();
    void loadData();
    void buildArena();
    void setupLobby();
    void startFromLobby();
    void backToLobby();
    void restart();
    void startWave(int n);
    void updateWaves(float dt);
    void updateCameras(float dt);
    void report(float dt);
    int alivePlayers() const;
    int aliveFoes() const;
    float groundAt(const glm::vec3& p) const;
    bool lineBlocked(const glm::vec3& a, const glm::vec3& b) const;

    // ---- Puppets.cpp: animated characters and what they hold
    void dressPuppet(Puppet& p, const Look* look, const glm::vec3& color, float scale);
    void undressPuppet(Puppet& p);
    void posePuppet(Puppet& p, const kke::Pose& pose, const glm::mat4& xf);
    kke::Pose overlay(const Rig& rig, const kke::Pose& base, const kke::Pose& upper, float weight) const;

    // ---- Heroes.cpp: the players
    struct Entry {
        int seat = -1;
        bool remote = false;
        int netId = -1;
        std::string name;
        glm::vec3 color{ 1.0f };
        int character = 0, skin = 0, accessory = 0, weapon = 0;
    };
    std::vector<Entry> wantedHeroes() const;
    void buildHeroes(const std::vector<Entry>& entries);
    void spawnHero(Hero& h, const glm::vec3& at);
    void removeHero(Hero& h);
    void dressHero(Hero& h);
    void equip(Hero& h, int weapon);
    void updateHero(Hero& h, float dt);
    void heroBot(Hero& h, glm::vec2& move, bool& light, bool& heavyDown, bool& heavyHeld, bool& block, bool& roll, bool& aim);
    void startMove(Hero& h, const Move& m, int state);
    void shootArrow(Hero& h, float drawFraction);
    void animateHero(Hero& h, float dt);
    glm::vec3 heroFeet(const Hero& h) const;
    const Weapon& weaponOf(const Hero& h) const;
    Hero* heroByCombatant(kke::CombatantId id);

    // ---- Foes.cpp: goblins and bosses
    // A client copies the host's goblin: its net id and its look.
    Foe& spawnFoe(int type, bool boss, const glm::vec3& at, uint16_t netId = 0, int look = -1, int skin = -1, int weapon = -1, int offhand = -1);
    void updateFoes(float dt);
    void thinkFoe(Foe& f, float dt);
    void beginFoeMove(Foe& f, int move);
    void runFoeMove(Foe& f, float dt);
    void animateFoe(Foe& f, float dt);
    void killFoe(Foe& f, const glm::vec3& push);
    void releaseFoe(Foe& f);
    void dressFoe(Foe& f);
    Foe* foeByCombatant(kke::CombatantId id);
    Foe* foeByAgent(kke::ai::AgentId id);
    Foe* foeByNet(uint16_t id);
    int pickType();

    // ---- Shots.cpp: projectiles, marks, blasts, effects
    void fire(Shot s);
    void updateShots(float dt);
    int mark(Mark m);
    void unmark(int id);
    void updateMarks(float dt);
    void blast(const Blast& b);
    void burst(const Blast& b);
    void hurtHero(Hero& h, const kke::AttackDesc& a, const glm::vec3& from, const glm::vec3& point);
    void hurtFoe(Foe& f, const kke::AttackDesc& a, const glm::vec3& from, const glm::vec3& point, int byHero);
    void onHit(const kke::HitEvent& e);
    void buildMarkMesh();
    glm::vec3 ballistic(const glm::vec3& from, const glm::vec3& to, float time, float gravity) const;

    // ---- Hud.cpp
    void buildHud();
    void updateHud();
    void openInventory(int hero);
    void updateInventory();

    // ---- Net.cpp: online
    void setupNet();
    void syncNetPlayers();
    void updateNet(float dt);
    void sendNet(float dt);
    void onNetEvent(const kke::net::GameEventMsg& e);
    bool netHost() const;
    bool netClient() const;
    // The host tells an online player's screen they were hit (it decides
    // the block or the parry there).
    void sendHurt(const Hero& h, const kke::AttackDesc& a, const glm::vec3& from, const glm::vec3& point);
    // A shot, a warning or a burst, for every other screen to see.
    void sendFx(const Shot* shot, const Mark* mark, const Blast* blast);
    void sendHeroHit(int foeNet, const kke::AttackDesc& a, const glm::vec3& from, const glm::vec3& point, int byHero);

    kke::Application* m_app = nullptr;
    kke::RigidBodyModule* m_rigid = nullptr;
    kke::InputModule* m_input = nullptr;
    kke::ModelModule* m_models = nullptr;
    kke::LobbyModule* m_lobby = nullptr;
    kke::NetModule* m_net = nullptr;
    kke::IRagdollPhysics* m_ragdolls = nullptr;

    Roster m_roster;
    std::unique_ptr<Art> m_art;
    kke::CombatWorld m_combat;
    kke::ai::AiWorld m_ai;
    kke::ai::NavMesh m_nav;
    std::mt19937 m_rng{ 7 };

    // Arena
    std::unique_ptr<kke::DynamicMeshRenderer> m_arena, m_block, m_marks, m_stock;
    std::unique_ptr<kke::SphereImpostorRenderer> m_spheres;
    std::unique_ptr<kke::ParticleEffects> m_fx;
    std::vector<glm::vec3> m_gates;     // where goblins come in from
    std::vector<glm::vec4> m_obstacles; // x, z, half x, half z: ruins nobody walks through

    // Players
    std::vector<std::unique_ptr<Hero>> m_heroes;
    kke::ai::AgentId m_nextHeroAgent = 1;
    bool m_bot = false;
    bool m_captured = false;
    float m_mouseSensitivity = 0.12f, m_stickSpeed = 160.0f;
    std::string m_startWeapon;

    // Horde
    std::vector<std::unique_ptr<Foe>> m_foes;
    std::deque<kke::ai::AgentId> m_ragdolled; // oldest first
    std::set<const Look*> m_noRagdoll;        // skeletons a ragdoll can't be built for
    kke::ai::AgentId m_nextAgent = 100;
    uint16_t m_nextNetId = 1;
    std::vector<Shot> m_shots;
    std::vector<Mark> m_markList;
    int m_nextMark = 1;
    size_t m_markVerts = 0;
    std::vector<Blast> m_blasts;
    std::vector<kke::ModelModule::InstanceId> m_arrowPool;
    kke::ModelModule::ModelId m_arrowModel = 0, m_boltModel = 0;

    // Waves
    Phase m_phase = Phase::Lobby;
    float m_phaseTime = 0.0f;
    int m_wave = 0;
    int m_toSpawn = 0, m_waveSize = 0;
    int m_bossToSpawn = -1;
    float m_spawnTimer = 0.0f;
    int m_kills = 0, m_waveKills = 0;
    int m_maxAlive = 60, m_ragdollCap = 12, m_startWave = 1;
    bool m_forceBoss = false;
    bool m_lineup = false;   // KKE_HORDE_LINEUP=1: every type in a row, showing its moves
    std::string m_poseClip;  // KKE_HORDE_POSE=<clip>: every goblin holds it (checking a clip on every skeleton)
    float m_morale = 1.0f;
    int m_round = 0;

    // Online
    float m_netTime = 0.0f, m_netSendAt = 0.0f, m_netSearchAt = 0.0f;
    std::string m_lastNetStatus;
    bool m_wasOnline = false;

    // Report / headless
    float m_clock = 0.0f, m_quitAfter = -1.0f, m_reportAt = 10.0f;
    double m_frameMs = 0.0, m_worstMs = 0.0;
    int m_frames = 0;
    double m_aiMs = 0.0, m_animMs = 0.0;
    int m_shotsFired = 0, m_heroHits = 0;

    // HUD
    struct PlayerHud {
        Rml::String name, health = "100%", stamina = "100%", weapon, ammo, accent = "#e8b64c", charge = "0%", status;
        bool low = false, aiming = false, charged = false, down = false, drawing = false;
        Rml::String x = "0%", y = "0%", w = "100%", h = "100%";
    };
    struct InventoryRow {
        Rml::String label, kind, line;
        bool focused = false, equipped = false;
    };
    struct Hud {
        std::vector<PlayerHud> players;
        Rml::String wave, left, kills, banner, sub, hint, boss, bossHealth = "0%";
        bool bossShown = false, inventory = false;
        std::vector<InventoryRow> rows;
        Rml::String inventoryTitle, inventoryHint, invX = "0%", invY = "0%", invW = "100%", invH = "100%";
    };
    Hud m_hud;
    Rml::DataModelHandle m_hudModel;
    Rml::ElementDocument* m_hudDoc = nullptr;
    int m_inventoryHero = -1, m_inventoryRow = 0;
    bool m_inventoryFresh = false; // opened this frame (the button that opened it isn't a menu press)
};

} // namespace horde
