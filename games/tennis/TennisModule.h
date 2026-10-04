#pragma once

#include "Ball.h"
#include "Body.h"
#include "Bot.h"
#include "Court.h"
#include "NetTennis.h"
#include "Rules.h"
#include "Shot.h"
#include "Swing.h"

#include "kke/Application.h"
#include "kke/CameraRig.h"
#include "kke/Mesh.h"
#include "kke/Module.h"
#include "kke/RigidWorld.h"

#include <RmlUi/Core/DataModelHandle.h>

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace kke {
namespace ai {
class NavMesh;
}
class DynamicMeshRenderer;
class InputModule;
class LobbyModule;
class ModelModule;
class NetModule;
class PhysicsModule;
class RigidBodyModule;
} // namespace kke
namespace Rml { class ElementDocument; }

namespace tennis {

// Tennis (README.md): singles or doubles on a court of the sport center,
// against CPU players or friends at the same screen, with a FEMFX rubber
// ball that really squashes on the court and the strings.
//
// Controls: move with the stick (WASD); A / Space topspin, X / J flat,
// B / K slice, Y / L lob. Press before the ball arrives (hold to hit
// harder); the stick at the moment of the hit aims it: left and right,
// deep (up) or short (down). Serving: a shot button tosses, the next one
// hits (at the top of the toss is best).
//
// Headless / demo switches: KKE_TENNIS_BOTS=1 (everyone is a CPU),
// KKE_TENNIS_QUIT=<s> (quit after that long, logging the score),
// KKE_TENNIS_DOUBLES=1, KKE_TENNIS_LEVEL=0..3, KKE_TENNIS_BALLTEST=1
// (fire test shots at the net, the court and the fence and log what the
// FEMFX ball does), KKE_TENNIS_SEED.
class TennisModule : public kke::Module {
public:
    TennisModule();
    ~TennisModule() override;
    const char* name() const override { return "Tennis"; }
    std::vector<kke::ModuleDependency> dependencies() const override;
    void init(kke::Application& app) override;
    void fixedUpdate(const kke::FixedUpdateContext& ctx) override;
    void update(const kke::UpdateContext& ctx) override;
    void render(const kke::RenderContext& ctx) override;
    void renderShadow(const kke::ShadowRenderContext& ctx) override;
    void renderTranslucent(const kke::RenderContext& ctx) override;
    void shutdown() override;

    // A shot a player wants to play (from their controls or the CPU).
    struct Intent {
        glm::vec3 move{0.0f};          // court space, m/s wanted
        bool press = false;            // a shot button went down this frame
        bool held = false;             // ...and is still down (charging)
        ShotKind kind = ShotKind::Topspin;
        glm::vec2 aim{0.0f};           // -1..1: x left/right as the player sees it, y short/deep
    };

