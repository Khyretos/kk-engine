#pragma once

#include "kke/Application.h"
#include "kke/AssetCatalog.h"
#include "kke/AudioMixer.h"
#include "kke/EngineSound.h"
#include "kke/Module.h"
#include "kke/RigidWorld.h"
#include "kke/Vehicle.h"
#include "kke/modules/ModelModule.h"

#include "Cars.h"
#include "NetRace.h"
#include "Track.h"

#include <RmlUi/Core/DataModelHandle.h>

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace kke {
class AudioModule;
class DynamicMeshRenderer;
class InputModule;
class LobbyModule;
class NetModule;
class ParticleEffects;
class RigidBodyModule;
namespace net { struct GameEventMsg; }
} // namespace kke
namespace Rml { class ElementDocument; }

namespace racing {

// Racing (games/racing/README.md): up to 24 cars on Jolt's vehicle
// physics (kke::VehicleDesc), three events on three tracks (tracks/*.yaml):
//   Oval   - laps of a banked speedway, NASCAR-style: two abreast, contact,
//            damage you have to drive around, a pit lane that repairs.
//   Drift  - a twisty harbour loop: points for sliding (angle x speed, a
//            combo that grows while you keep it going); a hit loses it.
//   Drag   - a quarter mile side by side: launch on green, change gear
//            yourself at the top of the rev counter.
// Hits dent the body where they land (its mesh is pushed in), bend wheels
// (that corner loses grip and the car pulls) and hurt the engine (less
// power, smoke, then fire); at 0% the car is totalled and out. Tyres
// smoke and leave marks when they slide or spin; metal on metal sparks.
//
// It starts in the lobby (kke::LobbyModule): press A to join (split
// screen, up to four), pick a name, a car, a body kit and a paint job;
// player 1 picks the track, how many cars, laps, the CPU drivers' skill
// and the damage, and can Host or Join online (Climb Race's pattern:
// every machine drives its own cars; the host's CPU cars go to everyone).
//
// Headless / demo switches: KKE_RACE_TRACK=<id> (speedway, harbour_drift,
// quarter_mile), KKE_RACE_CARS=<n>, KKE_RACE_LAPS=<n>, KKE_RACE_DAMAGE=
// 0|1|2 (off, normal, brutal), KKE_RACE_LOBBY=0 (straight into a race),
// KKE_RACE_AUTOPILOT=1 (no lobby, and a CPU drives your car too),
// KKE_RACE_CAMERA=<n> (0 chase, 1 far, 2 bumper, 3 TV), KKE_RACE_QUIT=<s>
// (quit after that long, logging every car every few seconds),
// KKE_RACE_CRASH=1 (every CPU driver aims for the car ahead: a damage test).
class RacingModule : public kke::Module {
public:
    RacingModule();
    ~RacingModule() override;
    const char* name() const override { return "Racing"; }
    std::vector<kke::ModuleDependency> dependencies() const override;
    void init(kke::Application& app) override;
    void fixedUpdate(const kke::FixedUpdateContext& ctx) override;
    void update(const kke::UpdateContext& ctx) override;
    void render(const kke::RenderContext& ctx) override;
    void renderShadow(const kke::ShadowRenderContext& ctx) override;
    void renderTranslucent(const kke::RenderContext& ctx) override;
    void shutdown() override;

