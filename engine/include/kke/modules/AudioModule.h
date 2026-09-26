#pragma once

#include "kke/AudioMixer.h"
#include "kke/ImpactSynth.h"
#include "kke/Module.h"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

namespace kke {

// The audio engine as a module (ACTION_PLAN.md 2.2, AUDIO.md).
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
//   FEMFX breaks (PhysicsModule::frameBreaks) play as a crack of the
//   object's material (Material::audioMaterial, or guessed).
// - Occlusion: one ray per playing sound from the listener, re-cast every
//   0.1 s; a wall in between lets through its material's `transmission`
//   and muffles the highs. Replaceable: set occlusionQuery.
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
    uint32_t play(const VoiceDesc& desc) { return m_mixer->play(desc); }

    // Decodes WAV / FLAC / MP3 (miniaudio) to mono at the mixer's rate.
    // Null on failure (logged).
    SoundHandle loadSound(const std::string& path);

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

    void handleContacts();
    void handleBreaks();
    void updateOcclusion(float dt);

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
};

} // namespace kke
