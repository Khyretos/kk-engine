#include "kke/modules/AudioModule.h"

#include "kke/Application.h"
#include "kke/Log.h"
#if KKE_ENABLE_STEAM_AUDIO
#include "kke/SteamAudioSpatializer.h"
#endif
#include "kke/WavFile.h"
#include "kke/modules/InputModule.h"
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
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <utility>
#include <iterator>
#if defined(__linux__)
#include <dlfcn.h>
#include <cstdarg>
#include <cstdio>
#endif

namespace kke {

struct AudioModule::Device {
    ma_device device{};
};

namespace {
void dataCallback(ma_device* device, void* output, const void*, ma_uint32 frameCount) {
    auto* mixer = static_cast<AudioMixer*>(device->pUserData);
    mixer->mix(static_cast<float*>(output), int(frameCount));
}

#if defined(__linux__)
// On a machine with libasound but no sound card (CI, containers, servers)
// miniaudio's ALSA probe makes libasound print a dozen "ALSA lib ... cannot
// find card '0'" lines straight to stderr before it falls back. Route
// libasound's messages into the engine log at debug level instead; the
// outcome (the device we got, or "running silent") is logged by init().
__attribute__((format(printf, 5, 6)))
void alsaMessage(const char* file, int line, const char* function, int err, const char* fmt, ...) {
    char text[512];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);
    log::get("Audio")->debug("ALSA {}:{} {}: {} ({})", file, line, function, text, err);
}

void routeAlsaMessages() {
    using Handler = void (*)(const char*, int, const char*, int, const char*, ...);
    // Kept open: the handler is process-wide in libasound, which miniaudio
    // opens (and may close) on its own.
    static void* lib = dlopen("libasound.so.2", RTLD_NOW | RTLD_GLOBAL);
    if (!lib) return; // no ALSA here: nothing to quiet
    using SetHandler = int (*)(Handler);
    if (auto set = reinterpret_cast<SetHandler>(dlsym(lib, "snd_lib_error_set_handler")))
        set(&alsaMessage);
}
#endif
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
#if defined(__linux__)
        routeAlsaMessages();
#endif
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
    if (const char* h = std::getenv("KKE_AUDIO_HRTF"); h && *h && std::strcmp(h, "0") != 0) {
        settings.spatial = SpatialMode::Hrtf;
        if (std::strcmp(h, "1") != 0) settings.hrtfSofaFile = h; // a SOFA file
    }
    setSpatialMode(settings.spatial);
    if (const char* r = std::getenv("KKE_AUDIO_RECORD"); r && *r) startRecording(r);
    log->info("Audio ready: {} Hz, {} voices, output: {}", settings.sampleRate, settings.maxVoices, m_deviceName);

#if KKE_ENABLE_JOLT
    if (!occlusionQuery) {
        // Every wall between the two, each by its material and thickness
        // (materials' transmission is for ~30 cm): a thin wood partition
        // lets more through than a thick one, two walls less than one.
        // Two rays per wall: in, and back from the far side for the
        // thickness. Up to 4 walls; past that it is silent anyway.
        occlusionQuery = [this](const glm::vec3& from, const glm::vec3& to) -> float {
            auto* rb = m_app ? m_app->getModule<RigidBodyModule>() : nullptr;
            if (!rb) return 1.0f;
            const glm::vec3 d = to - from;
            const float dist = glm::length(d);
            if (dist < 0.5f) return 1.0f;
            const glm::vec3 dir = d / dist;
            const RigidWorld& world = rb->world();
            RigidWorld::BodyId seen[4];
            int nSeen = 0;
            const std::function<bool(RigidWorld::BodyId, RigidWorld::Motion)> notSeen = [&](RigidWorld::BodyId b, RigidWorld::Motion) {
                for (int i = 0; i < nSeen; ++i)
                    if (seen[i] == b) return false;
                return true;
            };
            float through = 1.0f, travelled = 0.0f;
            while (nSeen < 4 && through > 0.005f) {
                ++m_raysThisFrame;
                const RigidWorld::RayHit hit = world.raycast(from + dir * travelled, dir, dist - travelled, notSeen);
                // The sounding object itself is usually hit right at the end.
                if (!hit.hit || travelled + hit.distance > dist - 0.7f) break;
                const RigidWorld::BodyId wall = hit.body;
                const std::function<bool(RigidWorld::BodyId, RigidWorld::Motion)> only = [wall](RigidWorld::BodyId b, RigidWorld::Motion) {
                    return b == wall;
                };
                const float entry = travelled + hit.distance;
                ++m_raysThisFrame;
                const RigidWorld::RayHit back = world.raycast(to, -dir, dist - entry, only);
                const float thickness = back.hit ? std::max(0.01f, dist - back.distance - entry) : 0.3f;
                through *= std::pow(m_materials.get(hit.material).transmission, std::clamp(thickness / 0.3f, 0.5f, 3.0f));
                seen[nSeen++] = wall;
                travelled = entry + thickness;
            }
            return through;
        };
    }
    if (!roomRay) {
        roomRay = [this](const glm::vec3& from, const glm::vec3& dir, float maxDistance) -> AcousticRay {
            auto* rb = m_app ? m_app->getModule<RigidBodyModule>() : nullptr;
            if (!rb) return {};
            ++m_raysThisFrame;
            const RigidWorld::RayHit hit = rb->world().raycast(from, dir, maxDistance);
            return AcousticRay{hit.hit, hit.distance, hit.material, hit.normal};
        };
    }
#endif
}