    struct Player {
        std::string name;
        glm::vec3 tint{1.0f};
        int team = 0, slot = 0;        // slot: 0 or 1 within a doubles team
        bool cpu = true;               // a CPU brain moves it (the host's CPU players; KKE_TENNIS_AUTOPLAY)
        bool remote = false;           // online: another machine runs it; we draw what it sends
        bool netCpu = false;           // ... a host's CPU player (its moves come in the Cpus event)
        bool hasPose = false;
        int netId = -1;                // online: its network player id
        int walker = -1;               // sport center: the walker who is playing (m_walkers)
        bool alive = false;            // a slot in use (matches come and go in the sport center)
        int input = -1;                // InputModule player (humans)
        int level = 1;                 // CPU level
        kke::RigidWorld::CharacterId body = 0;
        glm::vec3 feet{0.0f};          // court space, this frame
        glm::vec3 vel{0.0f};
        glm::vec3 facing{0.0f, 0.0f, -1.0f}; // court space
        std::unique_ptr<Bot> bot;
        std::unique_ptr<Body> look;
        Intent intent;
        // The swing (Swing.h): pressed, the takeback builds power; let go,
        // the forward swing meets the ball (or doesn't) at contact.
        Stroke stroke = Stroke::Ready;
        bool backhand = false;
        float swingT = kSwingIdle;     // Swing.h's clock: -2..-1 takeback, -1..0 forward, 0..1 follow-through
        glm::vec3 swingContact{0.0f};  // body frame
        ShotKind armedKind = ShotKind::Topspin; // the shot button pressed
        float charge = 0.0f;           // 0..1 power built in the takeback
        // A person's serve: when (p.clock) letting go meets the ball at the
        // top of the hitting spot (the bar full, full power), and how long
        // before that the bar is green (safe: the hit is clean).
        float serveFull = -1.0f, serveGreen = 0.0f;
        float clock = 0.0f;            // s since the swing began
        float contactAt = -1.0f;       // on that clock: when the racket reaches contact (-1: not let go yet)
        float crossAt = -1.0f;         // ... when the ball reached the hitting spot (-1: not yet)
        float prevPlane = 99.0f;       // the ball's distance to the hitting spot last step
        float releaseLead = 0.0f;      // the CPU: its timing error this swing (s)
        bool swingDone = false;        // this swing hit or missed already
        Stamina stamina;
        std::string timingText;        // "Perfect", "Late"... shown a moment after a shot
        float timingShown = 0.0f;      // s left
        float tossAge = -1.0f;         // serving: s since the toss
        float celebrate = 0.0f;        // s left of a cheer or a groan
        bool cheer = true;
        net::Pose pose;                // online, a remote player: what its machine last sent (world)
        kke::Camera camera;
        bool cameraInit = false;
        int camSide = 0;               // the half the camera last sat behind
        // A person's camera: close behind them (kke::CameraRig, third
        // person); the right stick looks round, and it settles back to
        // looking over the net.
        kke::CameraRig rig;
        float idleLook = 10.0f;        // s since the right stick was last used
    };

    // The last few seconds of a match, kept to show a winner or an ace
    // again in slow motion (Replay.cpp).
    struct Replay {
        static constexpr uint32_t kFrames = 300; // 5 s of fixed steps
        struct Who {                   // one player, as Scene.cpp draws them
            glm::vec3 feet{0.0f}, vel{0.0f}; // world
            float yaw = 0.0f;          // degrees
            Stroke stroke = Stroke::Ready;
            bool backhand = false, tossing = false;
            float swingT = kSwingIdle;
            glm::vec3 contact{0.0f};
        };
        struct Frame {
            Ball::State ball;
            std::array<Who, 4> who;
        };
        struct Hit {
            uint32_t tick = 0;
            int hitter = 0;            // index into m_players
            net::Hit hit;
        };
        std::vector<Frame> frames = std::vector<Frame>(kFrames); // frames[tick % kFrames]
        uint32_t tick = 0;             // fixed steps recorded
        std::vector<Hit> hits;         // the latest ones
        uint32_t bounceTick = 0;       // the first bounce after the last hit
        // Playing it: ticks from..to, now at `at`.
        bool pending = false, playing = false;
        uint32_t from = 0, to = 0, at = 0;
        float part = 0.0f;             // how far from frame `at` to the next (a slow replay steps between frames)
        std::string sub;               // the HUD's line, put back afterwards
        glm::vec3 camPos{0.0f}, camLook{0.0f};
        bool camInit = false;
        const Frame& frame(uint32_t t) const { return frames[t % kFrames]; }
    };

    struct Match {
        int court = 0;
        MatchRules rules;
        Score score;
        Rally rally;
        std::unique_ptr<Ball> ball;
        std::vector<int> players;      // indices into m_players
        enum class Phase { Warmup, Serve, Rally, PointOver, MatchOver } phase = Phase::Warmup;
        float phaseTime = 0.0f;
        int lastPointTo = -1;
        std::string call, sub;         // the umpire: "Out", "15-30"
        int rallyShots = 0;
        bool serveAgain = false;       // after a fault or a let: the same point, served again
        float deadBall = 0.0f;         // s the ball has lain still in a rally
        float sinceHit = 0.0f;         // s since the last hit (a ball nobody can reach ends the rally)
        float sinceBounce = -1.0f;     // s since it bounced (-1: not since the last hit)
        uint16_t serial = 0;           // online: +1 each serve, so a late hit for an old point is dropped
        uint32_t netId = 0;            // online: the host's number for it (0: not sent yet)
        std::vector<uint8_t> history;  // who won each point (a late joiner replays the score)
        float ballSentAt = 0.0f;       // host: when its ball last went out
        int lastHitter = -1;           // index into m_players
        glm::vec3 landingAt{0.0f};     // where the last shot comes down (Marks.cpp: the ring)
        int landingShot = -1;          // ... for which shot of the rally (rallyShots)
        std::unique_ptr<Replay> replay; // winners and aces again in slow motion (offline, one match)
    };

