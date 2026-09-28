#pragma once

#include "kke/Application.h"
#include "kke/Lobby.h"
#include "kke/Module.h"
#include "kke/SphereImpostors.h"
#include "kke/modules/ModelModule.h"

#include "Course.h"
#include "Flight.h"
#include "FlyNet.h"
#include "Stunts.h"

#include <RmlUi/Core/DataModelHandle.h>

#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace kke {
class AudioModule;
class AudioStream;
class InputModule;
class LobbyModule;
class NetModule;
namespace net { struct GameEventMsg; }
} // namespace kke
namespace Rml { class ElementDocument; }

namespace flying {

// The Flying demo (README.md): stunt planes over a generated island.
// Up to four players on one screen (split screen), more online, and up
// to five CPU pilots. Three modes: a Race through a ring course, Stunts
// (loops, rolls, inverted flight and low passes for points against the
// clock) and Free flight (take off from the runway and go where you like).
//
// It starts in the lobby (kke::LobbyModule): every controller, flight
// stick or the keyboard that presses its button joins (the trigger on a
// stick), each player picks a name, a colour and a paint job, player 1
// the mode, the island, laps, rings, the sky and the CPU pilots.
//
// Controllers (README.md "Controllers"): each player flies with the
// device they joined on: a gamepad, a flight stick, or the keyboard and
// mouse. The pause menu (Start, Esc, or a flight stick's fourth button)
// lets a player move to any other device that is plugged in and free,
// never to one another player holds. That's this screen's business
// only: online players don't see or care.
//
// Headless / demo switches (developer builds): KKE_FLY_LOBBY=0 (straight
// into a flight), KKE_FLY_MODE=race|stunts|free, KKE_FLY_ISLAND=<n or
// name>, KKE_FLY_CPUS=<n>, KKE_FLY_AUTOPILOT=1 (player 1 flies by
// autopilot too), KKE_FLY_QUIT=<s> (quit after that long, with a log of
// every plane's progress every few seconds), KKE_FLY_STUNT_TIME=<s>,
// KKE_FLY_WAIT=<n> (online host: start once n players joined),
// KKE_LOBBY_JOIN=<n>.
class FlyingModule : public kke::Module {
public:
    FlyingModule();
    ~FlyingModule() override;
    const char* name() const override { return "Flying"; }
    std::vector<kke::ModuleDependency> dependencies() const override;
    void init(kke::Application& app) override;
    void update(const kke::UpdateContext& ctx) override;
    void render(const kke::RenderContext& ctx) override;
    void renderShadow(const kke::ShadowRenderContext& ctx) override;
    void shutdown() override;

    enum class Mode : uint8_t { Race, Stunts, Free };
    static constexpr int kModes = 3;

private:
    // A plane's smoke: puffs dropped behind it while the smoke is on,
    // each growing and drifting up as it ages.
    struct Trail {
        struct Point {
            glm::vec3 at{0.0f};
            float age = 0.0f;
        };
        std::deque<Point> points;
        glm::vec3 last{0.0f};   // where the last puff went
        bool dropping = false;  // the smoke was on last frame
    };
    // The parts of the plane that move: each with the hinge it turns on.
    struct PartPose {
        kke::ModelModule::InstanceId instance = 0;
    };
    struct Pilot {
        int seat = -1;              // lobby seat (-1: a CPU pilot or another screen's)
        int player = 0;             // InputModule player (people at this screen)
        bool cpu = false;
        int skill = 1;              // CPU pilots: the lobby's difficulty (0..3)
        int slot = 0;               // start position
        std::string name;
        glm::vec3 tint{1.0f};
        int livery = 0;
        PlaneState plane;
        Controls controls;
        RingPilot autopilot;
        bool autopilotOn = false;   // KKE_FLY_AUTOPILOT: a player flown by the CPU pilot
        // Race.
        int nextRing = 0, lap = 0;
        bool finished = false;
        float finishTime = 0.0f;
        float sinceRing = 0.0f;     // s since its last ring (a CPU pilot lost for too long is put back)
        // Stunts.
        StuntTracker stunts;
        std::string trick;          // the last one, shown for a moment
        float trickTime = 0.0f;
        // Crashing: down for a moment, then back in the air where it was.
        float respawnIn = 0.0f;
        int crashes = 0;
        bool smoke = false;
        Trail trail;
        // Camera.
        kke::Camera camera;
        int cameraMode = 0;         // 0 chase, 1 cockpit, 2 far chase
        glm::vec2 look{0.0f};       // degrees off the nose (right stick, hat, mouse with the right button)
        glm::vec3 eye{0.0f};        // the chase camera, smoothed
        glm::vec3 eyeUp{0.0f, 1.0f, 0.0f};
        bool eyeSet = false;
        // Keyboard and mouse: the mouse moves a virtual stick that centres itself.
        glm::vec2 mouseStick{0.0f};
        float stickThrottle = -2.0f; // the flight stick's throttle lever last frame (-2: not read yet)
        // Drawing.
        float propAngle = 0.0f;
        std::vector<kke::ModelModule::InstanceId> parts; // Synty plane: one instance per part (Art order)
        std::unique_ptr<kke::DynamicMeshRenderer> blockPlane; // no Synty plane: a built one in its colour
        // Online.
        int netId = -1;             // network player id (-1: offline)
        bool remote = false;        // another screen flies it (drawn from what it sends)
        net::Plane net;             // remote: the newest state
        glm::quat drawnRotation{1.0f, 0.0f, 0.0f, 0.0f}; // remote: smoothed toward net.rotation
        bool teleported = false;    // ours: respawned this frame (sent once)
    };