bool AudioModule::hrtfAvailable() { return KKE_ENABLE_STEAM_AUDIO != 0; }

SpatialMode AudioModule::setSpatialMode(SpatialMode mode) {
    if (mode == SpatialMode::Hrtf && !m_mixer->hasSpatializer()) {
        std::string why = "this build has no HRTF backend (configure with -DKKE_ENABLE_STEAM_AUDIO=ON)";
#if KKE_ENABLE_STEAM_AUDIO
        if (!m_hrtfFailed) {
            SteamAudioSpatializer::Settings ss;
            ss.sampleRate = settings.sampleRate;
            ss.maxVoices = settings.maxVoices;
            ss.sofaFile = settings.hrtfSofaFile;
            if (auto sp = SteamAudioSpatializer::create(ss, &why)) {
                m_mixer->setSpatializer(sp);
                log::get(name())->info("Steam Audio HRTF ready ({}), {} voices, {}-sample frames",
                                       ss.sofaFile.empty() ? std::string("default HRTF") : ss.sofaFile, ss.maxVoices, ss.frameSize);
            }
        } else {
            why = "Steam Audio failed to start earlier";
        }
#endif
        if (!m_mixer->hasSpatializer()) {
            if (!m_hrtfFailed) log::get(name())->warn("HRTF unavailable, using Binaural: {}", why);
            m_hrtfFailed = true;
            mode = SpatialMode::Binaural;
        }
    }
    settings.spatial = mode;
    m_mixer->setSpatialMode(mode);
    return mode;
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
    return playTracked(d);
}

uint32_t AudioModule::play(const VoiceDesc& desc) { return m_mixer ? playTracked(desc) : 0; }

uint32_t AudioModule::playTracked(VoiceDesc d) {
    if (d.spatial) {
        const SoundPath p = soundPath(m_mixer->listener().position, d.position);
        d.transmission = p.transmission;
        d.viaOpening = p.viaOpening;
        d.via = p.via;
        d.viaPathLength = p.pathLength;
    }
    const uint32_t id = m_mixer->play(d);
    if (id) m_tracked[id] = Tracked{0.1f}; // just worked out: the next check is a recheck
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
    const uint32_t id = playTracked(d);
    if (id) ++m_footsteps;
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
        m_mixer->setEchoes({});
        return;
    }
    const glm::vec3 at = m_mixer->listener().position;
    // Somewhere new (a teleport, a respawn, the first frame): probe now,
    // four times round, so the room is right before the first sound.
    const bool moved = m_probeCount == 0 || glm::length(at - m_lastProbeAt) > m_tracker.settings.snapDistance;
    if ((m_roomProbeIn -= dt) > 0.0f && !moved) return;
    m_roomProbeIn = settings.roomProbeInterval;
    if (!roomRay) return;
    m_lastProbeAt = at;
    // Each probe turned by the golden angle from the last: over a few
    // probes the rays cover every direction, so doors narrower than the
    // gap between two rays are found, for the cost of one probe.
    for (int i = 0; i < (moved ? 4 : 1); ++i) {
        RoomProbeSettings ps;
        ps.rotation = float(m_probeCount++) * 2.39996323f;
        m_tracker.update(at, probeRoom(at, roomRay, m_materials, ps), m_materials);
    }
    m_room = m_tracker.room();
    m_mixer->setRoom(m_room.rt60, m_room.damping, m_room.wet, m_room.preDelay);
    m_mixer->setEchoes(settings.echoes ? m_tracker.echoes(AudioMixer::kMaxEchoes, m_materials) : std::vector<EchoTap>{});
}

