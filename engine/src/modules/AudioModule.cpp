#include "kke/modules/AudioModule.h"

#include "kke/Application.h"
#include "kke/Log.h"
#include "kke/modules/SoundVisualizerModule.h"
#if KKE_ENABLE_JOLT
#include "kke/modules/RigidBodyModule.h"
#endif
#if KKE_ENABLE_FEMFX
#include "kke/modules/PhysicsModule.h"
#endif

#include <glm/gtc/constants.hpp>
#include <imgui.h>
#include <miniaudio.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace kke {

struct AudioModule::Device {
    ma_device device{};
};

namespace {
void dataCallback(ma_device* device, void* output, const void*, ma_uint32 frameCount) {
    auto* mixer = static_cast<AudioMixer*>(device->pUserData);
    mixer->mix(static_cast<float*>(output), int(frameCount));
}
} // namespace

AudioModule::AudioModule() : AudioModule(Settings{}) {}
AudioModule::AudioModule(const Settings& s) : settings(s) {}
AudioModule::~AudioModule() {
    // Only the device, if shutdown() never ran: nothing else (logging) is safe this late.
    if (m_device && m_deviceRunning) ma_device_uninit(&m_device->device);
}

void AudioModule::init(Application& app) {
    m_app = &app;
    m_mixer = std::make_unique<AudioMixer>(settings.sampleRate, settings.maxVoices);
    m_bank = std::make_unique<ImpactBank>(m_materials, settings.sampleRate);
    auto log = log::get(name());

    const char* env = std::getenv("KKE_AUDIO");
    const bool wantDevice = settings.openDevice && !(env && (std::strcmp(env, "off") == 0 || std::strcmp(env, "0") == 0));
    if (wantDevice) {
        m_device = std::make_unique<Device>();
        ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
        cfg.playback.format = ma_format_f32;
        cfg.playback.channels = 2;
        cfg.sampleRate = ma_uint32(settings.sampleRate);
        cfg.dataCallback = dataCallback;
        cfg.pUserData = m_mixer.get();
        // ~10 ms blocks: low latency for impacts without starving a 1-core machine.
        cfg.periodSizeInMilliseconds = 10;
        if (ma_device_init(nullptr, &cfg, &m_device->device) == MA_SUCCESS) {
            if (ma_device_start(&m_device->device) == MA_SUCCESS) {
                m_deviceRunning = true;
                m_deviceName = m_device->device.playback.name;
                m_deviceName += " (";
                m_deviceName += ma_get_backend_name(m_device->device.pContext->backend);
                m_deviceName += ")";
            } else {
                ma_device_uninit(&m_device->device);
            }
        }
        if (!m_deviceRunning) {
            m_device.reset();
            log->warn("No audio output device; running silent (sounds are still mixed for the visualizer)");
        }
    }
    if (const char* t = std::getenv("KKE_AUDIO_TOUR"); t && *t && *t != '0') tour = true;
    if (const char* b = std::getenv("KKE_AUDIO_BINAURAL"); b && *b && *b != '0') settings.spatial = SpatialMode::Binaural;
    m_mixer->setSpatialMode(settings.spatial);
    log->info("Audio ready: {} Hz, {} voices, output: {}", settings.sampleRate, settings.maxVoices, m_deviceName);

#if KKE_ENABLE_JOLT
    if (!occlusionQuery) {
        occlusionQuery = [this](const glm::vec3& from, const glm::vec3& to) -> float {
            auto* rb = m_app ? m_app->getModule<RigidBodyModule>() : nullptr;
            if (!rb) return 1.0f;
            const glm::vec3 d = to - from;
            const float dist = glm::length(d);
            if (dist < 0.5f) return 1.0f;
            RigidWorld::RayHit hit = rb->world().raycast(from, d, dist);
            // The sounding object itself is usually hit right at the end.
            if (!hit.hit || hit.distance > dist - 0.7f) return 1.0f;
            return m_materials.get(hit.material).transmission;
        };
    }
    if (!roomRay) {
        roomRay = [this](const glm::vec3& from, const glm::vec3& dir, float maxDistance) -> AcousticRay {
            auto* rb = m_app ? m_app->getModule<RigidBodyModule>() : nullptr;
            if (!rb) return {};
            const RigidWorld::RayHit hit = rb->world().raycast(from, dir, maxDistance);
            return AcousticRay{hit.hit, hit.distance, hit.material, hit.normal};
        };
    }
#endif
}

