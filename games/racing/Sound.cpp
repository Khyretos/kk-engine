// Engines and tyres (kke::EngineSound, docs/AUDIO.md "Engines and tyres"):
// each of the nearest cars gets its own synthesized engine and tyre squeal,
// played from the car so you hear the pack around you, a car coming up
// behind, the one you just passed. The players' own cars always have one;
// the rest go to the CPU and online cars closest to the camera.

#include "RacingModule.h"

#include "kke/modules/AudioModule.h"

#include <algorithm>
#include <cmath>

namespace racing {

namespace {

constexpr size_t kEngineVoices = 8;    // cars heard at once (the mixer has 32 voices; crashes need the rest)
constexpr float kAssignEvery = 0.25f;  // s between choosing which cars they are
constexpr float kAheadSeconds = 0.1f;  // how far ahead of the speakers each engine is written

// The engine each car type has.
kke::EngineSound::Params engineFor(const CarType& t, uint32_t seed) {
    kke::EngineSound::Params p;
    const std::string id = t.id;
    if (id == "muscle") {
        p = kke::EngineSound::v8();
    } else if (id == "exotic") {
        p = kke::EngineSound::v10();
    } else if (id == "sports") {
        p = kke::EngineSound::inline6();
    } else if (id == "ute") {
        p = kke::EngineSound::inline6();
        p.exhaustHz = 125.0f; // a big, lazy six
        p.roughness = 0.3f;
        p.brightness = 0.35f;
    } else if (id == "sedan") {
        p = kke::EngineSound::inline4();
        p.exhaustHz = 160.0f;
        p.brightness = 0.5f;
    } else {
        p = kke::EngineSound::inline4(); // the hatch: a buzzy four
    }
    p.maxRpm = t.maxRpm;
    p.seed = seed;
    return p;
}

} // namespace

void RacingModule::updateSounds(float dt) {
    if (!m_audio) return;
    kke::AudioMixer& mixer = m_audio->mixer();
    const int rate = mixer.sampleRate();
    if (m_engines.empty()) m_engines.resize(kEngineVoices);

    // Which cars: players here first, then the nearest to the camera.
    m_engineAssign -= dt;
    if (m_engineAssign <= 0.0f) {
        m_engineAssign = kAssignEvery;
        const glm::vec3 ear = m_app->camera().position;
        std::vector<std::pair<float, int>> order;
        for (size_t i = 0; i < m_cars.size(); ++i) {
            const Car& c = m_cars[i];
            if (c.totalled) continue;
            const bool mine = c.seat >= 0 && !c.remote;
            order.emplace_back(mine ? -1.0f : glm::length(carPosition(c) - ear), static_cast<int>(i));
        }
        std::sort(order.begin(), order.end());
        if (order.size() > kEngineVoices) order.resize(kEngineVoices);
        // Cars that keep a voice keep the same one; the rest are handed out.
        std::vector<bool> wanted(m_cars.size(), false);
        for (const auto& o : order) wanted[static_cast<size_t>(o.second)] = true;
        std::vector<bool> placed(m_cars.size(), false);
        for (EngineVoice& v : m_engines)
            if (v.car >= 0 && static_cast<size_t>(v.car) < m_cars.size() && wanted[static_cast<size_t>(v.car)] && !placed[static_cast<size_t>(v.car)])
                placed[static_cast<size_t>(v.car)] = true;
            else
                v.car = -1;
        size_t next = 0;
        for (EngineVoice& v : m_engines) {
            if (v.car >= 0) continue;
            while (next < order.size() && placed[static_cast<size_t>(order[next].second)]) ++next;
            if (next == order.size()) break;
            v.car = order[next].second;
            placed[static_cast<size_t>(v.car)] = true;
        }
    }

    for (size_t n = 0; n < m_engines.size(); ++n) {
        EngineVoice& v = m_engines[n];
        const bool active = v.car >= 0 && static_cast<size_t>(v.car) < m_cars.size();
        if (!active) {
            if (v.voice) {
                mixer.stop(v.voice);
                v.voice = 0;
            }
            if (v.stream) v.stream->close();
            v.stream.reset();
            continue;
        }
        const Car& c = m_cars[static_cast<size_t>(v.car)];
        const CarType& type = carTypes()[static_cast<size_t>(c.type)];
        const uint32_t seed = static_cast<uint32_t>(v.car) * 7919u + 17u;
        if (v.type != c.type || v.seed != seed) {
            v.synth.setParams(engineFor(type, seed));
            v.type = c.type;
            v.seed = seed;
        }
        // A voice for it: started when needed (the mixer drops sounds out of range).
        if (!v.voice || !mixer.isPlaying(v.voice)) {
            if (v.stream) v.stream->close();
            v.stream = std::make_shared<kke::AudioStream>(static_cast<size_t>(rate) / 2);
            kke::VoiceDesc d;
            d.stream = v.stream;
            d.position = carPosition(c);
            d.spatial = true;
            d.minDistance = 5.0f;
            d.maxDistance = 160.0f;
            d.gain = c.seat >= 0 && !c.remote ? 0.9f : 0.75f;
            d.priority = c.seat >= 0 && !c.remote ? 3.0f : 1.5f;
            d.category = kke::SoundCategory::Ambient;
            d.reverbSend = 0.4f;
            v.voice = m_audio->play(d);
        } else {
            mixer.setPosition(v.voice, carPosition(c));
        }

        // What the engine and tyres are doing.
        const float speed = std::fabs(carSpeed(c));
        float rpm = 0.0f, throttle = 0.0f, slide = 0.0f;
        if (c.remote) {
            rpm = c.net.rpm * type.maxRpm;
            throttle = c.net.braking ? 0.0f : 0.7f;
            int smoking = 0;
            for (int w = 0; w < 4; ++w) smoking += (c.net.smoke >> w) & 1u;
            slide = static_cast<float>(smoking) / 4.0f;
        } else {
            rpm = c.state.rpm;
            throttle = std::fabs(c.input.throttle);
            for (const kke::VehicleWheelState& w : c.state.wheels) {
                if (!w.contact || speed < 2.0f) continue;
                const float sideways = std::clamp((std::fabs(w.lateralSlip) - 8.0f) / 25.0f, 0.0f, 1.0f);
                const float spinOrLock = std::clamp((w.longitudinalSlip - 0.3f) / 0.8f, 0.0f, 1.0f);
                slide = std::max(slide, std::max(sideways, spinOrLock));
            }
        }
        v.synth.set(rpm, throttle, slide, speed);

        // Keep the stream a little ahead of the speakers (more at a low frame rate).
        if (!v.stream) continue;
        const size_t want = static_cast<size_t>(static_cast<float>(rate) * std::clamp(std::max(kAheadSeconds, dt * 2.0f), kAheadSeconds, 0.3f));
        const size_t have = v.stream->buffered();
        if (have >= want) continue;
        m_engineScratch.resize(want - have);
        v.synth.render(m_engineScratch.data(), m_engineScratch.size(), rate);
        v.stream->push(m_engineScratch.data(), m_engineScratch.size());
    }
}

void RacingModule::stopSounds() {
    for (EngineVoice& v : m_engines) {
        if (v.stream) v.stream->close();
        if (m_audio && v.voice) m_audio->mixer().stop(v.voice);
    }
    m_engines.clear();
}

} // namespace racing
