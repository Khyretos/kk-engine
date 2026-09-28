#pragma once

#include "Ball.h"
#include "Body.h"
#include "Bot.h"
#include "Court.h"
#include "Rules.h"
#include "Shot.h"

#include "kke/Application.h"
#include "kke/Module.h"
#include "kke/RigidWorld.h"

#include <RmlUi/Core/DataModelHandle.h>

#include <memory>
#include <string>
#include <vector>

namespace kke {
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
        bool cpu = true;
        int input = -1;                // InputModule player (humans)
        int level = 1;                 // CPU level
        kke::RigidWorld::CharacterId body = 0;
        glm::vec3 feet{0.0f};          // court space, this frame
        glm::vec3 vel{0.0f};
        glm::vec3 facing{0.0f, 0.0f, -1.0f}; // court space
        std::unique_ptr<Bot> bot;
        std::unique_ptr<Body> look;
        Intent intent;
        // A shot waiting for the ball.
        float armed = -1.0f;           // s since pressed (-1: none)
        ShotKind armedKind = ShotKind::Topspin;
        float charge = 0.0f;           // 0..1
        float ballDist = 99.0f;        // m to the ball at the last hit check
        // The swing on screen.
        SwingPose::Kind swingKind = SwingPose::Kind::Ready;
        float swingT = -2.0f;          // -1..1 while swinging; < -1 idle
        glm::vec3 swingContact{0.0f};  // body frame
        float tossAge = -1.0f;         // serving: s since the toss
        float celebrate = 0.0f;        // s left of a cheer or a groan
        bool cheer = true;
        kke::Camera camera;
        bool cameraInit = false;
        int camSide = 0;               // the half the camera last sat behind
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
    };

private:
    // Setting up (TennisModule.cpp).
    void defineControls();
    void buildWorld();              // Scene.cpp
    void startLocalMatch();         // from the menu or the switches
    void spawnPlayer(const std::string& name, const glm::vec3& tint, int team, bool cpu, int input, int level);
    void clearPlayers();
    Player& player(int index) { return m_players[static_cast<size_t>(index)]; }

    // The match (Play.cpp).
    void stepMatch(Match& m, float dt);
    void startPoint(Match& m);
    void placeForPoint(Match& m);
    void stepPlayer(Match& m, Player& p, float dt);
    void readHuman(Match& m, Player& p);
    void thinkCpu(Match& m, Player& p, float dt);
    bool tryHit(Match& m, Player& p, bool serve);
    void hitBall(Match& m, Player& p, const glm::vec3& contact, bool serve);
    void resolve(Match& m, Rally::Result r);
    int partnerOf(const Match& m, int index) const;
    int nearestOpponent(const Match& m, const Player& p) const;
    int serverIndex(const Match& m) const;
    bool isMyBall(const Match& m, int index) const;

    // The ball test (KKE_TENNIS_BALLTEST=1).
    void ballTest(float dt);

    // Cameras and drawing (Scene.cpp).
    void updateCameras(float dt);
    void updateBodies(float dt);
    void renderCourts(const kke::RenderContext& ctx);

    // The HUD (Hud.cpp, ui/tennis_hud.rml).
    void buildHud();
    void updateHud();

    // The start menu (Lobby.cpp).
    void setupLobby();
    void updateLobby(float dt);

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
    std::string m_ballTexture;

    // The world's look.
    std::unique_ptr<kke::DynamicMeshRenderer> m_courtMesh, m_standMesh, m_fenceMesh;
    std::vector<kke::RigidWorld::BodyId> m_statics;

    // Switches.
    bool m_allBots = false, m_doubles = false, m_inMenu = false;
    int m_level = 1;
    uint32_t m_seed = 1;
    float m_quitAfter = -1.0f, m_clock = 0.0f, m_reportAt = 10.0f;
    bool m_ballTest = false;
    float m_testTime = 0.0f;
    int m_testShot = -1;
    std::unique_ptr<Ball> m_testBall;
    bool m_assist = true;
    int m_length = 0;               // the menu's Length row
    int m_teams = 0;                // the menu's Teams row

    // Tally for the log.
    int m_pointsPlayed = 0, m_longestRally = 0;

    // HUD.
    struct TeamRow { std::string name, sets, points; bool serving = false; };
    struct Hud { TeamRow t[2]; std::string call, sub, hint, banner; };
    Hud m_hud;
    Rml::DataModelHandle m_hudModel;
    Rml::ElementDocument* m_hudDoc = nullptr;
    bool m_broadcastInit = false;   // the TV camera (nobody at this screen plays) has a place
};

} // namespace tennis
