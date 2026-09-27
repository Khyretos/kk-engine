#pragma once

#include "kke/AudioMixer.h"
#include "kke/Module.h"
#include "kke/RigidWorld.h"
#include "kke/modules/ModelModule.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace kke {
class AudioModule;
class OrbitCameraModule;
class RigidBodyModule;
} // namespace kke

namespace kke_audio_demo {

// Every case the audio engine handles, one station each, side by side on a
// long field (docs/AUDIO.md "Audio demo"): open air, a small stone room, a
// great hall, a padded room, walls of different materials, a sound coming
// round through a door, falling crates, footsteps on five grounds, a sound
// circling your head, navigation pings. You stand at the station's
// listening spot; turning the camera turns your ears.
//
// KKE_AUDIO_DEMO_TOUR=1 visits every station in turn and logs what the
// engine measured there (room, how much got through the walls, whether it
// came through a door); KKE_AUDIO_DEMO_EXIT=1 closes the demo after the
// tour. With KKE_AUDIO_RECORD=tour.wav the whole run is saved to listen to.
class AudioDemoModule : public kke::Module {
public:
    const char* name() const override { return "AudioDemo"; }
    std::vector<kke::ModuleDependency> dependencies() const override;
    void init(kke::Application& app) override;
    void update(const kke::UpdateContext& ctx) override;
    void renderUi() override;
    void shutdown() override;

    enum class Kind { Field, StoneRoom, Hall, Padded, Walls, Door, Crates, Footsteps, Circle, Pings, Count };

private:
    using ModelId = kke::ModelModule::ModelId;
    using InstanceId = kke::ModelModule::InstanceId;
    // A sound that repeats at one spot.
    struct Emitter {
        glm::vec3 position{0.0f};
        uint32_t material = 0;
        float period = 1.5f;       // s between hits
        float phase = 0.0f;        // s after the station starts
        float intensity = 0.7f;
        const char* label = "";
    };
    struct Station {
        Kind kind = Kind::Field;
        std::string title, listenFor;
        glm::vec3 ears{0.0f};      // the listening spot
        float cameraDistance = 9.0f, cameraYaw = 0.0f, cameraPitch = -0.55f, tourSeconds = 7.0f;
        std::vector<Emitter> emitters;
    };
    // What the engine did at a station, for the tour log and the panel.
    struct Measured {
        int sounds = 0, throughDoor = 0;
        float minTransmission = 1.0f, peak = 0.0f;
        float rt60 = 0.0f, wet = 0.0f, openness = 0.0f;
        size_t openings = 0, echoes = 0;
        int maxRays = 0;
        std::vector<float> emitterThrough; // per emitter: least that got through
        std::vector<bool> emitterVia;      // per emitter: came through an opening
    };
    struct Crate {
        kke::RigidWorld::BodyId body = kke::RigidWorld::kNoBody;
        InstanceId instance = 0;
        float size = 0.5f, age = 0.0f;
    };

    void buildWorld();
    // A static, visible box of an audio material (walls, floors, strips).
    void box(const glm::vec3& min, const glm::vec3& max, uint32_t material, bool visible = true);
    struct Door { int side; float offset, width, height; }; // side: 0 +z, 1 -z, 2 +x, 3 -x
    void room(const glm::vec3& floorCentre, const glm::vec3& inner, float wall, uint32_t material, std::vector<Door> doors = {},
              bool ceiling = true);
    void marker(const glm::vec3& p, uint32_t material, float size = 0.3f);
    ModelId cubeModel(uint32_t material);

    void enter(int station);
    void tickStation(float dt);
    void measure();
    void logMeasured(const Station& s, const Measured& m) const;
    kke::Listener listener() const;
    void defineInput();
    void readInput();
    void buildPanel();

    kke::Application* m_app = nullptr;
    kke::AudioModule* m_audio = nullptr;
    kke::RigidBodyModule* m_bodies = nullptr;
    kke::ModelModule* m_models = nullptr;
    kke::OrbitCameraModule* m_camera = nullptr;
    std::map<uint32_t, ModelId> m_cubes;
    std::vector<Station> m_stations;
    int m_current = -1;
    float m_time = 0.0f;           // since entering the station
    std::vector<float> m_nextHit;  // per emitter
    std::set<uint32_t> m_seen;                 // voices counted at this station
    std::map<uint32_t, int> m_emitterOf;       // voice id -> emitter
    Measured m_measured;
    std::vector<Crate> m_crates;
    float m_nextCrate = 0.0f;
    int m_crateCount = 0;
    InstanceId m_walker = 0, m_circler = 0, m_earsMarker = 0;
    float m_nextStep = 0.0f, m_nextTick = 0.0f, m_nextPing = 0.0f;
    int m_stepCount = 0;
    kke::SpatialMode m_modeBefore = kke::SpatialMode::Stereo;
    bool m_forcedBinaural = false;
    bool m_tour = false, m_exitAfterTour = false;
    float m_tourLeft = 0.0f;
    int m_uiFrames = 0;
    int m_stationIndex = 0, m_modeIndex = 0; // the panel's view of m_current and the spatial mode
    std::vector<kke::SpatialMode> m_modes;   // the panel's choices of how to hear
};

} // namespace kke_audio_demo