    // ---- the world (World.cpp)
    void buildWorld();
    void buildRings();
    Island m_island{ 1 };
    std::vector<Ring> m_rings;
    std::unique_ptr<kke::DynamicMeshRenderer> m_terrain, m_sea, m_ringMesh, m_nextRingMesh, m_debris;
    uint32_t m_builtSeed = 0;
    float m_builtRadius = 0.0f;
    int m_builtRings = 0;

    // ---- the planes (Planes.cpp)
    struct Art {
        bool loaded = false;
        std::vector<kke::ModelModule::ModelId> parts;   // body first, then the moving parts
        enum class Kind : uint8_t { Body, Prop, AileronLeft, AileronRight, Elevator, Rudder, Stick };
        std::vector<Kind> kinds;
        std::vector<glm::vec3> hinges;                   // model space: where each part turns
        std::vector<glm::vec3> axes;                     // model space: what it turns about
        glm::mat4 toPlane{1.0f};                         // model space -> plane space (forward -Z, metres)
        std::vector<std::string> liveries;               // texture paths, one per paint job
        std::string status;                              // shown when the pack isn't there
    };
    Art m_art;
    void loadArt();
    void spawnArt(Pilot& p);
    void removeArt(Pilot& p);
    void poseArt(Pilot& p, float dt);
    void rebuildTrails();
    void updateTrail(Pilot& p, float dt);
    std::unique_ptr<kke::SphereImpostorRenderer> m_smoke; // the puffs, drawn as lit spheres
    std::vector<kke::SphereImpostorRenderer::Sphere> m_puffs;
    std::vector<std::string> m_liveryNames;

    // ---- players, the lobby and the pause menu (Players.cpp)
    void defineActions();
    void setupLobby();
    void updateLobby(float dt);
    void startFromLobby();
    void backToLobby();
    struct Entry {
        int seat = -1, skill = 1, livery = 0, slot = 0;
        std::string name;
        glm::vec3 tint{1.0f};
        int netId = -1;
        bool remote = false;
        bool cpu = false;
    };
    std::vector<Entry> wantedRoster() const;
    void buildPilots(const std::vector<Entry>& roster);
    Controls readPlayer(Pilot& p, float dt);
    Controls readCpu(Pilot& p);
    bool onFlightStick(const Pilot& p) const;
    bool onKeyboard(const Pilot& p) const;
    bool stickPlayer(int player) const;
    bool pressedBy(int player, const std::string& action) const; // + the stick's own button when on a stick
    bool heldBy(int player, const std::string& action) const;
    std::string deviceName(int seat) const;
    // Pause: which seat opened it (their controller runs it), and the
    // devices a seat could move to (and who holds each).
    struct DeviceChoice {
        kke::Lobby::Device kind = kke::Lobby::Device::KeyboardMouse;
        uint32_t ref = 0;
        std::string label;
        int holder = -1;          // seat, or -1 when free
    };
    std::vector<DeviceChoice> deviceChoices() const;
    void openPause(int seat);
    void closePause();
    void updatePause(float dt);
    void pauseAction(int row);
    int m_pauseSeat = -1;         // -1: not paused
    int m_pauseRow = 0;
    int m_pauseDevice = 0;        // index into deviceChoices() the "Controls" row shows
    float m_pauseAge = 0.0f;