uint32_t AudioModule::playImpact(const glm::vec3& position, uint32_t material, float intensity, uint32_t seed, float gain) {
    if (!m_mixer || intensity <= 0.0f) return 0;
    if (seed == 0) seed = m_seedCounter++;
    VoiceDesc d;
    d.sound = m_bank->get(material, intensity, seed);
    d.position = position;
    d.gain = gain;
    d.minDistance = 1.5f;
    d.maxDistance = 50.0f;
    d.category = SoundCategory::Impact;
    d.material = material;
    const uint32_t id = m_mixer->play(d);
    if (id) m_tracked[id] = Tracked{0.0f};
    return id;
}

uint32_t AudioModule::playFootstep(const glm::vec3& position, uint32_t material, float intensity, uint32_t seed, float gain) {
    if (!m_mixer || intensity <= 0.0f) return 0;
    if (seed == 0) seed = m_seedCounter++;
    VoiceDesc d;
    d.sound = m_bank->getFootstep(material, intensity, seed);
    d.position = position;
    d.gain = gain;
    d.minDistance = 1.0f;
    d.maxDistance = 25.0f;
    d.priority = 0.6f; // an impact nearby matters more than a step
    d.category = SoundCategory::Footstep;
    d.material = material;
    const uint32_t id = m_mixer->play(d);
    if (id) {
        m_tracked[id] = Tracked{0.0f};
        ++m_footsteps;
    }
    return id;
}

uint32_t AudioModule::playEarcon(Earcon e, float gain) {
    if (!m_mixer || !settings.earcons || e >= Earcon::Count) return 0;
    SoundHandle& h = m_earcons[size_t(e)];
    if (!h) h = std::make_shared<SoundBuffer>(synthesizeEarcon(e, settings.sampleRate));
    VoiceDesc d;
    d.sound = h;
    d.spatial = false;
    d.gain = gain;
    d.priority = 2.0f; // the menu must always answer
    d.category = SoundCategory::Ui;
    d.reverbSend = 0.0f;
    return m_mixer->play(d);
}

void AudioModule::ping() {
    if (!m_mixer) return;
    const Listener l = m_mixer->listener();
    glm::vec3 fwd(l.forward.x, 0.0f, l.forward.z);
    if (glm::dot(fwd, fwd) < 1e-8f) fwd = glm::vec3(0, 0, -1);
    fwd = glm::normalize(fwd);
    const glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(0, 1, 0)));
    const int n = std::max(1, settings.pingRays);
    const float range = std::max(1.0f, settings.pingRange);
    m_pings.clear();
    for (int i = 0; i < n; ++i) {
        const float az = glm::two_pi<float>() * float(i) / float(n);
        const glm::vec3 dir = fwd * std::cos(az) + right * std::sin(az);
        const AcousticRay r = roomRay ? roomRay(l.position, dir, range) : AcousticRay{};
        const bool open = !r.hit || r.distance >= range;
        const float dist = open ? range : r.distance;
        auto sound = std::make_shared<SoundBuffer>(synthesizePing(dist, range, open, m_materials.get(r.material), settings.sampleRate));
        // The ping sits where the wall is (open: a few metres out), no
        // closer than 1 m so the direction stays clear.
        m_pings.push_back({0.07f * float(i), l.position + dir * std::clamp(open ? 4.0f : dist, 1.0f, 6.0f), std::move(sound)});
    }
}

void AudioModule::updatePings(float dt) {
    for (auto it = m_pings.begin(); it != m_pings.end();) {
        it->in -= dt;
        if (it->in > 0.0f) { ++it; continue; }
        VoiceDesc d;
        d.sound = it->sound;
        d.position = it->position;
        d.minDistance = 8.0f; // a cue, not a sound in the world: no falloff
        d.maxDistance = 60.0f;
        d.priority = 2.0f;
        d.category = SoundCategory::Alert;
        d.reverbSend = 0.3f;
        m_mixer->play(d);
        it = m_pings.erase(it);
    }
}