    // Who plays in a match about to start (the menu's seats, CPU players,
    // online players).
    struct Entry {
        std::string name;
        glm::vec3 tint{1.0f};
        kke::Outfit outfit;            // what they wear (the menu's look)
        bool dressed = false;          // ... or the tinted mannequin
        int person = 0;                // the menu's Body: a Synty person, or 0
        bool cpu = false;
        int input = -1, level = 1;
        int netId = -1;
        int walker = -1;
        bool remote = false;
        bool netCpu = false;           // online, a client: one of the host's CPU players
        int team = -1, slot = -1;      // -1: the match decides
    };

    // Someone in the sport center out of a match: a person at this screen
    // walking about, or one of the CPU crowd going from court to court to
    // watch (Center.cpp).
    struct Walker {
        std::string name;
        glm::vec3 tint{1.0f};
        kke::Outfit outfit;            // a person's clothes (the crowd: tinted)
        bool dressed = false;
        int person = 0;                // the menu's Body
        bool cpu = true;               // the crowd
        int input = -1;                // a person at this screen
        bool remote = false;           // online: someone at another screen (moved by what it sends)
        int netId = -1;                // online: their network player id
        int localSlot = -1;            // a person at this screen: their NetModule local player slot
        bool gone = false;             // left the game (the slot stays, so indices hold)
        kke::RigidWorld::CharacterId body = 0;
        std::unique_ptr<Body> look;
        glm::vec3 facing{0.0f, 0.0f, 1.0f}; // world
        int playing = -1;              // the Player while in a match
        int queued = -1;               // the court they wait to play on
        // The crowd: where they go and what they watch.
        enum class Doing { Wander, ToSeat, Watch } doing = Doing::Wander;
        glm::vec3 goal{0.0f};
        glm::vec3 exit{0.0f};          // out of the gap beside a court first, after watching there
        bool exiting = false;
        int court = -1, seat = -1;
        float timer = 0.0f;
        bool sitting = false;
        float cheer = 0.0f;            // s left of a cheer (a point ended on the court they watch)
        bool happy = true;
        uint32_t dice = 1;
        std::vector<glm::vec3> path;   // the way to pathGoal (Walk.cpp)
        size_t pathAt = 0;
        glm::vec3 pathGoal{1e9f};
        float repath = 0.0f;
        // A person's view.
        kke::Camera camera;
        bool cameraInit = false;
        float camYaw = 0.0f;           // degrees, the way the camera looks
        glm::vec2 stick{0.0f};
        bool play = false, cpuNow = false, leave = false; // presses, kept for the fixed step
    };

private:
    // Setting up (TennisModule.cpp).
    void defineControls();
    void buildWorld();              // Scene.cpp
    // The crowd's way about (Walk.cpp).
    void buildNavMesh();
    glm::vec3 wayTo(Walker& w, const glm::vec3& feet, const glm::vec3& goal, float arrive, float dt);
    struct NavBox { glm::vec3 centre, half; glm::quat rotation; };
    std::vector<NavBox> m_navBoxes;   // buildWorld's solids, world space
    std::unique_ptr<kke::ai::NavMesh> m_nav;
    // The cloth nets (NetCloth.cpp).
    void buildNets();
    void updateNets(float dt);
    void startLocalMatch();         // from the menu or the switches (and the host's online match)
    Match* buildMatch(std::vector<Entry> entries, const MatchRules& rules, int court);
    int spawnPlayer(const Entry& e, int team);
    void freePlayer(int index);
    MatchRules menuRules(int teamSize) const;
    void backToMenu();
    void leaveToMenu();                 // the menu button: back to the start menu (a client leaves online)
    bool leaveCenterMatch(int input);   // sport center: walk off the court (a walkover)
    bool inCenterMatch(int input) const;
    void clearPlayers();
    Player& player(int index) { return m_players[static_cast<size_t>(index)]; }

