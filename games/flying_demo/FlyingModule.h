#pragma once

#include "kke/Application.h"
#include "kke/Lobby.h"
#include "kke/Module.h"
#include "kke/SphereImpostors.h"
#include "kke/modules/ModelModule.h"

#include "Combat.h"
#include "Course.h"
#include "Flight.h"
#include "FlyNet.h"
#include "Stunts.h"

#include <RmlUi/Core/DataModelHandle.h>

#include <memory>
#include <string>
#include <vector>

namespace kke {
class AudioModule;
class AudioStream;
class InputModule;
class LobbyModule;
class NetModule;
class ParticleEffects;
namespace net { struct GameEventMsg; }
} // namespace kke
namespace Rml { class ElementDocument; }

namespace flying {

// The Flying demo (README.md): stunt planes over a generated island, a
// desert canyon or a mega city (Course.h Map).
// Up to four players on one screen (split screen), more online, and up
// to five CPU pilots. Four modes: a Race through a ring course, Stunts
// (loops, rolls, inverted flight and low passes for points against the
// clock), Free flight (go where you like) and a Dogfight over a town
// (shoot the others down). Every flight starts on the runway: full
// throttle, and at take-off speed the plane lifts off by itself.
// Planes collide with each other and the buildings: a bump dents them,
// a hard one (or the ground) and they explode (README.md "Collisions
// and damage").
//
// It starts in the lobby (kke::LobbyModule): every controller, flight
// stick or the keyboard that presses its button joins (the trigger on a
// stick), each player picks a name, a colour and a paint job, player 1
// the mode, the island, laps, rings, the sky and the CPU pilots.
//
// Controllers (README.md "Controllers"): each player flies with the
// device they joined on: a gamepad, a flight stick, or the keyboard and
// mouse. The pause menu (Start, Back, Esc, or a flight stick's fourth button)
// lets a player move to any other device that is plugged in and free,
// never to one another player holds. That's this screen's business
// only: online players don't see or care.
//
// Headless / demo switches (developer builds): KKE_FLY_LOBBY=0 (straight
// into a flight), KKE_FLY_MODE=race|stunts|free|dogfight, KKE_FLY_ISLAND=<n or
// name>, KKE_FLY_CPUS=<n>, KKE_FLY_AUTOPILOT=1 (player 1 flies by
// autopilot too), KKE_FLY_QUIT=<s> (quit after that long, with a log of
// every plane's progress every few seconds), KKE_FLY_STUNT_TIME=<s>,
// KKE_FLY_WAIT=<n> (online host: start once n players joined),
// KKE_FLY_CAMERA=chase|cockpit|far, KKE_FLY_BENCH=pileup (every plane flies head-on into the others at
// full speed, again every 7 s: a benchmark for explosions and dents),
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
    void renderTranslucent(const kke::RenderContext& ctx) override;
    void shutdown() override;

