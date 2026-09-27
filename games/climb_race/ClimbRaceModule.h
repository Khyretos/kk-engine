#pragma once

#include "kke/AnimRig.h"
#include "kke/Animator.h"
#include "kke/CameraRig.h"
#include "kke/ClimbWall.h"
#include "kke/Climber.h"
#include "kke/Locomotion.h"
#include "kke/Module.h"
#include "kke/RigidWorld.h"
#include "kke/modules/ModelModule.h"

#include "NetRace.h"

#include <RmlUi/Core/DataModelHandle.h>

#include <memory>
#include <string>
#include <vector>

namespace kke {
class DynamicMeshRenderer;
class InputModule;
class LobbyModule;
class NetModule;
class RigidBodyModule;
class UiModule;
} // namespace kke
namespace Rml { class ElementDocument; }

namespace climb_race {

// Climb Race: identical generated rock faces side by side, like a
// speed-climbing final, one per climber. You start on the ground in front
// of yours; the first to mantle over the summit wins. On the rock you
// choose each hold yourself (kke::Climber): a bumper reaches precisely, a
// trigger held and let go lunges (further the longer you hold), both
// together snatch quickly. Stamina runs out on bad holds and overhangs;
// stand on a ledge or hang on two jugs to get it back. Loose holds come
// off under a lunge and fall down the face (Jolt bodies).
//
// It starts in the lobby (kke::LobbyModule): the climbers line up in front
// of the mountain, every controller that presses A joins (up to four
// players, split screen), each player picks a name and a colour, and
// player 1 sets 0 to 5 CPU climbers (kke::ClimbBot) and how good each one
// is. A controller that joins during a race is in from the next one.
// Walking, jumping and catching ledges between climbs is kke::Locomotion;
// the body is the UAL mannequin with two-bone IK on all four limbs.
//
// Headless / demo switches: KKE_CLIMB_SEED=<n> (the mountain),
// KKE_CLIMB_LOBBY=0 (straight into a race: you and KKE_CLIMB_CPUS=<n>
// rivals, default 1), KKE_CLIMB_AUTOPILOT=1 (no lobby, and you climb by
// yourself too), KKE_CLIMB_BOT_PAUSE=<s> (every CPU's breath between
// moves), KKE_CLIMB_QUIT=<s> (quit after that long, with a log of every
// climber's height every few seconds), KKE_CLIMB_ROCKFALL=1 (no lobby;
// every loose hold on your face comes off two seconds in: watch them
// bounce down the rock and the ledges; where they came to rest is logged).
// KKE_LOBBY_JOIN=<n> joins n controllers in the lobby (see LobbyModule.h).
class ClimbRaceModule : public kke::Module {
public:
    ClimbRaceModule();
    ~ClimbRaceModule() override;
    const char* name() const override { return "ClimbRace"; }
    std::vector<kke::ModuleDependency> dependencies() const override;
    void init(kke::Application& app) override;
    void update(const kke::UpdateContext& ctx) override;
    void render(const kke::RenderContext& ctx) override;
    void renderShadow(const kke::ShadowRenderContext& ctx) override;
    void onEvent(const SDL_Event& event) override;

private:
    // One face of the mountain (a lane): the generated rock, its Jolt
    // bodies, what draws it, and its loose holds.
    struct Loose {
        int hold = -1;
        kke::RigidWorld::BodyId body = kke::RigidWorld::kNoBody;
        std::unique_ptr<kke::DynamicMeshRenderer> mesh;
        bool fallen = false;
    };
    struct Lane {
        std::unique_ptr<kke::ClimbWall> wall;
        glm::vec3 offset{0.0f}; // wall space -> world
        std::vector<kke::RigidWorld::BodyId> bodies;
        std::unique_ptr<kke::DynamicMeshRenderer> mesh;
        std::vector<Loose> loose;
    };
    struct Racer {
        int lane = 0;
        int seat = -1;              // lobby seat (-1: a CPU climber)
        int player = 0;             // input map (humans)
        bool bot = false;
        bool mouse = false;         // plays with the mouse (look, crosshair)
        int difficulty = 1;         // CPU climbers: the lobby's difficulty
        float pause = 0.45f;        // CPU climbers: breath between moves
        std::string name;
        glm::vec3 tint{1.0f};
        kke::RigidWorld::CharacterId id = 0;
        std::unique_ptr<kke::Locomotion> loco;
        std::unique_ptr<kke::Climber> climber;
        std::unique_ptr<kke::ClimbBot> brain;
        kke::CameraRig rig;
        kke::Camera camera;
        kke::ModelModule::InstanceId model = 0;
        std::unique_ptr<kke::Animator> anim;
        float armWeight = 0.0f, legWeight = 0.0f, footWeight = 0.0f;
        float time = 0.0f;          // race clock
        bool finished = false;
        float regrab = 0.0f;        // after a fall: no grabbing for a moment
        float idleLook = 0.0f;      // seconds without looking around (camera settles)
        float restTimer = 0.0f;     // bot standing on a ledge
        int crosshair = -1;         // hold under the crosshair (mouse aiming)
        bool crosshairOut = false;  // ... out of reach
        bool jumpQueued = false;
        bool wasClimbing = false;
        float fallStartY = 0.0f;
        int falls = 0;
        // Online (Net.cpp): its player in the game, and whether another
        // machine plays it (then it's posed from what that machine sends).
        int netId = -1;             // network player id (-1: offline)
        int netSlot = -1;           // ours: NetModule's local player slot (0 = the first)
        bool remote = false;
        netrace::Pose net;          // remote: the newest pose
        uint8_t netLoco = 0;        // remote: its loco state last frame (jumps and landings from the changes)
        float netStateTime = 0.0f;
        bool netJumped = false, netLanded = false;
    };
    // What the body is drawn from (Body.cpp): the local climber and
    // Locomotion, or (online) the pose another machine sent.
    struct BodyInput {
        glm::vec3 feet{0.0f};
        float yaw = 0.0f;
        glm::vec3 in{0.0f, 0.0f, -1.0f}; // facing: into the rock
        bool climbing = false, mantle = false;
        float mantleProgress = 0.0f;
        kke::Locomotion::State loco = kke::Locomotion::State::Ground;
        float stateTime = 0.0f, groundSpeed = 0.0f, fallHeight = 0.0f;
        bool jumped = false, landed = false;
        glm::vec3 hand[2]{}, foot[2]{}; // world: where the IK puts them (hands also on a ledge hang)
    };
    BodyInput bodyInput(const Racer& r) const;
    struct RacerInput {
        kke::Locomotion::Input loco;
        kke::Climber::Input climb;
        glm::vec2 look{0.0f};        // degrees this frame
        bool grab = false;           // any grab button: get on the rock
        bool mantle = false;         // jump: over the edge
    };