    enum class Phase { Lobby, Countdown, Racing, Finished };

private:
    // ---- a car in the race (Driving.cpp)
    struct Car {
        // Who drives it.
        int seat = -1;              // lobby seat of a player here (-1: a CPU driver, or another machine's)
        int player = 0;             // InputModule player (players here)
        bool cpu = false;           // a CPU driver steers it (the host's, offline everyone's)
        bool remote = false;        // another machine drives it (online): posed from what it sends
        int netId = -1;             // its driver's network player id (-1: offline or a CPU car)
        int skill = 1;              // CPU: 0 easy .. 3 expert
        std::string name;
        int type = 0, kit = 0, paint = 0;
        glm::vec3 color{1.0f};
        // Physics: a Jolt vehicle, or a kinematic body for a remote car.
        const CarArt* art = nullptr;
        kke::RigidWorld::VehicleId vehicle = 0;
        kke::RigidWorld::BodyId body = kke::RigidWorld::kNoBody;
        kke::VehicleInput input;
        kke::VehicleState state;
        glm::mat4 xf{1.0f}, prevXf{1.0f}; // this physics step and the one before (drawn in between)
        glm::mat4 wheelLocal[4]{};        // each wheel in the body's space (spin, steer, suspension)
        glm::vec3 velocity{0.0f};
        float wheelSpin = 0.0f;           // remote cars: the wheels' angle
        netrace::CarPose net;             // remote cars: the newest pose
        bool hasNet = false;
        // Drawing.
        kke::ModelModule::InstanceId bodyInst = 0, wheelInst[4] = {};
        std::vector<std::vector<glm::vec3>> dented, dentedNormals; // the body now (car space), once hit
        bool dentsChanged = false;
        // Where it is.
        Track::Where where;
        int lap = 0;                // line crossings since the start (-1 behind it on the grid)
        float progress = 0.0f;      // m from the start: standings
        float lapStart = 0.0f, bestLap = 0.0f, lastLap = 0.0f;
        int place = 0;              // 1 = leading
        bool finished = false;
        float finishTime = 0.0f;
        float wrongWay = 0.0f, upsideDown = 0.0f, offTrack = 0.0f;
        // Damage (Damage.cpp).
        float health = 100.0f;      // 0 = totalled
        float bent[4] = {};         // per wheel: 0 straight .. 1 wrecked
        bool totalled = false;
        float hitCooldown = 0.0f;
        float smokeTimer = 0.0f;
        float wheelSmoke[4] = {};   // s until the next puff
        float scrapeTimer = 0.0f;
        int hits = 0;
        // Pit lane.
        bool wantsPit = false;      // CPU: heading in
        float pitTime = 0.0f;       // s stopped in the pit box
        int pitStops = 0;
        // Drift.
        float driftScore = 0.0f, driftChain = 0.0f, driftCombo = 1.0f, driftHold = 0.0f, driftGrace = 0.0f, driftBest = 0.0f;
        bool drifting = false;
        float driftAngle = 0.0f;
        // Drag.
        int gear = 1;               // manual gearbox (players on the strip)
        bool falseStart = false;
        float reaction = -1.0f, trapSpeed = 0.0f, startS = 0.0f;
        std::string note;           // "Perfect shift", "Crashed: combo lost", ...
        float noteTime = 0.0f;
        // Camera.
        kke::Camera camera;
        glm::vec3 camPos{0.0f}, camLook{0.0f};
        glm::vec3 camDir{0.0f, 0.0f, 1.0f}; // the chase camera's heading, lagging the car's
        bool camInit = false;
        bool lookBack = false;
        // CPU driver.
        float aiLane = 0.0f;        // target u
        float aiLaneTimer = 0.0f;
        float aiStuck = 0.0f, aiReverse = 0.0f;
        float aiWrongWay = 0.0f; // s spent facing back down the track
        float releaseAt = 0.0f;  // drift: when this car's run starts (race clock s), one after another
        float aiPace = 1.0f;        // this driver's corner speed factor
        float aiShiftAt = 0.9f;     // drag: fraction of the rev range it shifts at
        float aiReact = 0.3f;       // drag: s after green it goes
        float aiSlide = 0.0f;       // drift: s left of this handbrake flick
        uint32_t rng = 1;
    };
    float random01(Car& c);
    void buildCar(Car& c, int slot);
    void removeCar(Car& c);
    void resetCarOnTrack(Car& c, float s, float u);
    void readCarState(Car& c);            // after a physics step: state, transforms
    kke::VehicleInput readPlayer(Car& c);
    kke::VehicleInput readCpu(Car& c, float dt);
    void driveCar(Car& c, float dt);      // input -> Jolt (+ damage, pits, drag launch)
    void updateTrackPosition(Car& c, float dt);
    void updateRemoteCar(Car& c, float dt);
    glm::mat4 drawTransform(const Car& c) const;
    void placeInstances(Car& c);          // body + wheel instances for this frame
    void updateCamera(Car& c, float dt, kke::Camera& out);
    kke::Camera& cameraOf(Car& c);
    static glm::vec3 carForward(const Car& c) { return glm::normalize(glm::vec3(c.xf[2])); }
    static glm::vec3 carLeft(const Car& c) { return glm::normalize(glm::vec3(c.xf[0])); }
    static glm::vec3 carUp(const Car& c) { return glm::normalize(glm::vec3(c.xf[1])); }
    static glm::vec3 carPosition(const Car& c) { return glm::vec3(c.xf[3]); }
    static float carSpeed(const Car& c) { return glm::dot(c.velocity, carForward(c)); }
    static bool done(const Car& c) { return c.finished || c.totalled; }
    static float carHalfWidthOf(const Car& c);