    // The match (Play.cpp).
    void stepMatch(Match& m, float dt);
    void startPoint(Match& m);
    void placeForPoint(Match& m);
    void stepPlayer(Match& m, Player& p, float dt);
    void stepRemote(Match& m, Player& p, float dt); // online: a player another machine runs
    void readHuman(Match& m, Player& p);
    void thinkCpu(Match& m, Player& p, float dt);
    // The swing (Swing.h): start it, how far the ball is from the hitting
    // spot, when it gets there, and the hit.
    void startSwing(Match& m, Player& p, ShotKind kind, bool serve);
    void stepSwing(Match& m, Player& p, bool serving, bool release, float dt);
    void endSwing(Player& p);
    float planeGap(const Match& m, const Player& p) const;
    float timeToSpot(const Match& m, const Player& p, glm::vec3* at = nullptr, float* bounceAge = nullptr) const;
    glm::vec3 bodyRel(const Match& m, const Player& p, const glm::vec3& at) const;
    glm::vec3 tossHand(const Match& m, const Player& p) const;
    void hitBall(Match& m, Player& p, const glm::vec3& contact, bool serve, float timingError);
    void resolve(Match& m, Rally::Result r, const std::string& call = {});
    void applyHit(Match& m, int hitter, const net::Hit& h);
    bool authority() const;         // offline or the host: this machine is the umpire
    int partnerOf(const Match& m, int index) const;
    int nearestOpponent(const Match& m, const Player& p) const;
    int serverIndex(const Match& m) const;
    bool isMyBall(const Match& m, int index) const;

    // The sport center (Center.cpp): walk about, pick a court, watch.
    void enterCenter();
    void spawnWalker(const std::string& name, const glm::vec3& tint, bool cpu, int input, const glm::vec3& at, const kke::Outfit* outfit = nullptr,
                     int person = 0);
    void readWalker(Walker& w);
    void stepCenter(float dt);
    void stepCrowd(Walker& w, float dt);
    void startCourt(int court);
    void startCpuMatch(int court);
    void endCenterMatch(size_t matchIndex, int forfeitTeam);
    void closeMatch(size_t matchIndex);
    void walkersOn(const Match& m);         // its people walk off at the gate, it's gone
    void gateJoin(int walker, int court);
    void gateLeave(int walker);
    int gateNear(const glm::vec3& world) const; // the court whose gate is here, or -1
    glm::vec3 gatePoint(int court) const;
    Match* matchOn(int court);
    const Match* focusMatch() const;            // the one the HUD shows
    void updateWalkerCameras(float dt, std::vector<kke::Camera*>& cams);
    std::string centerHint() const;
    void onPointForCrowd(const Match& m);
    void updateWalkerBodies(float dt);

    // Replays (Replay.cpp): the last seconds kept, a winner or an ace
    // shown again in slow motion from beside the ball.
    void recordReplay(Match& m);
    void queueReplay(Match& m, int winningTeam);
    void startReplay(Match& m);
    void stepReplay(Match& m, float dt);
    void endReplay(Match& m);
    bool replayCamera(Match& m, float realDt, kke::Camera& cam);
    bool replaysOn(const Match& m) const;
    // The busiest case (Bench.cpp, KKE_TENNIS_BENCH).
    void setupBench();
    void seatBenchCrowd();
    void benchCamera(float dt, kke::Camera& cam);

    // The ball test (KKE_TENNIS_BALLTEST=1).
    void ballTest(float dt);

    // Cameras and drawing (Scene.cpp).
    void updateCameras(float dt);
    void updateBodies(float dt);
    void renderCourts(const kke::RenderContext& ctx);

    // The HUD (Hud.cpp, ui/tennis_hud.rml).
    void buildHud();
    void updateHud();
    // The ball's shadow and the landing ring (Marks.cpp).
    void updateMarks();

