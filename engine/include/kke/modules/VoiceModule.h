#pragma once

// Voice chat (docs/NETWORKING.md "Voice"): the microphone, coded with
// Opus, sent through NetModule, played back where each speaker stands.
//
//   Talk:     push-to-talk (the "voice.talk" action, B by default), voice
//             activated (a gate that opens when you speak), or open mic.
//   Channels: Proximity (people near you hear you, from where you are),
//             Team, All (heard as a radio, not from a place). The server
//             decides who hears what (net::VoiceRules on the host).
//   Hearing:  each speaker gets a jitter buffer (late and lost packets
//             concealed; Opus FEC rebuilds a single lost one) and a live
//             AudioStream voice in the mixer, so it's occluded and
//             reverberated like any other sound (category Voice).
//   You:      mute anyone locally, set their volume, see who's speaking
//             (speaking(id), and the panel). "Hear myself" tests the mic.
//             talkers() says who spoke lately, how loud, how far and from
//             which direction: kke::VoiceHudModule draws it (a marker over
//             each talker's head, an arrow at the screen's edge for one
//             out of view, and a list of nearby talkers to mute).
//   Clean:    before coding, the microphone goes through echo
//             cancellation (what the speakers play is taken out, so
//             nobody hears themselves back when you don't wear
//             headphones) and noise suppression (RNNoise takes out fans,
//             keys, traffic); kke/voice/VoiceCleaner.h.
//
// KKE_VOICE=off leaves the microphone closed. KKE_VOICE_NOISE=off and
// KKE_VOICE_ECHO=off switch the cleaning off (both on by default). KKE_VOICE_TONE=440 sends a
// tone of that pitch instead of the microphone, open mic (check a
// connection without anyone talking). No microphone (a server, CI):
// it still plays others; feedCapture() stands in for one in tests.

#include "kke/AudioMixer.h"
#include "kke/Module.h"
#include "kke/net/Protocol.h"
#include "kke/voice/JitterBuffer.h"
#include "kke/voice/VoiceCleaner.h"
#include "kke/voice/VoiceCodec.h"

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>

namespace kke {

class VoiceModule : public Module {
public:
    enum class TalkMode : uint8_t { PushToTalk, VoiceActivated, Open };
    struct Settings {
        TalkMode mode = TalkMode::PushToTalk;
        net::VoiceChannel channel = net::VoiceChannel::Proximity;
        int bitrate = 24000;          // bits/s
        float inputGain = 1.0f;
        float outputGain = 1.0f;
        float hearingRange = 40.0f;   // m: proximity voices fade out to here (hosting, the server's
                                      // VoiceRules::proximityRange follows it, so nobody far is even sent)
        float mouthHeight = 1.6f;     // m above a player's state position (their feet) a voice comes from
        bool openMicrophone = true;   // false: listen only
        bool noiseSuppression = true; // RNNoise on the microphone
        bool echoCancellation = true; // take out what the speakers play (needs the audio output at 48 kHz)
    };

    VoiceModule();
    explicit VoiceModule(const Settings& s);
    ~VoiceModule() override;

    const char* name() const override { return "Voice"; }
    std::vector<ModuleDependency> dependencies() const override;
    void init(Application& app) override;
    void update(const UpdateContext& ctx) override;
    void renderUi() override;
    void shutdown() override;

    Settings settings;
    bool pushToTalk = false; // the game may hold this itself (a touch button); the action works too
    bool hearMyself = false; // loop the microphone back through the codec

    bool microphoneOpen() const { return m_captureRunning; }
    const std::string& microphoneName() const { return m_captureName; }
    bool talking() const { return m_talking; }         // sending this frame
    bool speaking(uint8_t playerId) const;             // heard in the last 0.3 s
    void mute(uint8_t playerId, bool muted);
    // Voice chat off for this player: nothing is sent (the microphone's
    // sound is dropped) and nobody is heard, until it's turned back on.
    void setEnabled(bool on);
    bool enabled() const { return m_enabled; }
    bool muted(uint8_t playerId) const { return m_muted.count(playerId) != 0; }
    void setVolume(uint8_t playerId, float gain);
    float inputLevelDb() const { return m_vad.levelDb(); }
    // The cleaner's view of the last microphone frame: speech probability
    // (-1: noise suppression off) and how much echo it took out (dB).
    float voiceProbability() const { return m_cleaner ? m_cleaner->voiceProbability() : -1.0f; }
    float echoReductionDb() const { return m_cleaner ? m_cleaner->echoReductionDb() : 0.0f; }