    enum class Mode : uint8_t { Race, Stunts, Free, Dogfight };
    static constexpr int kModes = 4;

private:
    // A plane's smoke: puffs dropped behind it while the smoke is on,
    // each growing and drifting up as it ages.
    struct Trail {              // the puffs themselves live in m_fx
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
        // Damage (Damage.cpp): bumps and bullets take health; at 0 it
        // goes down. Dents stay until it comes back as a new plane.
        float health = 100.0f;
        bool inLane = false;        // a CPU pilot: down in the Canyon's gorge or a Mega City avenue, following it
        bool safe = false;          // on the runway (and the first few metres up): bullets and planes pass through it
        float bumpCooldown = 0.0f;  // s: one bump at a time
        int lastBy = -1;            // the pilot (index) that last hurt it, for who gets the kill
        float lastByAt = -100.0f;   // m_clock then
        int downCause = 0;          // why it went down: 0 crashed, 1 shot down, 2 a collision
        int kills = 0, deaths = 0;  // Dogfight
        glm::vec3 previous{0.0f};   // where it was last frame (collisions between frames)
        bool wasDown = false;       // remote: it was down last frame (its explosion once)
        float smokeTimer = 0.0f;    // a hurt plane's smoke
        std::vector<std::vector<std::vector<glm::vec3>>> dented, dentedNormals; // Synty: [part][mesh] model space
        std::vector<kke::Vertex> blockBase, blockDented; // built plane: its vertices as made, and dented
        std::vector<uint32_t> blockIndices;
        bool dentsChanged = false;
        // Guns (Dogfight).
        bool firing = false;
        float gunCooldown = 0.0f;
        float hitMark = 0.0f;       // s the sight shows a hit
        int target = -1;            // CPU: who it's after
        float targetFor = 0.0f;     // CPU: s on this target
        bool climbOut = false;      // CPU: just off the runway, climbing straight out
        // Online: damage to a plane of another screen, gathered and sent a few times a second.
        float owed = 0.0f, owedAt = 0.0f;
        int owedBy = -1;
        glm::vec3 owedPoint{0.0f}, owedDirection{0.0f};
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
        bool netSeen = false;       // remote: its screen has sent where it is
        net::Plane net;             // remote: the newest state
        glm::quat drawnRotation{1.0f, 0.0f, 0.0f, 0.0f}; // remote: smoothed toward net.rotation
        bool teleported = false;    // ours: respawned this frame (sent once)
    };