    // ---- the flight (FlyingModule.cpp)
    void newFlight();             // the lobby's settings -> island, rings, pilots at the start
    void placeAtStart(Pilot& p);
    void respawn(Pilot& p);
    void updatePilot(Pilot& p, float dt);
    void passRings(Pilot& p, const glm::vec3& from);
    void crash(Pilot& p);
    void updateCamera(Pilot& p, float dt);
    void updateEngineSound(float dt);
    int place(const Pilot& p) const; // 1 = leading
    float progress(const Pilot& p) const;
    bool everyoneDone() const;
    Mode mode() const;
    uint32_t islandSeed() const;
    std::string moodName() const;

    // ---- online (Net.cpp)
    void setupNet();
    void updateNet(float dt);
    void sendNet();
    void onNetEvent(const kke::net::GameEventMsg& e);
    void applySetup(const net::Setup& s);
    void sendSetup();
    void syncNetPlayers();
    std::vector<Entry> onlineRoster() const;
    bool netClient() const;
    bool netHost() const;
    std::string netStatus() const;
    float m_netSearchAt = 0.0f, m_netTime = 0.0f;
    bool m_wasOnline = false;
    std::string m_lastNetStatus;
    uint32_t m_round = 0;         // flights started (the host sends each one)
    bool m_netApplying = false;
    uint32_t m_sentRound = 0;     // the host: the last flight it sent
    std::string m_netName;        // KKE_NET_NAME: player 1's name
    int m_netWait = 0;            // KKE_FLY_WAIT: the host starts once this many others are in

    // ---- the HUD (Hud.cpp, ui/flying_hud.rml)
    void buildHud();
    void updateHud(float dt);

    kke::Application* m_app = nullptr;
    kke::InputModule* m_input = nullptr;
    kke::ModelModule* m_models = nullptr;
    kke::LobbyModule* m_lobby = nullptr;
    kke::NetModule* m_net = nullptr;
    kke::AudioModule* m_audio = nullptr;
    FlightSettings m_flight;
    std::vector<Pilot> m_pilots;
    kke::Camera m_overview;       // three players: the fourth quarter watches the leader

    // The flight now (the lobby's settings, or the host's online).
    Mode m_mode = Mode::Race;
    uint32_t m_seed = 1;
    int m_laps = 2;
    int m_ringCount = 10;
    float m_ringRadius = 14.0f;
    void readSettings();          // the lobby's rows -> the five above
    int score(const Pilot& p) const;
    bool down(const Pilot& p) const; // crashed, waiting to come back

    enum class Phase { Lobby, Countdown, Flying, Results };
    Phase m_phase = Phase::Lobby;
    float m_countdown = 0.0f;
    float m_clock = 0.0f;         // s since the start
    float m_stuntTime = 120.0f;   // Stunts: the length of a round
    float m_quitAfter = -1.0f, m_runTime = 0.0f, m_reportAt = 5.0f;
    bool m_autopilot = false;
    int m_defaultCpus = 3;
    std::string m_flash;          // a line in the middle for a moment
    float m_flashTime = 0.0f;
    std::string m_mood;           // applied now

    // Engine sound: one stream per screen, the local planes' engines mixed.
    std::shared_ptr<kke::AudioStream> m_engine;
    uint32_t m_engineVoice = 0;
    double m_enginePhase[4] = {};

    // HUD model.
    struct PlayerHud {
        std::string name, speed, altitude, throttle, status, big, sub, device, accent, x, y, w, trick, arrow;
        int throttlePct = 0;
        bool stall = false, down = false;
    };
    struct RowHud {
        std::string place, name, what, accent;
        bool you = false;
    };
    struct PauseRow {
        std::string label, value;
        bool focused = false, arrows = false, taken = false;
    };
    struct Hud {
        std::vector<PlayerHud> players;
        std::vector<RowHud> standings;
        std::string banner, sub, hint, flash, clock, artStatus;
        bool flying = false, results = false, paused = false;
        std::string pauseTitle, pauseHint;
        std::vector<PauseRow> pause;
        std::vector<std::string> devices; // the pause menu's list: every device and who holds it
    };
    Hud m_hud;
    Rml::DataModelHandle m_hudModel;
    Rml::ElementDocument* m_hudDoc = nullptr;
};

} // namespace flying