bool AudioModule::findOpening(const glm::vec3& listener, const glm::vec3& source, glm::vec3& via, float& pathLength) {
    if (m_room.openings.empty() || !occlusionQuery) return false;
    glm::vec3 toSource = source - listener;
    toSource.y = 0.0f;
    const float flat = glm::length(toSource);
    if (flat < 1e-3f) return false;
    toSource /= flat;
    // The openings most in the sound's direction first; 3 of them, 2
    // points each (just past the gap, and further out): at most 12
    // occlusion queries per occluded sound per recheck.
    std::vector<RoomOpening> ways = m_room.openings;
    std::sort(ways.begin(), ways.end(), [&](const RoomOpening& a, const RoomOpening& b) { return glm::dot(a.dir, toSource) > glm::dot(b.dir, toSource); });
    bool found = false;
    float best = 0.0f;
    size_t tried = 0;
    for (size_t i = 0; i < ways.size() && tried < 3; ++i) {
        const RoomOpening& o = ways[i];
        if (glm::dot(o.dir, toSource) < -0.2f) break; // behind you: not the way the sound comes
        if (i > 0 && glm::dot(o.dir, ways[i - 1].dir) > 0.98f) continue; // the same way, remembered twice
        ++tried;
        const float limit = std::max(0.5f, o.reach - 0.3f);
        const float ks[2] = {o.through > 0.0f ? o.through + 0.8f : 3.0f, o.through > 0.0f ? o.through + 3.0f : 6.0f};
        for (float k : ks) {
            k = std::min(k, limit);
            const glm::vec3 p = listener + o.dir * k;
            if (occlusionQuery(listener, p) < 0.99f) continue; // the way isn't free that far
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

AudioModule::SoundPath AudioModule::soundPath(const glm::vec3& listener, const glm::vec3& source) {
    SoundPath p;
    p.transmission = settings.occlusion && occlusionQuery ? occlusionQuery(listener, source) : 1.0f;
    if (p.transmission < 0.99f && settings.openings && findOpening(listener, source, p.via, p.pathLength)) {
        // Round the wall through the opening: from its direction, duller
        // the sharper it had to bend (diffraction loses the highs first),
        // and never quieter than straight through the wall.
        const glm::vec3 a = p.via - listener, b = source - p.via;
        const float la = glm::length(a), lb = glm::length(b);
        const float bend = la > 1e-3f && lb > 1e-3f ? std::acos(std::clamp(glm::dot(a, b) / (la * lb), -1.0f, 1.0f)) : 0.0f;
        p.transmission = std::max(p.transmission, std::clamp(0.95f - 0.45f * bend / glm::half_pi<float>(), 0.4f, 0.95f));
        p.viaOpening = true;
    }
    return p;
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

void AudioModule::handleSoftImpacts() {
#if KKE_ENABLE_FEMFX
    auto* phys = m_app->getModule<PhysicsModule>();
    if (!phys) return;
    std::vector<const PhysicsModule::ImpactEvent*> sorted;
    for (const PhysicsModule::ImpactEvent& e : phys->frameImpacts())
        if (e.speed > settings.impactThreshold) sorted.push_back(&e);
    if (sorted.empty()) return;
    std::sort(sorted.begin(), sorted.end(), [](auto* a, auto* b) { return a->speed > b->speed; });
    int played = 0;
    for (const PhysicsModule::ImpactEvent* e : sorted) {
        // Soft-body pairs have their own key space (bit 63) next to Jolt's.
        const uint64_t key = e->pair | (1ull << 63);
        auto it = m_pairLastSound.find(key);
        if (played >= settings.maxImpactsPerFrame || (it != m_pairLastSound.end() && m_time - it->second < settings.pairCooldown)) {
            ++m_impactsSkipped;
            continue;
        }
        m_pairLastSound[key] = m_time;
        const float intensity = impactIntensity(e->speed, settings.impactThreshold, settings.impactFullSpeed);
        const uint32_t seed = uint32_t(key * 0x9E3779B97F4A7C15ull >> 40) ^ m_seedCounter++;
        const uint32_t a = audioMaterialFor(e->materialA);
        // FEMFX's floor is a plain plane: it sounds like stone.
        const uint32_t b = e->ground ? uint32_t(AudioMaterialTable::Stone) : audioMaterialFor(e->materialB);
        playImpact(e->position, a, intensity, seed, 0.7f);
        if (b != a) playImpact(e->position, b, intensity, seed + 1, 0.7f);
        ++m_impactsPlayed;
        ++played;
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
    // Most overdue first, while the frame's ray budget lasts; the rest keep
    // their last value until a later frame has room.
    std::vector<std::pair<float, const ActiveSound*>> due;
    for (const ActiveSound& s : active) {
        Tracked t = m_tracked.count(s.id) ? m_tracked[s.id] : Tracked{};
        t.recheckIn -= dt;
        if (s.spatial && t.recheckIn <= 0.0f) due.push_back({t.recheckIn, &s});
        still[s.id] = t;
    }
    std::sort(due.begin(), due.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (const auto& [late, s] : due) {
        (void)late;
        if (m_raysThisFrame >= settings.maxRaysPerFrame) break;
        const SoundPath p = soundPath(l.position, s->position);
        if (p.viaOpening) m_mixer->setVia(s->id, p.via, p.pathLength);
        else m_mixer->clearVia(s->id);
        m_mixer->setTransmission(s->id, p.transmission);
        still[s->id].recheckIn = 0.1f;
    }
    m_tracked.swap(still);
}

void AudioModule::update(const UpdateContext& ctx) {
    m_time += ctx.dt;
    m_raysLastFrame = std::exchange(m_raysThisFrame, 0);
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
    handleSoftImpacts();
    handleBreaks();
    if (auto* input = m_app->getModule<InputModule>(); input && input->map(0).action("audio.ping") && input->map(0).pressed("audio.ping"))
        ping();
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

void AudioModule::startRecording(const std::string& path) {
    if (!m_mixer || path.empty()) return;
    // A minute up front; the audio thread grows it (rarely) after that.
    m_mixer->startCapture(size_t(settings.sampleRate) * 60);
    m_recordPath = path;
    log::get(name())->info("Recording the audio output to {}", path);
}

bool AudioModule::stopRecording() {
    auto log = log::get(name());
    if (!m_mixer || m_recordPath.empty()) {
        log->warn("stopRecording: nothing is being recorded");
        return false;
    }
    const std::string path = std::exchange(m_recordPath, {});
    const std::vector<float> samples = m_mixer->stopCapture();
    std::string error;
    if (!writeWav(path, samples.data(), samples.size(), 2, settings.sampleRate, &error)) {
        log->error("Could not save the recording: {}", error);
        return false;
    }
    log->info("Saved {:.1f} s of audio to {}", double(samples.size() / 2) / settings.sampleRate, path);
    return true;
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
    ImGui::SameLine();
    ImGui::Checkbox("Echoes", &settings.echoes);
    ImGui::Checkbox("Air absorption", &m_mixer->airAbsorption);
    ImGui::Text("Room: %.0f%% enclosed, RT60 %.2f s, wet %.2f, %zu openings", double(m_room.enclosure * 100.0f), double(m_room.rt60),
                double(m_room.wet), m_room.openings.size());
    ImGui::Text("Rays last frame: %d (budget %d)", m_raysLastFrame, settings.maxRaysPerFrame);
    int mode = int(settings.spatial);
    const char* modes[] = {spatialModeName(SpatialMode::Stereo), spatialModeName(SpatialMode::Binaural), spatialModeName(SpatialMode::Hrtf)};
    if (ImGui::Combo("Spatial", &mode, modes, hrtfAvailable() ? 3 : 2)) setSpatialMode(SpatialMode(mode));
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
    if (m_mixer && (m_impactsPlayed || m_impactsSkipped || m_breaksPlayed || m_footsteps))
        log::get(name())->info("Audio: {} impacts played, {} skipped (cooldown/cap), {} breaks, {} footsteps, voices dropped {} stolen {}; room RT60 {:.2f} s wet {:.2f}",
                               m_impactsPlayed, m_impactsSkipped, m_breaksPlayed, m_footsteps, m_mixer->droppedCount(), m_mixer->stolenCount(),
                               m_room.rt60, m_room.wet);
    if (m_device) {
        if (m_deviceRunning) ma_device_uninit(&m_device->device);
        m_device.reset();
        m_deviceRunning = false;
    }
    m_deviceRunning = false;
    if (recording()) stopRecording(); // after the device: nothing mixes any more
}

} // namespace kke