    // Tests and tools: microphone samples (mono, 48 kHz) as if captured.
    void feedCapture(const float* samples, size_t count);
    // What was sent / played so far.
    uint64_t framesSent() const { return m_framesSent; }
    uint64_t framesPlayed() const { return m_framesPlayed; }
    uint64_t framesConcealed() const { return m_framesConcealed; }
    // A speaker's stream (tests read what would be heard).
    AudioStreamHandle streamOf(uint8_t playerId) const;

    // Where a speaker's voice comes from, when the game knows better than
    // the player's network state + mouthHeight (a bean's head, a seat).
    // Return false to fall back to the state.
    std::function<bool(uint8_t playerId, glm::vec3& mouth)> speakerPosition;

    // Someone heard lately, as a speaking indicator or a mute list sees
    // them. Direction and distance are from the listener (the audio
    // module's ears, else the camera): azimuth 0 = straight ahead, +pi/2 =
    // right, +-pi = behind.
    struct Talker {
        uint8_t id = 0;
        std::string name;
        bool speaking = false;   // heard in the last 0.3 s
        float quietFor = 0.0f;   // s since last heard
        bool muted = false;      // by you (their voice still arrives, so you can see them talk and unmute)
        bool placed = false;     // heard from a place (proximity, position known)
        glm::vec3 mouth{0.0f};
        float distance = 0.0f;   // m (placed only)
        float azimuth = 0.0f;    // radians (placed only)
        float elevation = 0.0f;  // radians, + above (placed only)
        float level = 0.0f;      // 0..1, how loud their voice is right now (before distance)
    };
    // Everyone heard in the last `recentSeconds`, nearest first (voices
    // with no place last).
    std::vector<Talker> talkers(float recentSeconds = 5.0f) const;
    // A player's name (or "Player N" when they're not in the list any more).
    std::string nameOf(uint8_t playerId) const;

private:
    struct Capture;
    struct Speaker {
        voice::JitterBuffer jitter;
        std::unique_ptr<voice::VoiceDecoder> decoder = std::make_unique<voice::VoiceDecoder>();
        AudioStreamHandle stream = std::make_shared<AudioStream>(voice::kSampleRate / 2);
        uint32_t mixerVoice = 0;
        net::VoiceChannel channel = net::VoiceChannel::Proximity;
        double lastHeard = -1e9;
        float gain = 1.0f;
        float level = 0.0f; // 0..1, smoothed
    };
    void onVoice(const net::VoiceMsg& m);
    void sendCaptured();
    void playOut(float dt);
    void place(uint8_t id, Speaker& s);
    bool mouthOf(uint8_t id, glm::vec3& mouth) const;
    void clearSpeakers();
    static constexpr uint8_t kSelf = 255; // "hear myself"

    Application* m_app = nullptr;
    std::unique_ptr<Capture> m_capture;
    bool m_captureRunning = false;
    std::string m_captureName = "none";
    bool m_enabled = true;
    AudioStreamHandle m_captured = std::make_shared<AudioStream>(voice::kSampleRate); // mic -> game thread
    std::unique_ptr<voice::VoiceEncoder> m_encoder;
    std::unique_ptr<voice::VoiceCleaner> m_cleaner;
    AudioStreamHandle m_played; // what the speakers play (AudioMixer's output tap): the echo reference
    voice::VoiceActivity m_vad;
    std::map<uint8_t, Speaker> m_speakers;
    std::map<uint8_t, float> m_volumes;
    std::set<uint8_t> m_muted;
    int m_netRole = 0;
    uint16_t m_seq = 0;
    bool m_talking = false;
    double m_time = 0.0;
    float m_toneHz = 0.0f;
    double m_tonePhase = 0.0, m_toneBacklog = 0.0;
    uint64_t m_framesSent = 0, m_framesPlayed = 0, m_framesConcealed = 0;
};

} // namespace kke
