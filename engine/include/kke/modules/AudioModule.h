#pragma once

#include "kke/AudioMixer.h"
#include "kke/ImpactSynth.h"
#include "kke/Module.h"
#include "kke/RoomAcoustics.h"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

namespace kke {

// The audio engine as a module (ACTION_PLAN.md 2.2, docs/AUDIO.md).
//
// - Output: miniaudio (public domain / MIT-0) opens the platform's device
//   and pulls blocks from kke::AudioMixer on its own thread. No device (CI,
//   servers, a VM with no sound card): the module keeps running "silent",
//   still mixing on the game thread, so the sound visualizer and every
//   test behave the same with or without speakers.
// - Physics: every Jolt contact of the frame (RigidBodyModule::
//   frameContacts) above a speed threshold becomes an impact of both
//   materials, synthesized (kke::ImpactSynth), with a per-pair cooldown
//   and a per-frame cap so a collapsing pile can't flood the mixer.
//   FEMFX objects hitting each other or the floor
//   (PhysicsModule::frameImpacts) sound the same way. FEMFX breaks
//   (PhysicsModule::frameBreaks) play as a crack of the
//   object's material (Material::audioMaterial, or guessed).
// - Occlusion: one ray per playing sound from the listener, re-cast every
//   0.1 s; a wall in between lets through its material's `transmission`
//   and muffles the highs. Replaceable: set occlusionQuery.
// - Room: a few dozen rays around the listener every 0.25 s
//   (kke::probeRoom) set the reverb: a stone hall rings, a padded room
//   is dry, a field has none. An occluded sound that has a way around the
//   wall (an opening the probe found) is heard from the opening.
// - Footsteps (playFootstep, fed by kke::CharacterFootsteps), UI earcons
//   (playEarcon, fed by UiModule) and navigation pings (ping(), bound to
//   an action): docs/AUDIO.md "Accessibility".
// - Listener: the Application camera, unless listenerOverride is set.
class AudioModule : public Module {
public:
    struct Settings {
        int sampleRate = 48000;
        int maxVoices = 32;
        bool openDevice = true;          // false (or KKE_AUDIO=off): always silent mode
        float impactThreshold = 0.8f;    // m/s: slower contacts make no sound
        float impactFullSpeed = 9.0f;    // m/s: loudest impact
        int maxImpactsPerFrame = 8;      // strongest first
        float pairCooldown = 0.08f;      // s between sounds from the same two bodies
        bool occlusion = true;
        bool reverb = true;              // ray-traced room reverb
        bool openings = true;            // occluded sounds come through doors/windows the probe found
        bool echoes = true;              // early reflections off the walls around you
        float roomProbeInterval = 0.25f; // s
        // Rays the audio may cast in one frame for occlusion rechecks
        // (the room probe and new sounds always get theirs). Past it,
        // sounds keep their last value a frame longer: the cost stays flat
        // however many sounds play.
        int maxRaysPerFrame = 160;
        SpatialMode spatial = SpatialMode::Stereo; // KKE_AUDIO_BINAURAL=1 starts in Binaural
        bool earcons = true;             // UI sounds on focus/click/change
        float pingRange = 12.0f;         // m, navigation pings
        int pingRays = 8;                // around you, one ping each
    };

    AudioModule();
    explicit AudioModule(const Settings& settings);
    ~AudioModule() override;

    const char* name() const override { return "Audio"; }
    void init(Application& app) override;
    void update(const UpdateContext& ctx) override;
    void renderUi() override;
    void shutdown() override;

    AudioMixer& mixer() { return *m_mixer; }
    AudioMaterialTable& materials() { return m_materials; }
    ImpactBank& impacts() { return *m_bank; }
    bool deviceRunning() const { return m_deviceRunning; }
    const std::string& deviceName() const { return m_deviceName; }

    // An impact sound of `material` at `position`. intensity 0..1.
    uint32_t playImpact(const glm::vec3& position, uint32_t material, float intensity, uint32_t seed = 0, float gain = 1.0f);
    // Plays `desc`, occluded from its first sample (see soundPath).
    uint32_t play(const VoiceDesc& desc);
    // A footstep on `material` (kke::CharacterFootsteps calls this).
    uint32_t playFootstep(const glm::vec3& position, uint32_t material, float intensity, uint32_t seed = 0, float gain = 0.8f);
    // A UI sound, centred (not spatial).
    uint32_t playEarcon(Earcon e, float gain = 0.6f);
    // Navigation pings: `pingRays` rays around the listener, level with it;
    // each wall within pingRange pings from its direction, higher the
    // closer; a direction with nothing in range sounds "open". Staggered
    // clockwise from straight ahead, 70 ms apart.
    void ping();