    void buildMountain(uint32_t seed, int lanes);
    void clearMountain();
    void buildScenery();
    void resetRace();

    // The lobby (Lobby.cpp): the menu's fields, the climbers lined up
    // behind it, and the race's roster from it.
    void setupLobby();
    void updateLobby(float dt);
    void startFromLobby();
    void backToLobby();
    // The roster the lobby asks for now: who, in which seat, how good.
    struct Entry {
        int seat = -1, difficulty = 1;
        std::string name;
        glm::vec3 tint{1.0f};
        int netId = -1;          // online: its network player
        bool remote = false;     // online: another machine plays it
    };
    std::vector<Entry> wantedRoster() const;
    void buildRacers(const std::vector<Entry>& roster);
    void removeRacer(Racer& r);
    void applyLooks();
    int humans() const;
    kke::Camera& cameraOf(Racer& r);

    RacerInput readPlayer(Racer& r, float dt);
    RacerInput readBot(Racer& r, float dt);
    void updateRacer(Racer& r, float dt);
    void makeBrain(Racer& r);
    void updateCamera(Racer& r, float dt, kke::Camera& out);
    int crosshairHold(const Racer& r, const kke::Camera& cam, bool& outOfReach) const;
    void dropLoose(Lane& lane, int hold, const glm::vec3& push);

    // Online (Net.cpp): Host and Join in the lobby, the host's race setup,
    // everyone's climbers.
    void setupNet();
    void updateNet(float dt);         // before the racers move: events, remote racers
    void sendNet();                   // after: our racers' poses
    void onNetEvent(const kke::net::GameEventMsg& e);
    void applySetup(const netrace::Setup& s);
    void sendSetup();
    void syncNetPlayers();            // our lobby seats -> NetModule's local players
    std::vector<Entry> onlineRoster() const; // host: ours (wantedRoster) + everyone else's
    std::vector<std::string> onlineNames() const; // host: the other screens' players, so the CPU climbers pick other names
    bool netClient() const;           // in someone else's game
    bool netHost() const;
    void netFinished(Racer& r);
    void netLoose(int lane, int hold, const glm::vec3& push);
    std::string netStatus() const;