    // The start menu (Lobby.cpp).
    void setupLobby();
    void updateLobby(float dt);
    void startFromMenu();

    // Online (Net.cpp).
    bool netHost() const;
    bool netClient() const;
    bool online() const { return netHost() || netClient(); }
    void setupNet();
    void syncNetPlayers();
    void updateNet(float dt);
    void sendNet();
    void onNetEvent(const kke::net::GameEventMsg& e);
    void sendSetup(Match& m);               // host: gives it a network id if it has none
    net::Setup setupOf(const Match& m) const;
    void closeMatchByNet(uint32_t id, const std::string& why);
    void applySetup(const net::Setup& s);
    std::string netStatus() const;
    bool waitsOnline() const;       // KKE_NET join, or a host waiting for KKE_TENNIS_WAIT players
    std::vector<Entry> seatEntries() const; // this screen's players, as the menu has them
    // Clothes (docs/OUTFITS.md): a menu look (name, colour, skin, trousers,
    // shoes), another screen's (its NetModule character), and a CPU
    // player's, the same on every screen (from its name).
    kke::Outfit outfitOf(const std::vector<int>& look, const glm::vec3& tint) const;
    kke::Outfit outfitOfCharacter(const std::string& character, const glm::vec3& tint) const;
    static kke::Outfit cpuOutfit(const std::string& name, const glm::vec3& tint);
    int personOf(const std::vector<int>& look) const; // the Body row (0: the mannequin)
    int personOfCharacter(const std::string& character) const;
    std::vector<Entry> netEntries() const;  // ... as network players (slot order), with netId
    Match* matchByNet(uint32_t id);
    size_t matchIndex(const Match& m) const;
    int indexInMatch(const Match& m, const Player& p) const;
    void sendBoard();
    void syncRemoteWalkers();
    void enterCenterOnline();                   // a client: the host is in the sport center

    kke::Application* m_app = nullptr;
    kke::RigidBodyModule* m_rigid = nullptr;
    kke::PhysicsModule* m_physics = nullptr;
    kke::InputModule* m_input = nullptr;
    kke::ModelModule* m_models = nullptr;
    kke::LobbyModule* m_lobby = nullptr;
    kke::NetModule* m_net = nullptr;

    SportCenter m_center;
    std::unique_ptr<Rig> m_rig;
    std::vector<Player> m_players;
    std::vector<std::unique_ptr<Match>> m_matches;

    // The world's look.
    std::unique_ptr<kke::DynamicMeshRenderer> m_courtMesh, m_standMesh, m_fenceMesh;
    std::unique_ptr<kke::DynamicMeshRenderer> m_shadowMesh, m_markMesh, m_ballMesh; // Marks.cpp, rebuilt every frame
    struct CourtNet {
        kke::RigidWorld::ClothId cloth = 0;
        kke::RigidWorld::BodyId ball = 0;  // the court's ball, as only cloth feels it
        std::vector<uint32_t> lines;       // the threads (kke::clothNet)
        std::vector<glm::vec3> pos;
    };
    std::vector<CourtNet> m_nets;          // NetCloth.cpp, one per court
    std::unique_ptr<kke::DynamicMeshRenderer> m_netMesh;
    std::vector<kke::Vertex> m_netVerts;
    std::vector<uint32_t> m_netIdx;
    std::vector<kke::Vertex> m_shadowVerts, m_markVerts, m_ballVerts;
    std::vector<uint32_t> m_shadowIdx, m_markIdx, m_ballIdx;
    std::vector<kke::RigidWorld::BodyId> m_statics;