void AudioModule::updateRoom(float dt) {
    if (!settings.reverb) {
        m_mixer->setRoom(0.3f, 0.5f, 0.0f, 0.0f);
        return;
    }
    if ((m_roomProbeIn -= dt) > 0.0f) return;
    m_roomProbeIn = settings.roomProbeInterval;
    if (!roomRay) return;
    m_room = probeRoom(m_mixer->listener().position, roomRay, m_materials);
    m_mixer->setRoom(m_room.rt60, m_room.damping, m_room.wet, m_room.preDelay);
}

bool AudioModule::findOpening(const glm::vec3& listener, const glm::vec3& source, glm::vec3& via, float& pathLength) const {
    if (m_room.openings.empty() || !occlusionQuery) return false;
    glm::vec3 toSource = source - listener;
    toSource.y = 0.0f;
    const float flat = glm::length(toSource);
    if (flat < 1e-3f) return false;
    toSource /= flat;
    // The openings most in the sound's direction first; 3 of them, 2
    // distances each: at most 6 rays per occluded sound per recheck.
    std::vector<glm::vec3> dirs = m_room.openings;
    std::sort(dirs.begin(), dirs.end(), [&](const glm::vec3& a, const glm::vec3& b) { return glm::dot(a, toSource) > glm::dot(b, toSource); });
    bool found = false;
    float best = 0.0f;
    for (size_t i = 0; i < std::min<size_t>(3, dirs.size()); ++i) {
        if (glm::dot(dirs[i], toSource) < -0.2f) break; // behind you: not the way the sound comes
        for (float k : {3.0f, 6.0f}) {
            const glm::vec3 p = listener + dirs[i] * k;
            if (occlusionQuery(listener, p) < 0.99f) continue; // the opening isn't that far out
            if (occlusionQuery(p, source) < 0.99f) continue;
            const float len = k + glm::length(source - p);
            if (!found || len < best) {
                found = true;
                best = len;
                via = p;
            }
        }
    }
    pathLength = best;
    return found;
}

void AudioModule::handleContacts() {
#if KKE_ENABLE_JOLT
    auto* rb = m_app->getModule<RigidBodyModule>();
    if (!rb) return;
    const auto& contacts = rb->frameContacts();
    if (contacts.empty()) return;
    // Strongest first, capped: a pile landing reports hundreds of contacts
    // in one frame, and only the loudest few are audible anyway.
    std::vector<const RigidWorld::Contact*> sorted;
    sorted.reserve(contacts.size());
    for (const auto& c : contacts)
        if (c.speed > settings.impactThreshold) sorted.push_back(&c);
    std::sort(sorted.begin(), sorted.end(), [](auto* a, auto* b) { return a->speed > b->speed; });
    int played = 0;
    for (const RigidWorld::Contact* c : sorted) {
        const uint64_t lo = std::min(c->a, c->b), hi = std::max(c->a, c->b);
        const uint64_t key = (hi << 32) | lo;
        auto it = m_pairLastSound.find(key);
        if (played >= settings.maxImpactsPerFrame || (it != m_pairLastSound.end() && m_time - it->second < settings.pairCooldown)) {
            ++m_impactsSkipped;
            continue;
        }
        m_pairLastSound[key] = m_time;
        const float intensity = impactIntensity(c->speed, settings.impactThreshold, settings.impactFullSpeed);
        const uint32_t seed = uint32_t(key * 0x9E3779B97F4A7C15ull >> 40) ^ m_seedCounter++;
        // Both objects ring: wood crate on stone = a knock and a thud.
        playImpact(c->point, c->materialA, intensity, seed, 0.7f);
        if (c->materialB != c->materialA) playImpact(c->point, c->materialB, intensity, seed + 1, 0.7f);
        ++m_impactsPlayed;
        ++played;
    }
    if (m_pairLastSound.size() > 4096) {
        for (auto i = m_pairLastSound.begin(); i != m_pairLastSound.end();)
            i = (m_time - i->second > 1.0) ? m_pairLastSound.erase(i) : std::next(i);
    }
#endif
}