    // ---- the world (World.cpp)
    void buildWorld();
    void buildRings();
    void buildTown();             // the airfield's buildings, and the town in a dogfight
    void loadTownArt();
    Town m_town;
    std::unique_ptr<kke::DynamicMeshRenderer> m_townMesh;
    std::vector<kke::ModelModule::ModelId> m_houseModels; // Synty POLYGON Town houses and shops
    std::vector<glm::vec3> m_houseSizes, m_houseOffsets;  // their footprints (m) and where their bottom middle is (model space, m)
    std::vector<float> m_houseScales;
    std::vector<kke::ModelModule::InstanceId> m_houses;
    uint32_t m_townSeed = 0;
    bool m_townDistrict = false;
    size_t m_townRings = 0;       // the course the town was laid round (the city's avenues)
    bool m_townBuilt = false;
    bool m_townArtTried = false;
    Island m_island{ 1 };
    std::vector<Ring> m_rings;
    std::unique_ptr<kke::DynamicMeshRenderer> m_terrain, m_sea, m_ringMesh, m_nextRingMesh;
    uint32_t m_builtSeed = 0;     // the island the terrain mesh was built for
    uint32_t m_ringSeed = 0;      // ... and the rings
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
        glm::vec3 eye{0.0f, 1.05f, 0.9f};                // plane space: the cockpit camera (the built plane's seat)
        std::vector<std::string> liveries;               // texture paths, one per paint job
        std::string status;                              // shown when the pack isn't there
        // Each part as made (model space), for dents: [part][mesh].
        std::vector<std::vector<std::vector<glm::vec3>>> positions, normals;
        std::vector<std::vector<std::vector<uint32_t>>> indices;
        float scale = 1.0f;                              // model units -> metres
    };
    Art m_art;
    void loadArt();
    void spawnArt(Pilot& p);
    void removeArt(Pilot& p);
    void poseArt(Pilot& p, float dt);
    void rebuildTrails();
    void updateTrail(Pilot& p, float dt);
    std::unique_ptr<kke::SphereImpostorRenderer> m_smoke; // the fireballs, drawn as glowing spheres
    std::vector<kke::SphereImpostorRenderer::Sphere> m_puffs;
    uint32_t m_puffSeed = 0x2545F491u; // each smoke puff's shape
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
        std::vector<int> look; // its lobby look (name, colour, paint), as it goes online
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
    void respawn(Pilot& p, bool crashed = false);
    bool safe(const Pilot& p) const { return p.remote ? p.net.safe : p.safe; }
    void placePileup(Pilot& p);   // KKE_FLY_BENCH=pileup: on the circle, full speed, nose to the middle
    void updatePileup(float dt);
    void updatePilot(Pilot& p, float dt);
    void passRings(Pilot& p, const glm::vec3& from);
    void crash(Pilot& p);

    // ---- damage, explosions and guns (Damage.cpp)
    void collide(float dt);                   // every plane here against the others and the buildings
    void bump(Pilot& a, Pilot* b, const glm::vec3& point, const glm::vec3& normal, float speed, int by);
    void hurt(Pilot& p, float amount, int by, const glm::vec3& worldPoint, const glm::vec3& worldDirection, int cause);
    void dent(Pilot& p, const glm::vec3& planePoint, const glm::vec3& planeDirection, float depth, bool tell = true);
    void clearDents(Pilot& p);
    void applyDents(Pilot& p);
    void explode(const glm::vec3& at, const glm::vec3& velocity, const glm::vec3& tint);
    void warmUpExplosions();      // the pieces' meshes and the bangs made before the first explosion needs them
    void wentDown(Pilot& p);                  // deaths, kills, the feed
    void fireGuns(Pilot& p, float dt);
    void updateBullets(float dt);
    void updateEffects(float dt);
    int pilotOfNet(int netId) const;
    bool present(const Pilot& p) const;
    int netOf(int pilot) const;
    void sendOwed(float dt);
    void onDamage(const net::Damage& d);
    void onDent(const net::Dent& d);
    void onDown(const net::Down& d);
    void addKill(int killer, int victim, int cause);
    Controls readCpuDogfight(Pilot& p, const Controls& cruise);
    glm::mat4 planeToWorld(const Pilot& p) const;
    std::unique_ptr<kke::ParticleEffects> m_fx;
    PlaneShape m_shape = planeShape(); // for hits: the built plane's, or Synty's (loadArt)
    struct Bullet {
        glm::vec3 position{0.0f}, velocity{0.0f};
        float life = 0.0f;
        int owner = -1;           // pilot index
        bool live = true;         // false: another screen's tracer (drawn, hits nothing)
    };
    std::vector<Bullet> m_bullets;
    struct Fireball {
        glm::vec3 position{0.0f}, velocity{0.0f};
        float age = 0.0f, size = 1.0f;
    };
    std::vector<Fireball> m_fireballs;
    struct Chunk {
        glm::vec3 position{0.0f}, velocity{0.0f}, axis{0.0f, 1.0f, 0.0f};
        float angle = 0.0f, spin = 0.0f, size = 1.0f, age = 0.0f;
        bool resting = false;
        int mesh = 0;             // 0 dark, 1 the plane's colour (m_chunkMeshes)
    };
    std::vector<Chunk> m_chunks;
    std::vector<std::unique_ptr<kke::DynamicMeshRenderer>> m_chunkMeshes;
    std::vector<glm::vec3> m_chunkColours;
    int chunkMesh(const glm::vec3& colour);
    uint32_t m_rng = 0x2545f491u;
    float random01();
    float m_gunPhase = 0.0f, m_gunEnvelope = 0.0f; // the guns' rattle in the engine stream
    bool m_gunsHere = false;
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
    bool m_pileup = false;        // KKE_FLY_BENCH=pileup
    int m_startCamera = 0;        // KKE_FLY_CAMERA=chase|cockpit|far: the camera every flight starts with
    float m_pileupIn = 0.0f;      // s to the next pile-up
    int m_pileups = 0;

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
    mutable uint32_t m_randomSeed = 0; // the Random island's seed, rolled on first use
    int m_laps = 2;
    int m_killsToWin = 10;        // Dogfight
    float m_dogfightTime = 300.0f; // Dogfight: s, unless someone gets the kills first
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
    struct TagHud { // another plane in this player's view: its name and, in a dogfight, its health
        std::string name, accent, health, x, y;
        bool hurt = false, bar = false;
        bool operator==(const TagHud&) const = default;
    };
    struct PlayerHud {
        std::string name, speed, altitude, throttle, status, big, sub, device, accent, x, y, w, trick, arrow, health, sightX, sightY;
        int throttlePct = 0;
        bool stall = false, down = false, hurt = false, sight = false, hit = false;
        std::vector<TagHud> tags;
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