    // Switches.
    bool m_allBots = false, m_doubles = false, m_inMenu = false;
    int m_level = 1;
    uint32_t m_seed = 1;
    std::string m_poseTest;         // KKE_TENNIS_POSE: a stroke held still (Scene.cpp)
    bool m_stringTest = false;      // KKE_TENNIS_STRINGTEST: log each racket's pocket (Strings.h)
    int m_closeUp = -1;             // KKE_TENNIS_CLOSEUP=<n>: the camera side on to player n of the first match
    float m_closeUpDistance = 3.8f; // KKE_TENNIS_CLOSEUP_DISTANCE=<m>: how far away (for looking at hands)
    bool m_swingLog = false;        // KKE_TENNIS_SWINGLOG=1: every swing in the log (stroke, timing, power, stamina)
    float m_quitAfter = -1.0f, m_clock = 0.0f, m_reportAt = 10.0f;
    bool m_ballTest = false;
    float m_serveAt = -1.0f;       // KKE_TENNIS_SERVEAT (Play.cpp)
    float m_testTime = 0.0f;
    int m_testShot = -1;
    std::unique_ptr<Ball> m_testBall;
    std::unique_ptr<StringBed> m_testBed; // KKE_TENNIS_STRINGTEST
    float m_testBedTime = 0.0f;
    void stringTest(float dt);
    float m_timingScale = 1.0f;     // the timing window: the menu's Swing timing (Relaxed 1.5, Normal 1, Pro 0.7)
    bool m_autoTiming = false;      // ... Automatic: the swing goes by itself at the right moment
    bool m_replays = true;          // the menu's Replays row / KKE_TENNIS_REPLAYS=0
    void setTiming(int choice) {
        m_timingScale = choice == 0 ? 1.5f : choice == 2 ? 0.7f : 1.0f;
        m_autoTiming = choice == 3;
    }
    bool m_assist = true;
    int m_length = 0;               // the menu's Length row
    int m_teams = 0;                // the menu's Teams row
    bool m_autoplay = false;        // KKE_TENNIS_AUTOPLAY: this screen's players have CPU brains (tests)

    // The sport center.
    int m_where = 0;                // the menu's Play row: 0 one match, 1 the sport center
    bool m_inCenter = false;
    int m_crowd = 40;               // the menu's Crowd row (CPU people walking and watching)
    bool m_cpuMatches = true;       // CPU players take the free courts
    std::vector<Walker> m_walkers;
    struct Gate { std::vector<int> waiting; float countdown = -1.0f; };
    std::array<Gate, SportCenter::kCourts> m_gates;
    std::array<std::vector<int>, SportCenter::kCourts> m_seatTaken; // walker per seat, -1 free
    std::array<float, SportCenter::kCourts> m_courtRest{}; // s each court has been empty
    std::vector<std::pair<std::string, int>> m_wins; // matches won here, best first
    float m_overviewYaw = 0.0f;
    int m_bench = 0;                // KKE_TENNIS_BENCH: 1 = ten singles matches, 2 = ten doubles (Bench.cpp)
    float m_benchClock = 0.0f;      // s the flying camera has been touring

    // Online.
    uint32_t m_netMatch = 0;        // the host's match number
    float m_netTime = 0.0f, m_netSearchAt = 0.0f, m_cpusSentAt = 0.0f, m_boardSentAt = 0.0f;
    bool m_boardDirty = false;
    std::vector<uint8_t> m_newcomers;           // host: players who joined, to be sent every match
    net::Board m_board;                         // a client: the host's gates and wins
    bool m_wasOnline = false;
    int m_netWait = 0;              // KKE_TENNIS_WAIT: the host starts once this many others are in
    std::string m_lastNetStatus;

    // Tally for the log.
    int m_pointsPlayed = 0, m_longestRally = 0;

    // HUD.
    struct TeamRow { std::string name, sets, points; bool serving = false; };
    // A person at this screen, in a match: their legs and their swing.
    struct MeterRow {
        std::string name, timing;
        float stamina = 1.0f, power = 0.0f;
        bool charging = false;
        // The serve's bar: where green starts (0..1 along it), and where
        // the fill is: 0 before green, 1 in it, 2 past full (late).
        bool serve = false;
        float green = 0.0f;
        int zone = 0;
        bool operator==(const MeterRow&) const = default;
    };
    struct Hud { TeamRow t[2]; std::string call, sub, hint, banner, ranking; std::vector<MeterRow> meters; };
    Hud m_hud;
    Rml::DataModelHandle m_hudModel;
    Rml::ElementDocument* m_hudDoc = nullptr;
    bool m_broadcastInit = false;   // the TV camera (nobody at this screen plays) has a place
};

} // namespace tennis
