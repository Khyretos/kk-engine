// Red Light, Green Light: run for the line while the giant doll sings
// with its back turned; when it turns round, freeze. Anyone still moving
// once it's looking is out. The first over the line win the most.
//
// The doll's schedule (how long each green and red lasts) comes from the
// round's seed, so it turns at the same moment on every machine; each
// machine watches its own beans.

#include "Common.h"

#include "kke/ImpactSynth.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace party {

namespace {

using namespace common;

constexpr float kFieldHalfX = 12.0f;
constexpr float kStartZ = 4.0f;
constexpr float kLineZ = -58.0f;
constexpr float kDollZ = -66.0f;
constexpr float kTurn = 0.45f;     // s the doll takes to turn round (a warning)
constexpr float kGrace = 0.3f;     // s after it's round before moving counts
constexpr float kStill = 0.6f;     // m/s: slower than this is standing still

class RedLight final : public Minigame {
public:
    const char* id() const override { return "red_light"; }
    const char* title() const override { return "Red Light, Green Light"; }
    const char* goal() const override { return "Run while the doll isn't looking. When it turns round, FREEZE: move and you're out!"; }
    std::string controls() const override { return "{move} run  ·  let go to freeze"; }
    const char* mood() const override { return "golden_hour"; }
    float timeLimit() const override { return 75.0f; }
    float killY() const override { return -6.0f; }
    float bumpStrength() const override { return 1.5f; }

    void build(Arena& a) override {
        Rng& rng = a.rng();
        // Green, red, green, red... Greens shorten as it goes on.
        m_switches.clear();
        float t = rng.range(2.5f, 4.0f);
        bool green = true;
        while (t < timeLimit() + 5.0f) {
            m_switches.push_back(t);
            green = !green;
            t += green ? std::max(1.2f, rng.range(2.0f, 4.5f) - t * 0.02f) : rng.range(1.6f, 3.2f);
        }
        m_lastRed = false;
        // The field, sand with a line; walls round it.
        a.staticBox({ 0.0f, -0.5f, (kStartZ + kDollZ) * 0.5f + 1.0f }, { kFieldHalfX, 0.5f, (kStartZ - kDollZ) * 0.5f + 4.0f }, glm::vec3(0.86f, 0.74f, 0.55f));
        for (int s = -1; s <= 1; s += 2)
            a.staticBox({ static_cast<float>(s) * (kFieldHalfX + 0.5f), 2.0f, (kStartZ + kDollZ) * 0.5f + 1.0f }, { 0.5f, 2.5f, (kStartZ - kDollZ) * 0.5f + 4.0f },
                        glm::vec3(0.93f, 0.6f, 0.62f));
        a.staticBox({ 0.0f, 2.0f, kStartZ + 3.5f }, { kFieldHalfX, 2.5f, 0.5f }, glm::vec3(0.93f, 0.6f, 0.62f));
        a.staticBox({ 0.0f, 2.0f, kDollZ - 4.5f }, { kFieldHalfX, 2.5f, 0.5f }, glm::vec3(0.93f, 0.6f, 0.62f));
        MeshBuilder m = a.levelMesh();
        m.box({ 0.0f, 0.01f, kLineZ }, { kFieldHalfX, 0.01f, 0.15f }, glm::vec3(0.9f, 0.15f, 0.15f));
        m.box({ 0.0f, 0.01f, kStartZ - 1.0f }, { kFieldHalfX, 0.01f, 0.08f }, glm::vec3(1.0f));
        // A tree behind the doll.
        m.cylinder({ 4.0f, 0.0f, kDollZ - 2.5f }, 0.35f, 0.25f, 5.0f, glm::vec3(0.4f, 0.28f, 0.18f), 10);
        m.ellipsoid({ 4.0f, 5.5f, kDollZ - 2.5f }, { 2.4f, 1.6f, 2.4f }, glm::vec3(0.35f, 0.55f, 0.25f), 14, 8);
        // The doll: a giant bean with pigtails (a bow), facing us or not.
        std::vector<kke::Vertex> v;
        std::vector<uint32_t> idx;
        buildBean(BeanLook{ 9, 3, 1, 8 }, v, idx);
        m_doll = a.addVisual(std::move(v), std::move(idx));
        placeDoll(a, 0.0f);
    }

    void spawn(Arena& a, int index, int count, glm::vec3& feet, float& yaw) override {
        (void)a;
        const float span = std::min(2.0f * kFieldHalfX - 3.0f, 1.6f * static_cast<float>(count));
        feet = { count <= 1 ? 0.0f : -span * 0.5f + span * static_cast<float>(index) / static_cast<float>(count - 1), 0.02f, kStartZ };
        yaw = 0.0f;
    }

