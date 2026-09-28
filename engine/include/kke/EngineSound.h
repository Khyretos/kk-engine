#pragma once

#include <cstddef>
#include <cstdint>

namespace kke {

// A car's sound, synthesized: the engine and the tyres, from nothing but
// the revs, the throttle and how much the tyres slide. No recordings, so
// every car, engine size and rev range sounds right, and it costs a few
// multiplies per sample.
//
// The engine follows Andy Farnell's model (Designing Sound, MIT Press
// 2010, "Practical 24: Motors / Cars"), the one most procedural engine
// sounds are built on: each cylinder firing is a short pulse of pressure
// into the exhaust pipe, and the pipe rings at its own resonances. So:
//   - firing pulses at revs/60 x cylinders/2 per second, each cylinder a
//     little louder or softer than the next and, for a lumpy V8, a little
//     early or late (the "burble" of a cross-plane crank);
//   - through two resonant band-passes (the pipe) plus a darker direct
//     path (the block), brighter and louder under throttle;
//   - intake and mechanical noise, pulsing with the firings;
//   - off the throttle at high revs, the odd pop from the exhaust.
// The tyres: noise through a narrow band-pass around 1 kHz that wavers,
// louder the more they slide (a squeal), and a low rumble with speed.
//
//   kke::EngineSound car(kke::EngineSound::v8());
//   car.set(rpm, throttle, slide, speed);           // each frame
//   car.render(buffer.data(), count, 48000);        // mono, adds nothing: overwrites
//
// Feed it into an AudioStream played by a spatial voice to hear it from
// the car (games/racing/Sound.cpp, docs/AUDIO.md "Engines and tyres").
class EngineSound {
public:
    struct Params {
        int cylinders = 4;
        float idleRpm = 900.0f, maxRpm = 7000.0f;
        float exhaustHz = 150.0f;   // the pipe's main resonance; its second is ~2.7x
        float unevenness = 0.05f;   // 0..0.3: firing-time wobble per cylinder (a V8's lope)
        float roughness = 0.2f;     // 0..1: loudness difference between cylinders
        float brightness = 0.5f;    // 0..1: how much top end under full throttle
        float intakeNoise = 0.25f;  // 0..1
        float pops = 0.5f;          // 0..1: exhaust pops off the throttle at high revs
        float gain = 1.0f;
        uint32_t seed = 1;          // the per-cylinder differences (two cars of a kind differ)
    };
    // Presets for common engines.
    static Params inline4();
    static Params inline6();
    static Params v8();
    static Params v10();

    explicit EngineSound(const Params& p = inline4());
    void setParams(const Params& p);
    const Params& params() const { return m_p; }

    // rpm: the engine's revs; throttle 0..1; slide 0..1: how hard the
    // tyres slide (0 = rolling, 1 = a full drift or lock-up); speed m/s
    // (road rumble). Changes glide over the next render, no clicks.
    void set(float rpm, float throttle, float slide, float speed);
    // Writes `count` mono samples.
    void render(float* out, size_t count, int sampleRate);

    // A band-pass or low-pass (Robert Bristow-Johnson's cookbook biquad).
    struct Biquad {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        void bandPass(float hz, float q, int sampleRate);
        void lowPass(float hz, float q, int sampleRate);
        float run(float x) {
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };

private:
    float noise();
    Params m_p;
    float m_cylGain[16] = {}, m_cylTime[16] = {};
    // Targets (set) and where the last render ended (glides between).
    float m_rpm = 900.0f, m_throttle = 0.0f, m_slide = 0.0f, m_speed = 0.0f;
    float m_rpmNow = 900.0f, m_throttleNow = 0.0f, m_slideNow = 0.0f, m_speedNow = 0.0f;
    double m_phase = 0.0; // 0..1 to the next firing
    int m_cylinder = 0;
    float m_pulse = 0.0f;  // the current firing's pressure, decaying
    float m_pop = 0.0f;
    float m_squealWobble = 0.0f;
    Biquad m_pipe1, m_pipe2, m_block, m_intake, m_squeal, m_rumble;
    int m_filterRate = 0;
    float m_filterBright = -1.0f;
    uint32_t m_rng = 1;
};

} // namespace kke