    // The character (Body.cpp).
    void loadCharacter();
    void setupBody(Racer& r);
    void animateBody(Racer& r, float dt);

    // The HUD (Hud.cpp, ui/climb_hud.rml).
    void buildHud();
    void updateHud(float dt);

    glm::vec3 toWorld(const Racer& r, const glm::vec3& p) const { return p + m_lanes[static_cast<size_t>(r.lane)]->offset; }
    glm::vec3 toWall(const Racer& r, const glm::vec3& p) const { return p - m_lanes[static_cast<size_t>(r.lane)]->offset; }

    kke::Application* m_app = nullptr;
    kke::RigidBodyModule* m_rigid = nullptr;
    kke::InputModule* m_input = nullptr;
    kke::ModelModule* m_models = nullptr;
    kke::LobbyModule* m_lobby = nullptr;
    kke::NetModule* m_net = nullptr;
    uint32_t m_round = 0, m_sentRound = 0; // races started (resetRace); the host sends each one
    uint32_t m_netRound = 0;               // the online race's number (the host's m_round)
    float m_netSearchAt = 0.0f;            // Join: when to ask the LAN again (m_netTime)
    float m_netTime = 0.0f;
    std::string m_netName;                 // KKE_NET_NAME: player 1's name, here and online                // s, also in the menu (m_clock stops there)
    int m_netWait = 0;                     // KKE_CLIMB_WAIT: the host starts once that many others are in
    bool m_netHold = false;                // the countdown waits for every machine (Ready / Go)
    float m_netHeld = 0.0f;                // host: seconds waited
    std::vector<int> m_netPending;         // host: racers whose machine hasn't said it's ready
    bool m_netApplying = false;            // a remote racer's event is being applied: don't send it back
    bool m_wasOnline = false;
    std::string m_lastNetStatus;

    uint32_t m_seed = 7;
    std::vector<std::unique_ptr<Lane>> m_lanes;
    std::vector<Racer> m_racers;
    kke::Camera m_overview; // three players: the fourth quarter's view of the whole race
    std::vector<kke::RigidWorld::BodyId> m_scenery;
    std::unique_ptr<kke::DynamicMeshRenderer> m_ground, m_markers[4];

    enum class Phase { Lobby, Countdown, Racing, Finished };
    Phase m_phase = Phase::Lobby;
    float m_countdown = 3.0f;
    float m_best = 0.0f;       // best time on this mountain (0 = none yet)
    std::string m_winner;
    bool m_autopilot = false, m_captured = false;
    float m_botPause = -1.0f; // KKE_CLIMB_BOT_PAUSE (-1: from the difficulty)
    int m_defaultCpus = 1;    // no lobby: KKE_CLIMB_CPUS
    bool m_rosterChanged = false; // someone joined during a race
    float m_quitAfter = -1.0f, m_clock = 0.0f, m_reportAt = 0.0f;
    float m_rockfall = -1.0f; // KKE_CLIMB_ROCKFALL: seconds until it starts (-1 = off)
    void updateRockfall(float dt);
    float m_mouseSensitivity = 0.12f, m_stickSpeed = 200.0f;

    // Character: the UAL mannequin's bones and clips (retargeting not
    // needed: it's the clips' own skeleton).
    kke::ModelModule::ModelId m_charModel = 0;
    kke::ModelData m_rigData;
    std::unique_ptr<kke::AnimationSet> m_animSet;
    float m_modelYaw = 0.0f;
    kke::TwoBoneChain m_arm[2], m_leg[2];
    kke::FootPlacer m_feet;
    int m_stMove = -1, m_stJump = -1, m_stFall = -1, m_stLand = -1, m_stHang = -1, m_stTop = -1;
    std::unique_ptr<kke::DynamicMeshRenderer> m_capsule; // no character model: a block

    // HUD: a panel per player (in the corner of their view), and the
    // standings of every climber.
    struct PlayerHud {
        std::string name, time, stamina = "100%", staminaColor = "#6fe39a", left, right, height, status, x, y, accent;
        bool low = false;
    };
    struct RivalHud {
        std::string name, height, accent, status;
    };
    struct Hud {
        std::vector<PlayerHud> players;
        std::vector<RivalHud> rivals;
        std::string banner, sub, hint;
        bool racing = false;
    };
    Hud m_hud;
    Rml::DataModelHandle m_hudModel;
    Rml::ElementDocument* m_hudDoc = nullptr;
};

} // namespace climb_race