    // ---- the race (Race.cpp)
    struct Entry {
        int seat = -1, skill = 1, type = 0, kit = 0, paint = 0;
        std::string name;
        int netId = -1;
        bool remote = false, cpu = false;
    };
    std::vector<Entry> wantedRoster() const;
    void loadTrackList();
    void buildTrack(const TrackDesc& desc);
    void clearTrack();
    void buildScenery();
    void buildRace(const std::vector<Entry>& roster);
    void resetRace();                     // everyone back on the grid, the lights
    void updateRace(float dt);
    void updateStandings();
    void crossLine(Car& c, int direction);
    void finishCar(Car& c);
    int chosenTrack() const;
    int chosenCars() const;
    int chosenLaps() const;
    int chosenSkill() const;
    int chosenDamage() const;
    Event event() const { return m_track ? m_track->desc().event : Event::Oval; }
    int humans() const;
    int lapsOf(Event e) const;
    static std::string clockText(float seconds);
    kke::ModelModule::InstanceId placeProp(const std::string& asset, const glm::vec3& pos, const glm::vec3& forward, float scale = 1.0f);

    // ---- damage and effects (Damage.cpp)
    void handleContacts();
    void hitCar(Car& c, const glm::vec3& point, const glm::vec3& into, float speed, bool byCar);
    void dent(Car& c, const glm::vec3& localPoint, const glm::vec3& localDir, float depth);
    void applyDamage(Car& c);
    void updateEffects(Car& c, float dt);
    void updateScrapes(float dt);
    void updateDrift(Car& c, float dt);
    void updateDebris(float dt);
    void repairCar(Car& c, float amount);
    void spawnDebris(const Car& c, const glm::vec3& point, const glm::vec3& push, int count);
    void addSkid(size_t car, int wheel, const glm::vec3& at, const glm::vec3& up, float darkness);
    void rebuildSkids();
    void clearEffects();
    struct Debris {
        kke::RigidWorld::BodyId body = kke::RigidWorld::kNoBody;
        glm::vec3 half{0.2f};
        int mesh = 0;
        float age = 0.0f;
    };
    std::vector<Debris> m_debris;
    std::vector<std::unique_ptr<kke::DynamicMeshRenderer>> m_debrisMeshes; // a unit cube per colour used
    std::vector<glm::vec3> m_debrisColors;
    int debrisMesh(const glm::vec3& color);

    // ---- engines and tyres (Sound.cpp)
    void updateSounds(float dt);
    void stopSounds();
    struct EngineVoice {
        kke::EngineSound synth;
        std::shared_ptr<kke::AudioStream> stream;
        uint32_t voice = 0; // mixer voice (0: not playing)
        int car = -1;       // index in m_cars
        int type = -1;      // the car type its synth is set up for
        uint32_t seed = 0;
    };
    std::vector<EngineVoice> m_engines;
    float m_engineAssign = 0.0f;
    std::vector<float> m_engineScratch;

    // ---- the lobby (Lobby.cpp)
    void setupLobby();
    void updateLobby(float dt);
    void startFromLobby();
    void backToLobby();

    // ---- online (Net.cpp)
    void setupNet();
    void updateNet(float dt);
    void sendNet();
    void onNetEvent(const kke::net::GameEventMsg& e);
    void applySetup(const netrace::Setup& s);
    void sendSetup();
    void syncNetPlayers();
    std::vector<Entry> onlineRoster() const;
    bool netClient() const;
    bool netHost() const;
    bool netConnected() const;
    void netHit(const Car& c, const glm::vec3& localPoint, const glm::vec3& localDir, float depth);
    void netFinished(const Car& c);
    netrace::CarPose poseOf(const Car& c) const;
    std::string netStatus() const;
    uint32_t m_round = 0, m_netRound = 0, m_sentRound = 0;
    float m_netTime = 0.0f, m_netSearchAt = 0.0f, m_cpuSendAt = 0.0f;
    std::string m_netName, m_lastNetStatus;
    bool m_wasOnline = false, m_netHold = false, m_netApplying = false;
    float m_netHeld = 0.0f;
    std::vector<int> m_netPending;
    int m_netWait = 0;