    // The listener's room, as last probed (steadied over the last probes).
    const RoomAcoustics& room() const { return m_room; }
    RoomTracker& roomTracker() { return m_tracker; }
    // How a sound at `source` reaches the listener: through the walls in
    // between (their materials and thickness), or round them through an
    // opening. What play() and the rechecks use.
    struct SoundPath {
        float transmission = 1.0f;
        bool viaOpening = false;
        glm::vec3 via{0.0f};
        float pathLength = 0.0f;
    };
    SoundPath soundPath(const glm::vec3& listener, const glm::vec3& source);
    int raysLastFrame() const { return m_raysLastFrame; }
    // What the room probe and pings cast. Jolt by default; replaceable.
    AcousticRayFn roomRay;

    // Decodes WAV / FLAC / MP3 (miniaudio) to mono at the mixer's rate.
    // Null on failure (logged).
    SoundHandle loadSound(const std::string& path);

    // Records everything the mixer outputs (device or silent) until
    // stopRecording(), which writes it to `path` as a 16-bit stereo WAV.
    // KKE_AUDIO_RECORD=file.wav records the whole run. For hearing what a
    // headless run (CI, a server) played, and for comparing changes.
    void startRecording(const std::string& path);
    // False (and logged) when nothing was recording or the file couldn't
    // be written.
    bool stopRecording();
    bool recording() const { return !m_recordPath.empty(); }

    // Returns 0..1, how much sound gets from `source` to `listener`.
    std::function<float(const glm::vec3& listener, const glm::vec3& source)> occlusionQuery;
    // Set to use something other than the camera as the ears.
    std::function<Listener()> listenerOverride;

    Settings settings;

    // Plays each material in turn, walking around the listener (front,
    // right, back, left), one every 0.6 s: for hearing/seeing directions
    // and materials without setting anything up. KKE_AUDIO_TOUR=1 or the
    // panel's checkbox.
    bool tour = false;

private:
    struct Device;
    struct Tracked { float recheckIn = 0.0f; };
    struct PendingPing { float in; glm::vec3 position; SoundHandle sound; };

    void handleContacts();
    void handleSoftImpacts();  // FEMFX objects hitting each other or the floor
    void handleBreaks();
    void updateOcclusion(float dt);
    void updateRoom(float dt);
    void updatePings(float dt);
    // A point by an opening that `source` can be heard through, or false.
    bool findOpening(const glm::vec3& listener, const glm::vec3& source, glm::vec3& via, float& pathLength);
    uint32_t playTracked(VoiceDesc d);

    Application* m_app = nullptr;
    std::unique_ptr<AudioMixer> m_mixer;
    AudioMaterialTable m_materials;
    std::unique_ptr<ImpactBank> m_bank;
    std::unique_ptr<Device> m_device;
    bool m_deviceRunning = false;
    bool m_shutDown = false;
    std::string m_deviceName = "none (silent)";
    std::vector<float> m_scratch;
    double m_silentBacklog = 0.0;
    double m_time = 0.0;
    uint32_t m_seedCounter = 1;
    std::unordered_map<uint64_t, double> m_pairLastSound;
    std::unordered_map<uint32_t, Tracked> m_tracked;
    uint64_t m_impactsPlayed = 0, m_impactsSkipped = 0, m_breaksPlayed = 0;
    float m_tourTimer = 0.0f;
    int m_tourStep = 0;
    RoomAcoustics m_room;
    RoomTracker m_tracker;
    uint32_t m_probeCount = 0;
    glm::vec3 m_lastProbeAt{0.0f};
    float m_roomProbeIn = 0.0f;
    int m_raysThisFrame = 0, m_raysLastFrame = 0;
    std::vector<PendingPing> m_pings;
    SoundHandle m_earcons[size_t(Earcon::Count)];
    uint64_t m_footsteps = 0;
    std::string m_recordPath;
};

} // namespace kke
