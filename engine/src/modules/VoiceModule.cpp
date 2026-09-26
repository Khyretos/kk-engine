#include "kke/modules/VoiceModule.h"

#include "kke/Application.h"
#include "kke/Log.h"
#include "kke/modules/AudioModule.h"
#include "kke/modules/InputModule.h"
#include "kke/modules/NetModule.h"

#include <imgui.h>
#include <miniaudio.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <typeindex>
#include <vector>

namespace kke {

struct VoiceModule::Capture {
    ma_device device;
};

namespace {

void captureCallback(ma_device* device, void*, const void* input, ma_uint32 frames) {
    auto* stream = static_cast<AudioStream*>(device->pUserData);
    if (input && stream) stream->push(static_cast<const float*>(input), frames);
}

constexpr double kSpeakingSeconds = 0.3;
constexpr size_t kMinCushion = 2 * voice::kFrameSamples; // decoded audio kept ahead of the mixer, at least

} // namespace

VoiceModule::VoiceModule() = default;
VoiceModule::VoiceModule(const Settings& s) : settings(s) {}

VoiceModule::~VoiceModule() {
    if (m_capture && m_captureRunning) ma_device_uninit(&m_capture->device);
}

std::vector<ModuleDependency> VoiceModule::dependencies() const {
    return { { std::type_index(typeid(NetModule)), true, "sends and receives the voice" },
             { std::type_index(typeid(AudioModule)), false, "plays the others' voices" } };
}

void VoiceModule::init(Application& app) {
    m_app = &app;
    auto log = log::get(name());
    m_encoder = std::make_unique<voice::VoiceEncoder>();
    if (!m_encoder->ok()) log->error("Opus encoder: {}; voice chat can't send", m_encoder->error());
    m_encoder->setBitrate(settings.bitrate);

    if (const char* t = std::getenv("KKE_VOICE_TONE"); t && *t) {
        m_toneHz = std::clamp(static_cast<float>(std::atof(t)), 0.0f, 8000.0f);
        if (m_toneHz > 0.0f) {
            settings.mode = TalkMode::Open;
            settings.openMicrophone = false; // the tone stands in for it
            log->info("Sending a {} Hz tone instead of the microphone (KKE_VOICE_TONE)", m_toneHz);
        }
    }
    auto envOff = [](const char* name) {
        const char* v = std::getenv(name);
        return v && (std::strcmp(v, "off") == 0 || std::strcmp(v, "0") == 0);
    };
    if (envOff("KKE_VOICE_NOISE")) settings.noiseSuppression = false;
    if (envOff("KKE_VOICE_ECHO")) settings.echoCancellation = false;
    const char* env = std::getenv("KKE_VOICE");
    const bool off = env && (std::strcmp(env, "off") == 0 || std::strcmp(env, "0") == 0);
    if (const char* a = std::getenv("KKE_AUDIO"); a && (std::strcmp(a, "off") == 0 || std::strcmp(a, "0") == 0)) settings.openMicrophone = false;
    if (settings.openMicrophone && !off) {
        m_capture = std::make_unique<Capture>();
        ma_device_config cfg = ma_device_config_init(ma_device_type_capture);
        cfg.capture.format = ma_format_f32;
        cfg.capture.channels = 1;
        cfg.sampleRate = voice::kSampleRate;
        cfg.dataCallback = captureCallback;
        cfg.pUserData = m_captured.get();
        cfg.periodSizeInMilliseconds = 10;
        if (ma_device_init(nullptr, &cfg, &m_capture->device) == MA_SUCCESS) {
            if (ma_device_start(&m_capture->device) == MA_SUCCESS) {
                m_captureRunning = true;
                m_captureName = m_capture->device.capture.name;
            } else {
                ma_device_uninit(&m_capture->device);
            }
        }
        if (!m_captureRunning) {
            m_capture.reset();
            log->warn("No microphone; voice chat listens only");
        }
    }
    if (m_captureRunning) {
        m_cleaner = std::make_unique<voice::VoiceCleaner>();
        // The echo canceller needs what the speakers play, sample for sample at our rate.
        if (auto* audio = app.getModule<AudioModule>(); audio && audio->mixer().sampleRate() == voice::kSampleRate) {
            m_played = std::make_shared<AudioStream>(8 * voice::kFrameSamples);
            audio->mixer().setOutputTap(m_played);
        } else if (settings.echoCancellation) {
            log->info("No echo cancellation: it needs the audio output at {} Hz", voice::kSampleRate);
            settings.echoCancellation = false;
        }
    }
    if (auto* net = app.getModule<NetModule>()) net->addVoiceListener([this](const net::VoiceMsg& m) { onVoice(m); });
    log->info("Voice chat ready: Opus {} kbit/s, microphone: {}{}{}", settings.bitrate / 1000, m_captureName,
              m_cleaner && settings.noiseSuppression ? ", noise suppression" : "", m_cleaner && settings.echoCancellation ? ", echo cancellation" : "");
}

void VoiceModule::shutdown() {
    uint64_t late = 0, skipped = 0, underruns = 0;
    for (const auto& [id, sp] : m_speakers) {
        late += sp.jitter.late();
        skipped += sp.jitter.skipped();
        underruns += sp.stream->underruns();
    }
    log::get(name())->info("Voice chat: sent {} frames, played {}, concealed {}; late {}, skipped {}, playback underruns {}", m_framesSent, m_framesPlayed,
                           m_framesConcealed, late, skipped, underruns);
    if (m_capture && m_captureRunning) ma_device_uninit(&m_capture->device);
    m_captureRunning = false;
    m_capture.reset();
    if (m_played)
        if (auto* audio = m_app ? m_app->getModule<AudioModule>() : nullptr) audio->mixer().setOutputTap(nullptr);
    m_played.reset();
    m_cleaner.reset();
    clearSpeakers();
}

void VoiceModule::feedCapture(const float* samples, size_t count) { m_captured->push(samples, count); }

bool VoiceModule::speaking(uint8_t playerId) const {
    auto it = m_speakers.find(playerId);
    return it != m_speakers.end() && m_time - it->second.lastHeard < kSpeakingSeconds;
}

void VoiceModule::mute(uint8_t playerId, bool on) {
    if (on) m_muted.insert(playerId);
    else m_muted.erase(playerId);
}

void VoiceModule::setVolume(uint8_t playerId, float gain) {
    m_volumes[playerId] = std::clamp(gain, 0.0f, 4.0f);
    if (auto it = m_speakers.find(playerId); it != m_speakers.end()) it->second.gain = m_volumes[playerId];
}

AudioStreamHandle VoiceModule::streamOf(uint8_t playerId) const {
    auto it = m_speakers.find(playerId);
    return it == m_speakers.end() ? nullptr : it->second.stream;
}

void VoiceModule::onVoice(const net::VoiceMsg& m) {
    if (m.speaker == kSelf) return; // not a real player id
    if (!m_speakers.count(m.speaker)) log::get(name())->info("Hearing player {}", m.speaker);
    Speaker& s = m_speakers[m.speaker];
    if (auto v = m_volumes.find(m.speaker); v != m_volumes.end()) s.gain = v->second;
    s.channel = m.channel;
    s.lastHeard = m_time;
    s.jitter.push(m.seq, m.data);
}

void VoiceModule::clearSpeakers() {
    auto* audio = m_app ? m_app->getModule<AudioModule>() : nullptr;
    for (auto& [id, s] : m_speakers) {
        s.stream->close();
        if (audio && s.mixerVoice) audio->mixer().stop(s.mixerVoice);
    }
    m_speakers.clear();
}

void VoiceModule::sendCaptured() {
    auto* net = m_app->getModule<NetModule>();
    const bool online = net && net->role() != NetModule::Role::Offline;
    auto* input = m_app->getModule<InputModule>();
    const bool held = pushToTalk || (input && input->map(0).action("voice.talk") && input->map(0).held("voice.talk"));
    float frame[voice::kFrameSamples], played[voice::kFrameSamples];
    m_talking = false;
    // The speakers' output is read in step with the microphone; if it got
    // ahead (the game hitched), the oldest goes, so the echo canceller's
    // reference stays within its reach.
    if (m_played && m_played->buffered() > size_t(4 * voice::kFrameSamples) + m_captured->buffered()) {
        std::vector<float> drop(m_played->buffered() - 2 * voice::kFrameSamples - m_captured->buffered());
        m_played->read(drop.data(), drop.size());
    }
    // Everything the microphone gave since last frame, 20 ms at a time.
    while (m_captured->buffered() >= size_t(voice::kFrameSamples)) {
        m_captured->read(frame, voice::kFrameSamples);
        if (settings.inputGain != 1.0f)
            for (float& x : frame) x = std::clamp(x * settings.inputGain, -1.0f, 1.0f);
        const float* reference = nullptr;
        if (m_played && m_played->buffered() >= size_t(voice::kFrameSamples)) {
            m_played->read(played, voice::kFrameSamples);
            reference = played;
        }
        if (m_cleaner && m_toneHz <= 0.0f) { // (a test tone isn't noise to take out)
            m_cleaner->noiseSuppression = settings.noiseSuppression;
            m_cleaner->echoCancellation = settings.echoCancellation && m_played;
            m_cleaner->process(frame, reference);
        }
        const bool voiced = m_vad.process(frame, voice::kFrameSamples, voice::kFrameSeconds);
        const bool send = settings.mode == TalkMode::Open || (settings.mode == TalkMode::PushToTalk && held) ||
                          (settings.mode == TalkMode::VoiceActivated && voiced);
        if (!send || (!online && !hearMyself) || !m_encoder->ok()) continue;
        m_talking = true;
        const std::vector<uint8_t> coded = m_encoder->encode(frame);
        if (coded.empty()) continue;
        const uint16_t seq = m_seq++;
        if (online) {
            net->sendVoice(settings.channel, seq, coded);
            ++m_framesSent;
        }
        if (hearMyself) {
            Speaker& me = m_speakers[kSelf];
            me.channel = net::VoiceChannel::All; // in your head, not from a place
            me.lastHeard = m_time;
            me.jitter.push(seq, coded);
        }
    }
}

void VoiceModule::place(uint8_t id, Speaker& s) {
    auto* audio = m_app->getModule<AudioModule>();
    if (!audio) return;
    const bool spatial = id != kSelf && s.channel == net::VoiceChannel::Proximity;
    glm::vec3 pos(0.0f);
    bool found = !spatial;
    if (spatial)
        if (auto* net = m_app->getModule<NetModule>())
            for (const net::RemotePlayer& p : net->remotePlayers())
                if (p.id == id && p.hasState) {
                    pos = p.state.position + glm::vec3(0.0f, 1.6f, 0.0f); // the mouth, not the feet
                    found = true;
                }
    if (!found) return; // not placed yet: wait for their first state
    AudioMixer& mixer = audio->mixer();
    if (s.mixerVoice && mixer.isPlaying(s.mixerVoice)) {
        if (spatial) mixer.setPosition(s.mixerVoice, pos);
        return;
    }
    if (s.stream->buffered() == 0) return;
    VoiceDesc d;
    d.stream = s.stream;
    d.spatial = spatial;
    d.position = pos;
    d.gain = 1.0f; // the volumes are applied as it's decoded, so changes are heard at once
    d.minDistance = 2.0f;
    d.maxDistance = settings.hearingRange;
    d.priority = 10.0f; // people over crates when voices run short
    d.category = SoundCategory::Voice;
    d.reverbSend = 0.4f;
    s.mixerVoice = mixer.play(d);
}

void VoiceModule::playOut(float dt) {
    // Enough decoded audio to last until the next frame, and then some: a
    // slow frame (a loading hitch, a weak GPU) mustn't starve the mixer.
    const size_t cushion = std::clamp(static_cast<size_t>(dt * 2.0f * voice::kSampleRate), kMinCushion, size_t(voice::kSampleRate / 4));
    float pcm[voice::kFrameSamples];
    for (auto& [id, s] : m_speakers) {
        if (m_muted.count(id)) {
            s.jitter.reset();
            continue;
        }
        while (s.stream->buffered() < cushion) {
            voice::JitterBuffer::Out o = s.jitter.pop();
            if (o.kind == voice::JitterBuffer::Kind::Nothing) break;
            if (o.kind == voice::JitterBuffer::Kind::Frame) {
                s.decoder->decode(o.data, pcm);
                ++m_framesPlayed;
            } else {
                s.decoder->conceal(o.next, pcm);
                ++m_framesConcealed;
            }
            const float g = settings.outputGain * s.gain;
            if (g != 1.0f)
                for (float& x : pcm) x *= g;
            s.stream->push(pcm, voice::kFrameSamples);
        }
        place(id, s);
    }
}

void VoiceModule::update(const UpdateContext& ctx) {
    m_time += ctx.dt;
    auto* net = m_app->getModule<NetModule>();
    const int role = net ? static_cast<int>(net->role()) : 0;
    if (role != m_netRole) { // joined, hosted or left: nobody's voice carries over
        m_netRole = role;
        clearSpeakers();
    }
    if (m_toneHz > 0.0f) { // KKE_VOICE_TONE: a test tone, 1 s on, 1 s off, as if spoken
        m_toneBacklog += ctx.dt * voice::kSampleRate;
        std::vector<float> tone(static_cast<size_t>(m_toneBacklog));
        m_toneBacklog -= double(tone.size());
        const bool on = std::fmod(m_time, 2.0) < 1.0;
        for (float& x : tone) {
            x = on ? 0.3f * static_cast<float>(std::sin(m_tonePhase)) : 0.0f;
            m_tonePhase = std::fmod(m_tonePhase + 2.0 * 3.14159265358979 * m_toneHz / voice::kSampleRate, 2.0 * 3.14159265358979);
        }
        m_captured->push(tone.data(), tone.size());
    }
    sendCaptured();
    playOut(ctx.dt);
    // Players who left: their voice goes once they've been quiet a while.
    if (net)
        for (auto it = m_speakers.begin(); it != m_speakers.end();) {
            const bool present = it->first == kSelf || std::any_of(net->remotePlayers().begin(), net->remotePlayers().end(),
                                                                    [&](const net::RemotePlayer& p) { return p.id == it->first; });
            if (!present && m_time - it->second.lastHeard > 2.0) {
                it->second.stream->close();
                it = m_speakers.erase(it);
            } else {
                ++it;
            }
        }
}

void VoiceModule::renderUi() {
    const float sc = ImGui::GetFontSize() / 13.0f;
    ImGui::SetNextWindowSize(ImVec2(300 * sc, 0), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Voice")) {
        ImGui::End();
        return;
    }
    ImGui::TextWrapped("Microphone: %s", m_captureName.c_str());
    int mode = static_cast<int>(settings.mode);
    ImGui::Combo("Talk", &mode, "Push to talk (B)\0Voice activated\0Open mic\0");
    settings.mode = static_cast<TalkMode>(mode);
    int channel = static_cast<int>(settings.channel);
    ImGui::Combo("Channel", &channel, "Nearby\0Team\0Everyone\0");
    settings.channel = static_cast<net::VoiceChannel>(channel);
    ImGui::SliderFloat("Mic volume", &settings.inputGain, 0.0f, 4.0f);
    ImGui::SliderFloat("Voices volume", &settings.outputGain, 0.0f, 2.0f);
    ImGui::Checkbox("Hear myself (mic test)", &hearMyself);
    if (m_cleaner) {
        ImGui::Checkbox("Noise suppression", &settings.noiseSuppression);
        ImGui::BeginDisabled(!m_played);
        ImGui::Checkbox("Echo cancellation", &settings.echoCancellation);
        ImGui::EndDisabled();
        if (settings.echoCancellation && m_played) {
            ImGui::SameLine();
            ImGui::TextDisabled("-%.0f dB", static_cast<double>(m_cleaner->echoReductionDb()));
        }
    }
    const float level = std::clamp((m_vad.levelDb() + 60.0f) / 60.0f, 0.0f, 1.0f);
    ImGui::ProgressBar(level, ImVec2(-1, 0), m_talking ? "talking" : "");
    if (auto* net = m_app->getModule<NetModule>()) {
        for (const net::RemotePlayer& p : net->remotePlayers()) {
            ImGui::PushID(p.id);
            bool on = !muted(p.id);
            if (ImGui::Checkbox("##hear", &on)) mute(p.id, !on);
            ImGui::SameLine();
            ImGui::Text("%s%s", p.name.c_str(), speaking(p.id) ? "  (speaking)" : "");
            ImGui::PopID();
        }
    }
    ImGui::Text("sent %llu, played %llu, concealed %llu", static_cast<unsigned long long>(m_framesSent), static_cast<unsigned long long>(m_framesPlayed),
                static_cast<unsigned long long>(m_framesConcealed));
    ImGui::End();
}

} // namespace kke
