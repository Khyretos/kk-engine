#pragma once

#include "kke/AnimRig.h"
#include "kke/Animator.h"
#include "kke/BodyShape.h"
#include "kke/CameraRig.h"
#include "kke/ClimbWall.h"
#include "kke/Climber.h"
#include "kke/Equipment.h"
#include "kke/Locomotion.h"
#include "kke/Module.h"
#include "kke/RigidWorld.h"
#include "kke/modules/ModelModule.h"

#include "Ghost.h"
#include "Mountains.h"
#include "Progress.h"
#include "NetRace.h"

#include <RmlUi/Core/DataModelHandle.h>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace kke {
class AudioModule;
class DynamicMeshRenderer;
class InputModule;
class LobbyModule;
class NetModule;
class RigidBodyModule;
class UiModule;
namespace net { struct RemotePlayer; }
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
        float grip[2] = {};         // fingers closed on a hold (0 open .. 1 closed)
        float handAim[2] = {};      // hand turned to its hold (0 = as animated)
        kke::BodyAvoidState avoid[2]; // each arm's way round the body, frame to frame
        struct { float worst = 0.0f, sum = 0.0f; int samples = 0; } gripError; // knuckles to hold, m
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
        int medal = -1;             // this race's medal (players; -1 none)
        bool out = false;           // Elimination: out of this race (lets go, watches)
        bool ghost = false;         // Time trial: the best run, played back (remote: posed from it)
        Ghost run;                  // players: this race, recorded (the next ghost if it's the best)
        float hitCooldown = 0.0f;   // Rockfall: s before another rock counts
        float hitFlash = 0.0f;      // Rockfall: s left of "hit by a rock!"
        bool newBest = false;       // ... and it's their best on this mountain
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
        // Each hand: where it closes (the knuckles, world), the rock's normal
        // there, how closed the fingers are (0..1), whether it's on the rock
        // (turned onto it) and on a hold.
        glm::vec3 grip[2]{}, normal[2]{};
        float closed[2]{};
        bool onRock[2]{}, held[2]{};
        // The hold each hand is on or going to (its shape for the fingers).
        kke::ClimbHold::Kind holdKind[2] = { kke::ClimbHold::Kind::Jug, kke::ClimbHold::Kind::Jug };
        float holdSize[2] = { 0.12f, 0.12f };
        glm::vec3 foot[2]{}, hips{0.0f}; // world
    };
    BodyInput bodyInput(const Racer& r) const;
    struct RacerInput {
        kke::Locomotion::Input loco;
        kke::Climber::Input climb;
        glm::vec2 look{0.0f};        // degrees this frame
        bool grab = false;           // any grab button: get on the rock
        bool mantle = false;         // jump: over the edge
    };

    // The mountains (Mountains.h, mountains/*.yaml): the one picked in
    // the menu (or Random), built as one face per climber.
    void loadMountainList();
    Mountain chosenMountain() const;       // the menu's Mountain row
    void useMountain(const Mountain& m);   // m_mountain and its mood (build it next)
    void nextMountain();                   // Y: the next one (Random: a new seed)
    void pickMountainFromEnv();            // KKE_CLIMB_MOUNTAIN, KKE_CLIMB_SEED
    int mountainPick() const;              // index into m_mountains (size() = Random)
    void setMountainPick(int pick);
    void buildMountain(int lanes);
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
        std::vector<int> look;   // its lobby look (name, colour), as it goes online
        int netId = -1;          // online: its network player
        bool remote = false;     // online: another machine plays it
        bool ghost = false;      // Time trial: the ghost of the best run
    };
    std::vector<Entry> wantedRoster() const;
    void buildRacers(const std::vector<Entry>& roster);
    void removeRacer(Racer& r);
    void applyLooks(const std::vector<Entry>& roster);
    std::vector<Entry> lobbyRoster() const; // the line-up in the menu: wantedRoster + the other screens' players
    Entry remoteEntry(const kke::net::RemotePlayer& p) const;
    int humans() const;
    int faces() const;
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
    void netHit(Racer& r);            // Rockfall: one of ours was hit (kEventHit)
    void netLoose(int lane, int hold, const glm::vec3& push);
    std::string netStatus() const;

    // The character (Body.cpp).
    void loadCharacter();
    void setupBody(Racer& r);
    void animateBody(Racer& r, float dt);
    // A climber on lane `lane`, with the body's proportions measured from
    // the character's skeleton (so every hold it takes is one the arms reach).
    std::unique_ptr<kke::Climber> makeClimber(int lane) const;

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
    kke::AudioModule* m_audio = nullptr;
    // Sounds (synthesised, kke::ImpactSynth: no sound files): a chalky tap
    // for each grab, stone for a breaking hold, a falling climber and
    // bouncing rocks, UI tones for the countdown, the finish and medals.
    void sound(const glm::vec3& at, uint32_t material, float intensity);
    void tone(int earcon, float gain = 0.6f);
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

    std::vector<Mountain> m_mountains;     // in tour order
    Mountain m_mountain;                   // the one being climbed
    std::string m_builtMood;               // the mood last applied
    std::string m_builtKey;                // the mountain the lanes are (id and seed)
    static std::string keyOf(const Mountain& m);
    int m_mountainPick = 0;                // no menu: the pick (m_mountains.size() = Random)
    // The tour (Progress.h): best times, medals, which mountains are open.
    Progress m_progress;
    std::string m_progressPath = "climb_race_progress.json";
    std::vector<int> m_menuMountains;      // the Mountain row's choices: indices into m_mountains (size() = Random)
    int m_forcedMountain = -1;             // KKE_CLIMB_MOUNTAIN picked a closed one: in the row anyway
    std::string m_opened;                  // this race opened that mountain (its name, for the results)
    void loadProgress();
    void saveProgress();
    void refreshMountainRow(int keep);     // the open mountains (after a finish opened one); `keep` stays picked
    void recordFinish(Racer& r);           // a player topped out: best time, medal, next mountain
    std::string recordText(const Mountain& m) const; // "best 0:19.13, gold" ("" = never finished)
    static std::string clockText(float seconds); // "1:05.20"
    uint32_t m_randomSeed = 7;             // Random's seed (KKE_CLIMB_SEED)
    std::vector<std::unique_ptr<Lane>> m_lanes;
    std::vector<Racer> m_racers;
    kke::Camera m_overview; // three players: the fourth quarter's view of the whole race
    std::vector<kke::RigidWorld::BodyId> m_scenery;
    std::unique_ptr<kke::DynamicMeshRenderer> m_ground, m_markers[4];

    // Party modes (Modes.cpp, DESIGN.md "Party modes"): the start menu's
    // Mode row (the host's, online).
    enum class Mode : uint8_t { Race, Rockfall, Elimination, TimeTrial };
    static constexpr int kModes = 4;
    Mode m_mode = Mode::Race;
    Mode chosenMode() const;
    void setupModes();                     // the Mode row, KKE_CLIMB_MODE
    void startMode();                      // a new race: rocks cleared, timers set
    void updateMode(float dt);             // while racing
    void clearRocks();
    void spawnRock(int lane, const glm::vec3& above);
    void eliminate(Racer& r);              // Elimination: out (the host decides online)
    bool isDone(const Racer& r) const { return r.finished || r.out; }
    // Time trial (Modes.cpp): the ghost of the best run on this mountain.
    std::optional<Ghost> m_ghost;          // for m_mountain (none yet: the ghost's face stays empty)
    float m_raceTime = 0.0f;               // s since the start (what the ghost plays back)
    std::filesystem::path ghostFile(const Mountain& m) const;
    void loadGhost();
    void updateGhosts(float dt);
    void recordRuns();                     // the players' runs, a pose a frame
    netrace::Pose poseOf(const Racer& r) const; // world space, as drawn (Net.cpp)
    struct Rock {
        kke::RigidWorld::BodyId body = kke::RigidWorld::kNoBody;
        float age = 0.0f;
        glm::vec3 lastVelocity{0.0f}; // a sudden change is a bounce: a knock of stone
    };
    std::vector<Rock> m_rocks;
    std::unique_ptr<kke::DynamicMeshRenderer> m_rockMesh;
    std::vector<float> m_rockTimers;       // per lane: s to its next rock
    uint32_t m_rockRng = 1;
    float m_elimTimer = 0.0f;              // s to the next elimination
    std::string m_flash;                   // a line in the middle for a moment ("Juno is out!")
    float m_flashTime = 0.0f;

    enum class Phase { Lobby, Countdown, Racing, Finished };
    Phase m_phase = Phase::Lobby;
    float m_countdown = 3.0f;
    float m_best = 0.0f;       // Random: best time on this mountain this session (0 = none yet)
    std::string m_winner;
    bool m_autopilot = false, m_captured = false;
    // How to play: up before the first race (KKE_CLIMB_INTRO=0 skips it,
    // =1 forces it; headless runs skip it) and on the help button. The
    // race waits while it's up.
    bool m_howto = false, m_howtoFirst = true;
    float m_howtoAge = 0.0f; // s since it opened (the press that opened it doesn't close it)
    void showHowTo(bool on);
    float m_botPause = -1.0f; // KKE_CLIMB_BOT_PAUSE (-1: from the difficulty)
    int m_defaultCpus = 1;    // no lobby: KKE_CLIMB_CPUS
    bool m_rosterChanged = false; // someone joined during a race
    float m_quitAfter = -1.0f, m_clock = 0.0f, m_reportAt = 0.0f;
    float m_rockfall = -1.0f; // KKE_CLIMB_ROCKFALL: seconds until it starts (-1 = off)
    void updateRockfall(float dt);
    float m_mouseSensitivity = 0.12f, m_stickSpeed = 200.0f;
    float m_climbCamera = 4.6f; // m behind you on the rock (KKE_CLIMB_CLOSEUP: nearer, to see the hands)

    // Character: the UAL mannequin's bones and clips (retargeting not
    // needed: it's the clips' own skeleton).
    kke::ModelModule::ModelId m_charModel = 0;
    kke::ModelData m_rigData;
    std::unique_ptr<kke::AnimationSet> m_animSet;
    float m_modelYaw = 0.0f;
    kke::TwoBoneChain m_arm[2], m_leg[2];
    kke::HumanArm m_human[2]; // the arms with a person's joint ranges (kke::solveHumanArm)
    kke::FootPlacer m_feet;
    int m_pelvis = -1;
    // Each hand as it is in the rest pose: which way the fingers point and
    // which way the thumb side faces (model space), to turn it onto a hold;
    // the finger bones, to close them around it.
    struct HandRig {
        glm::quat restModel{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 fingers{0.0f, 1.0f, 0.0f}, thumbSide{1.0f, 0.0f, 0.0f};
        float knuckles = 0.09f;     // m from the wrist to the knuckles
        int segment[4][3] = { { -1, -1, -1 }, { -1, -1, -1 }, { -1, -1, -1 }, { -1, -1, -1 } }; // index, middle, ring, pinky
        int thumb[3] = { -1, -1, -1 };
    };
    HandRig m_handRig[2];
    kke::HandRig m_hands[2];                // palms and fingers (kke/Equipment.h): the fingers close on the hold
    kke::BodyShape m_bodyShape;             // the body the arms keep out of (kke/BodyShape.h)
    kke::Climber::Settings m_climbSettings; // proportions from the skeleton
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
    struct ResultHud {
        std::string place, name, result, note, accent;
    };
    struct Hud {
        std::vector<PlayerHud> players;
        std::vector<RivalHud> rivals;
        std::vector<ResultHud> results; // the finish screen, in order
        std::string banner, sub, hint;
        bool racing = false;
        bool howto = false;         // the how-to-play screen is up
    };
    Hud m_hud;
    Rml::DataModelHandle m_hudModel;
    Rml::ElementDocument* m_hudDoc = nullptr;
};

} // namespace climb_race