void AudioModule::handleBreaks() {
#if KKE_ENABLE_FEMFX
    auto* phys = m_app->getModule<PhysicsModule>();
    if (!phys) return;
    int n = 0;
    for (const PhysicsModule::BreakEvent& e : phys->frameBreaks()) {
        if (n++ >= settings.maxImpactsPerFrame) break;
        // A break is a crack: a hard hit of the material, plus a few quick
        // smaller ones for the pieces coming apart (more pieces, more).
        const uint32_t mat = audioMaterialFor(e.material);
        const uint32_t seed = m_seedCounter++;
        playImpact(e.position, mat, 1.0f, seed, 1.0f);
        const int extra = int(std::min<uint32_t>(e.newPieces, 3));
        for (int k = 0; k < extra; ++k) {
            const glm::vec3 off(float(int((seed + k * 7) % 5)) * 0.05f - 0.1f, 0.0f, float(int((seed + k * 3) % 5)) * 0.05f - 0.1f);
            playImpact(e.position + off * std::max(0.2f, e.size), mat, 0.45f, seed + 11 + k, 0.6f);
        }
        ++m_breaksPlayed;
    }
#endif
}

void AudioModule::updateOcclusion(float dt) {
    const Listener l = m_mixer->listener();
    std::vector<ActiveSound> active = m_mixer->activeSounds();
    std::unordered_map<uint32_t, Tracked> still;
    for (const ActiveSound& s : active) {
        Tracked t = m_tracked.count(s.id) ? m_tracked[s.id] : Tracked{};
        t.recheckIn -= dt;
        if (s.spatial && t.recheckIn <= 0.0f) {
            float through = settings.occlusion && occlusionQuery ? occlusionQuery(l.position, s.position) : 1.0f;
            glm::vec3 via;
            float path = 0.0f;
            if (through < 0.99f && settings.openings && findOpening(l.position, s.position, via, path)) {
                // Around the wall through the opening: from its direction,
                // a little duller (it bent round an edge), the longer way.
                m_mixer->setVia(s.id, via, path);
                through = std::max(through, 0.7f);
            } else {
                m_mixer->clearVia(s.id);
            }
            m_mixer->setTransmission(s.id, through);
            t.recheckIn = 0.1f;
        }
        still[s.id] = t;
    }
    m_tracked.swap(still);
}

void AudioModule::update(const UpdateContext& ctx) {
    m_time += ctx.dt;
    Listener l;
    if (listenerOverride) {
        l = listenerOverride();
    } else {
        const Camera& cam = m_app->camera();
        l.position = cam.position;
        l.forward = cam.target - cam.position;
        l.up = cam.up;
    }
    m_mixer->setListener(l);
    handleContacts();
    handleBreaks();
    if (tour && (m_tourTimer -= ctx.dt) <= 0.0f) {
        m_tourTimer = 0.6f;
        const glm::vec3 fwd = glm::normalize(l.forward);
        const glm::vec3 right = glm::normalize(glm::cross(fwd, l.up));
        const float az = glm::half_pi<float>() * float(m_tourStep % 4);
        auto it = std::next(m_materials.all().begin(), m_tourStep % int(m_materials.all().size()));
        playImpact(l.position + (fwd * std::cos(az) + right * std::sin(az)) * 4.0f, it->first, 0.8f);
        ++m_tourStep;
    }
    updateRoom(ctx.dt);
    updatePings(ctx.dt);
    updateOcclusion(ctx.dt);

    if (!m_deviceRunning) {
        // Silent mode: consume audio at real-time speed so sounds end and
        // the visualizer sees the same loudness it would with a device.
        m_silentBacklog = std::min(m_silentBacklog + double(ctx.dt) * settings.sampleRate, double(settings.sampleRate) * 0.25);
        const int frames = int(m_silentBacklog);
        if (frames > 0) {
            m_scratch.resize(size_t(frames) * 2);
            m_mixer->mix(m_scratch.data(), frames);
            m_silentBacklog -= frames;
        }
    }
}

SoundHandle AudioModule::loadSound(const std::string& path) {
    ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 1, ma_uint32(settings.sampleRate));
    ma_uint64 frames = 0;
    void* data = nullptr;
    if (ma_decode_file(path.c_str(), &cfg, &frames, &data) != MA_SUCCESS) {
        log::get(name())->warn("Could not decode sound '{}'", path);
        return nullptr;
    }
    auto buf = std::make_shared<SoundBuffer>();
    buf->sampleRate = settings.sampleRate;
    buf->samples.assign(static_cast<float*>(data), static_cast<float*>(data) + frames);
    ma_free(data, nullptr);
    return buf;
}