    // ---- the HUD (Hud.cpp, ui/racing_hud.rml)
    void buildHud();
    void updateHud(float dt);
    void showHowTo(bool on);

    // ---- sound
    void sound(const glm::vec3& at, uint32_t material, float intensity);
    void tone(int earcon, float gain = 0.6f);

    kke::Application* m_app = nullptr;
    kke::RigidBodyModule* m_rigid = nullptr;
    kke::InputModule* m_input = nullptr;
    kke::ModelModule* m_models = nullptr;
    kke::LobbyModule* m_lobby = nullptr;
    kke::NetModule* m_net = nullptr;
    kke::AudioModule* m_audio = nullptr;

    kke::AssetCatalog m_catalog;
    std::unique_ptr<CarGarage> m_garage;
    std::vector<TrackDesc> m_tracks;
    std::unique_ptr<Track> m_track;
    std::string m_builtTrack, m_builtMood;
    std::vector<kke::RigidWorld::BodyId> m_trackBodies;
    std::unique_ptr<kke::DynamicMeshRenderer> m_ground, m_road, m_walls, m_markings, m_skidMesh;
    std::vector<kke::ModelModule::InstanceId> m_props;
    std::vector<std::string> m_propsUsed;
    std::unique_ptr<kke::ParticleEffects> m_fx;
    struct Skid {
        glm::vec3 a{0.0f}, b{0.0f}, up{0.0f, 1.0f, 0.0f};
        float dark = 0.5f;
    };
    std::vector<Skid> m_skids;
    size_t m_skidHead = 0;
    bool m_skidsChanged = false;
    float m_skidRebuild = 0.0f;
    struct SkidTrail {
        glm::vec3 last{0.0f};
        bool on = false;
    };
    std::vector<std::array<SkidTrail, 4>> m_trails; // per car, per wheel

    std::vector<Car> m_cars;
    Phase m_phase = Phase::Lobby;
    float m_countdown = 3.0f, m_raceClock = 0.0f, m_leaderDone = -1.0f, m_finishedFor = 0.0f;
    int m_laps = 5, m_damage = 1;
    int m_cameraMode = 0;
    bool m_autopilot = false, m_crashTest = false, m_rosterChanged = false;
    int m_defaultCars = 12;
    int m_forceTrack = -1, m_forceLaps = -1, m_forceDamage = -1;
    float m_quitAfter = -1.0f, m_clock = 0.0f, m_reportAt = 5.0f;
    kke::Camera m_tvCamera;
    glm::vec3 m_tvSpot{0.0f};
    int m_tvCar = -1;
    std::string m_winner;
    bool m_howto = false, m_howtoFirst = true;
    float m_howtoAge = 0.0f;
    std::vector<int> m_shift, m_resetAsked; // per InputModule player: gear presses and resets since the last physics step

    // HUD model.
    struct PlayerHud {
        std::string name, place, lap, speed, gear, rpm = "0%", rpmColor = "#6fe39a", health = "100%", healthColor = "#6fe39a", status;
        std::string x, y, r, b; // the view's corners: left, top, right, bottom (percent of the screen)
        std::string accent, drift, combo, note, best;
        bool low = false, drifting = false, drag = false, redline = false;
    };
    struct StandingHud {
        std::string place, name, gap, accent;
        bool you = false, out = false;
    };
    struct ResultHud {
        std::string place, name, result, note, accent;
    };
    struct Hud {
        std::vector<PlayerHud> players;
        std::vector<StandingHud> standings;
        std::vector<ResultHud> results;
        std::string banner, sub, hint, notice, lights; // lights: the drag tree ("", "amber1".."amber3", "green", "red")
        bool racing = false, howto = false, drag = false;
    };
    Hud m_hud;
    Rml::DataModelHandle m_hudModel;
    Rml::ElementDocument* m_hudDoc = nullptr;
};

} // namespace racing