    void update(Arena& a, float dt) override {
        (void)dt;
        const float t = a.playing() ? a.time() : 0.0f;
        const bool red = switchIndex(t) % 2 == 1;
        const float since = t - lastSwitch(t);
        // It turns round over kTurn (the warning), then it's looking.
        const float turn = std::clamp(since / kTurn, 0.0f, 1.0f);
        placeDoll(a, switchIndex(t) == 0 ? 0.0f : red ? 180.0f * turn : 180.0f * (1.0f - turn));
        if (!a.playing()) return;
        if (red != m_lastRed) {
            m_lastRed = red;
            a.flash(red ? "RED LIGHT!" : "GREEN LIGHT!", 1.0f);
            a.tone(static_cast<int>(red ? kke::Earcon::Error : kke::Earcon::ToggleOn));
        }
        a.status(red ? "Freeze!" : "Go go go!");
        kke::RigidWorld& w = a.world();
        for (Bean& b : a.beans()) {
            if (b.remote || !b.active || b.hidden) continue;
            const glm::vec3 p = b.feet(w);
            if (p.z < kLineZ) {
                a.finish(b);
                continue;
            }
            b.result.score = std::max(b.result.score, kStartZ - p.z); // how far (for the ones who don't make it)
            // Caught moving: a burst of paint, and out.
            const glm::vec3 vel = w.characterVelocity(b.id);
            if (red && since > kTurn + kGrace && glm::length(glm::vec2(vel.x, vel.z)) > kStill) {
                a.burst(p + glm::vec3(0.0f, 0.7f, 0.0f), glm::vec3(1.0f, 0.2f, 0.25f), 30, 3.0f);
                a.sound(p, kke::AudioMaterialTable::Rubber, 1.0f);
                a.knock(b, glm::vec3(0.0f, 3.0f, 2.0f), 2.0f);
                a.eliminate(b, "moved on a red light");
            }
        }
    }

    bool over(Arena& a) const override { return a.activeCount() == 0; }

    std::string beanStatus(Arena& a, const Bean& b) const override {
        (void)a;
        if (b.finished) return "Made it!";
        return std::to_string(static_cast<int>(std::max(0.0f, kStartZ - kLineZ - b.result.score))) + " m to go";
    }

    BeanInput bot(Arena& a, Bean& b, float dt) override {
        (void)dt;
        BeanInput in;
        const float t = a.time();
        const glm::vec3 p = b.feet(a.world());
        // A new reaction time at every switch: better bots react faster,
        // and everyone is slow now and then.
        const int n = switchIndex(t);
        if (b.i != n + 1) {
            b.i = n + 1;
            b.a = a.botRng().range(0.05f, 0.3f) + (1.0f - skill(b)) * a.botRng().range(0.0f, 1.0f);
            if (b.b == 0.0f) b.b = a.botRng().range(-3.0f, 3.0f); // its lane
        }
        if (n % 2 == 0 && t - lastSwitch(t) >= b.a * 0.5f)
            in.move = steer(p, { std::clamp(b.b, -kFieldHalfX + 1.0f, kFieldHalfX - 1.0f), 0.0f, kLineZ - 3.0f });
        else if (n % 2 == 1 && t - lastSwitch(t) < b.a)
            in.move = steer(p, { p.x, 0.0f, kLineZ - 3.0f }); // hasn't noticed yet
        return in;
    }

private:
    std::vector<float> m_switches; // green -> red -> green ... at these times
    int m_doll = -1;
    bool m_lastRed = false;

    // How many switches have happened by t (even: green, odd: red), and when the last was.
    int switchIndex(float t) const {
        int n = 0;
        for (float s : m_switches)
            if (t >= s) ++n;
        return n;
    }
    float lastSwitch(float t) const {
        float last = 0.0f;
        for (float s : m_switches)
            if (t >= s) last = s;
        return last;
    }

    void placeDoll(Arena& a, float degrees) {
        // 0: its back to the players (facing -Z); 180: looking at them.
        a.part(m_doll).transform = glm::scale(glm::rotate(glm::translate(glm::mat4(1.0f), { 0.0f, 0.0f, kDollZ }), glm::radians(-degrees), glm::vec3(0, 1, 0)),
                                              glm::vec3(4.0f));
    }
};

} // namespace

std::unique_ptr<Minigame> makeRedLight() { return std::make_unique<RedLight>(); }

} // namespace party