void AudioModule::renderUi() {
    const float s = ImGui::GetFontSize() / 13.0f;
    ImGui::SetNextWindowSize(ImVec2(300 * s, 0), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Audio")) { ImGui::End(); return; }
    ImGui::TextWrapped("Output: %s", m_deviceName.c_str());
    ImGui::Text("Voices: %zu / %d  (dropped %llu, stolen %llu)", m_mixer->voiceCount(), m_mixer->maxVoices(),
                (unsigned long long)m_mixer->droppedCount(), (unsigned long long)m_mixer->stolenCount());
    ImGui::Text("Impacts: %llu played, %llu skipped; breaks: %llu", (unsigned long long)m_impactsPlayed,
                (unsigned long long)m_impactsSkipped, (unsigned long long)m_breaksPlayed);
    ImGui::Text("Impact cache: %zu sounds, %.1f KB", m_bank->cachedCount(), double(m_bank->cachedBytes()) / 1024.0);
    ImGui::SliderFloat("Master", &m_mixer->masterGain, 0.0f, 2.0f);
    if (ImGui::TreeNode("Categories")) {
        for (int c = 0; c < int(SoundCategory::Count); ++c)
            ImGui::SliderFloat(soundCategoryName(SoundCategory(c)), &m_mixer->categoryGain[c], 0.0f, 2.0f);
        ImGui::TreePop();
    }
    ImGui::Checkbox("Occlusion (walls muffle)", &settings.occlusion);
    ImGui::Checkbox("Room reverb (ray traced)", &settings.reverb);
    ImGui::SameLine();
    ImGui::Checkbox("Openings", &settings.openings);
    ImGui::Text("Room: %.0f%% enclosed, RT60 %.2f s, wet %.2f, %zu openings", double(m_room.enclosure * 100.0f), double(m_room.rt60),
                double(m_room.wet), m_room.openings.size());
    int mode = int(settings.spatial);
    const char* modes[] = {spatialModeName(SpatialMode::Stereo), spatialModeName(SpatialMode::Binaural)};
    if (ImGui::Combo("Spatial", &mode, modes, 2)) {
        settings.spatial = SpatialMode(mode);
        m_mixer->setSpatialMode(settings.spatial);
    }
    ImGui::Checkbox("UI sounds", &settings.earcons);
    ImGui::SameLine();
    if (ImGui::Button("Ping surroundings")) ping();
    ImGui::Text("Footsteps: %llu", (unsigned long long)m_footsteps);
    ImGui::Checkbox("Sound tour (materials around you)", &tour);
    if (auto* vis = m_app->getModule<SoundVisualizerModule>()) {
        ImGui::Checkbox("Show sounds on screen", &vis->settings.enabled);
        ImGui::SameLine();
        if (ImGui::SmallButton("Customize...")) vis->showPanel = true;
    }
    ImGui::SliderFloat("Impact threshold (m/s)", &settings.impactThreshold, 0.1f, 5.0f);
    ImGui::Separator();
    ImGui::TextUnformatted("Hear each material (2 m ahead):");
    const Listener l = m_mixer->listener();
    const glm::vec3 ahead = l.position + glm::normalize(l.forward) * 2.0f;
    int n = 0;
    for (const auto& [id, mat] : m_materials.all()) {
        if (n++ % 4) ImGui::SameLine();
        if (ImGui::Button(mat.name.c_str())) playImpact(ahead, id, 0.7f);
    }
    ImGui::End();
}

void AudioModule::shutdown() {
    if (m_shutDown) return;
    m_shutDown = true; // the destructor calls this again, after logging is gone
    if (m_mixer && (m_impactsPlayed || m_impactsSkipped || m_breaksPlayed))
        log::get(name())->info("Audio: {} impacts played, {} skipped (cooldown/cap), {} breaks, voices dropped {} stolen {}", m_impactsPlayed,
                               m_impactsSkipped, m_breaksPlayed, m_mixer->droppedCount(), m_mixer->stolenCount());
    if (m_device) {
        if (m_deviceRunning) ma_device_uninit(&m_device->device);
        m_device.reset();
        m_deviceRunning = false;
    }
    m_deviceRunning = false;
}

} // namespace kke
