// Flaming Jump Rope: everyone on a narrow bridge over lava, a burning rope
// swung round it by two giant beans. Jump as it sweeps under you; it gets
// faster, and now and then it swings the other way. Touch it and you're
// out; step off the bridge and you're out. The last bean jumping wins.
//
// The rope's angle is a formula of the clock from GO (and the round's
// seed), the same on every machine; each machine checks its own beans.

#include "Common.h"

#include "kke/ImpactSynth.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace party {

namespace {

using namespace common;

constexpr float kHalfLength = 7.0f;  // along X
constexpr float kHalfDepth = 1.6f;   // along Z: narrow, so the rope reaches everyone
constexpr float kAxisY = 2.45f, kRopeR = 2.3f; // the rope's circle: its lowest point 0.15 m over the bridge
constexpr float kRopeThick = 0.13f;

class JumpRope final : public Minigame {
public:
    const char* id() const override { return "jump_rope"; }
    const char* title() const override { return "Flaming Jump Rope"; }
    const char* goal() const override { return "Jump the burning rope! It gets faster. Touch it or fall in the lava and you're out. Last one jumping wins."; }
    std::string controls() const override { return "{jump} jump  ·  {move} shuffle along the bridge"; }
    const char* mood() const override { return "dusk"; }
    float timeLimit() const override { return 100.0f; }
    CameraStyle camera() const override { return CameraStyle::Overview; }
    float killY() const override { return -2.5f; }

    void overview(kke::Camera& cam) const override {
        cam.position = { 0.0f, 5.2f, 15.5f };
        cam.target = { 0.0f, 1.3f, 0.0f };
    }

    void build(Arena& a) override {
        Rng& rng = a.rng();
        m_speed0 = rng.range(2.1f, 2.5f);
        m_accel = rng.range(0.028f, 0.036f);
        m_flips.clear();
        // When it turns round: two or three times, from 25 s on.
        float t = rng.range(22.0f, 32.0f);
        while (t < timeLimit()) {
            m_flips.push_back(t);
            t += rng.range(14.0f, 24.0f);
        }
        // The bridge, over lava.
        a.staticBox({ 0.0f, -0.4f, 0.0f }, { kHalfLength, 0.4f, kHalfDepth }, glm::vec3(0.55f, 0.42f, 0.36f));
        MeshBuilder m = a.levelMesh();
        for (int k = 0; k < 14; ++k) m.box({ -kHalfLength + 0.5f + static_cast<float>(k), 0.005f, 0.0f }, { 0.46f, 0.01f, kHalfDepth - 0.05f },
                                            k % 2 ? glm::vec3(0.62f, 0.48f, 0.4f) : glm::vec3(0.58f, 0.44f, 0.37f));
        m.box({ 0.0f, -4.0f, 0.0f }, { 40.0f, 0.2f, 30.0f }, glm::vec3(1.0f, 0.35f, 0.05f)); // lava
        for (int k = 0; k < 30; ++k)
            m.ellipsoid({ rng.range(-30.0f, 30.0f), -3.85f, rng.range(-20.0f, 12.0f) }, { rng.range(0.6f, 1.6f), 0.12f, rng.range(0.6f, 1.6f) },
                        glm::vec3(1.0f, 0.75f, 0.2f), 10, 4);
        // Rock pillars the turners stand on.
        for (int s = -1; s <= 1; s += 2) {
            const float x = (kHalfLength + 1.9f) * static_cast<float>(s);
            a.staticBox({ x, -2.0f, 0.0f }, { 1.4f, 2.0f, 1.4f }, glm::vec3(0.35f, 0.3f, 0.3f));
        }
        // The two giant turners (big beans), each holding an end.
        for (int s = 0; s < 2; ++s) {
            std::vector<kke::Vertex> v;
            std::vector<uint32_t> idx;
            buildBean(BeanLook{ s ? 7 : 11, 1, 2, s ? 5 : 3 }, v, idx);
            const int p = a.addVisual(std::move(v), std::move(idx));
            const float x = (kHalfLength + 1.9f) * (s ? 1.0f : -1.0f);
            a.part(p).transform = glm::scale(glm::rotate(glm::translate(glm::mat4(1.0f), { x, 0.0f, 0.2f }), glm::radians(s ? -90.0f : 90.0f), glm::vec3(0, 1, 0)),
                                             glm::vec3(2.4f));
        }
        // The rope: along X, drawn turned about the axis.
        std::vector<kke::Vertex> v;
        std::vector<uint32_t> idx;
        MeshBuilder rope{ v, idx };
        const size_t from = v.size();
        rope.cylinder({ 0.0f, -kHalfLength - 1.4f, 0.0f }, kRopeThick, kRopeThick, 2.0f * (kHalfLength + 1.4f), glm::vec3(1.0f, 0.45f, 0.1f), 10);
        rope.transform(from, glm::rotate(glm::mat4(1.0f), -kPi * 0.5f, glm::vec3(0, 0, 1))); // Y -> X
        for (int s = -1; s <= 1; s += 2) rope.box({ (kHalfLength + 1.4f) * static_cast<float>(s), kRopeR * 0.5f, 0.0f }, { 0.08f, kRopeR * 0.5f, 0.08f }, glm::vec3(0.5f, 0.35f, 0.2f));
        m_rope = a.addVisual(std::move(v), std::move(idx));
        m_lastBottom = 0;
        std::fill(std::begin(m_flipShown), std::end(m_flipShown), false);
    }

    void spawn(Arena& a, int index, int count, glm::vec3& feet, float& yaw) override {
        (void)a;
        const float span = 2.0f * (kHalfLength - 1.0f);
        const float x = count <= 1 ? 0.0f : -span * 0.5f + span * static_cast<float>(index) / static_cast<float>(count - 1);
        feet = { x, 0.05f, 0.0f };
        yaw = 180.0f; // facing the camera
    }

    void update(Arena& a, float dt) override {
        (void)dt;
        const float t = a.time();
        const float angle = ropeAngle(t);
        // The rope, and its flames.
        glm::mat4 m = glm::translate(glm::mat4(1.0f), { 0.0f, kAxisY, 0.0f });
        m = glm::rotate(m, angle, glm::vec3(1, 0, 0));
        m = glm::translate(m, { 0.0f, -kRopeR, 0.0f });
        a.part(m_rope).transform = m;
        // Where it is (turning about +X: the rope's point is R_x(angle) * (0, -R, 0)).
        const glm::vec3 at(0.0f, kAxisY - std::cos(angle) * kRopeR, -std::sin(angle) * kRopeR);
        for (int k = 0; k < 6; ++k) a.flame(at + glm::vec3(a.botRng().range(-kHalfLength - 1.2f, kHalfLength + 1.2f), 0.0f, 0.0f), 1.2f);
        if (!a.playing()) return;
        // A whoosh each time it passes under the bridge.
        const int bottom = static_cast<int>(std::floor((angle + kPi) / (2.0f * kPi)));
        if (bottom != m_lastBottom) {
            m_lastBottom = bottom;
            a.sound({ 0.0f, 0.3f, 0.0f }, kke::AudioMaterialTable::Dirt, 0.35f);
        }
        const float speed = std::abs(ropeSpeed(t));
        if (flipped(t, dt)) a.flash("It turned round!", 1.4f);
        a.status(speed > 4.0f ? "Faster!" : "Jump!");
        kke::RigidWorld& w = a.world();
        for (Bean& b : a.beans()) {
            if (b.remote || !b.active || b.hidden) continue;
            const glm::vec3 p = b.feet(w);
            if (std::abs(at.z - p.z) < kRopeThick + kBeanRadius * 0.85f && at.y > p.y - 0.05f && at.y < p.y + kBeanHeight) {
                a.knock(b, glm::vec3(0.0f, 7.0f, (ropeSpeed(t) > 0.0f ? -1.0f : 1.0f) * 3.0f), 1.5f);
                for (int k = 0; k < 20; ++k) a.flame(p + glm::vec3(0.0f, 0.6f, 0.0f), 1.6f);
                a.sound(p, kke::AudioMaterialTable::Dirt, 0.9f);
                a.eliminate(b, "caught by the rope");
            }
        }
    }

    bool over(Arena& a) const override {
        const int n = static_cast<int>(a.beans().size());
        return a.activeCount() == 0 || (n > 1 && a.activeCount() <= 1);
    }
    void timeUp(Arena& a) override {
        for (Bean& b : a.beans())
            if (b.active) b.result.score = 1.0f; // every survivor shares the win
    }

    std::string beanStatus(Arena& a, const Bean& b) const override {
        (void)b;
        return std::to_string(a.activeCount()) + " still jumping";
    }

    BeanInput bot(Arena& a, Bean& b, float dt) override {
        (void)dt;
        BeanInput in;
        const float t = a.time();
        const float w = ropeSpeed(t);
        const float angle = ropeAngle(t);
        // Time until the rope is at the bean's feet: the angle where it
        // crosses the bean's z at the bottom (about 0, mod 2 pi).
        const float p = b.feet(a.world()).z;
        const float target = std::asin(std::clamp(-p / kRopeR, -1.0f, 1.0f));
        float until = w > 0.0f ? target - angle : angle - target;
        until = std::fmod(until, 2.0f * kPi);
        if (until < 0.0f) until += 2.0f * kPi;
        const float eta = until / std::max(0.1f, std::abs(w));
        // Better bots judge it better; everyone misjudges it sometimes.
        if (b.botTimer <= 0.0f) b.a = a.botRng().range(0.17f, 0.34f) + (1.0f - skill(b)) * a.botRng().range(-0.2f, 0.25f);
        b.botTimer -= dt;
        if (eta < b.a && b.grounded) {
            in.jump = true;
            b.botTimer = 0.5f; // a new judgement next time
        }
        // Keep off the ends of the bridge.
        const glm::vec3 f = b.feet(a.world());
        if (std::abs(f.x) > kHalfLength - 1.2f) in.move.x = f.x > 0.0f ? -0.4f : 0.4f;
        if (std::abs(f.z) > 0.6f) in.move.y = f.z > 0.0f ? -0.4f : 0.4f;
        return in;
    }

private:
    float m_speed0 = 2.3f, m_accel = 0.03f;
    std::vector<float> m_flips;
    int m_rope = -1, m_lastBottom = 0;
    bool m_flipShown[8] = {};

    // Speed, faster over time, and turning round at each flip.
    float ropeSpeed(float t) const {
        float sign = 1.0f;
        for (float f : m_flips)
            if (t >= f) sign = -sign;
        return sign * (m_speed0 + m_accel * t);
    }
    // Its angle from the bottom: the integral of the speed (piecewise).
    float ropeAngle(float t) const {
        float angle = 0.0f, from = 0.0f, sign = 1.0f;
        auto integral = [this](float t0, float t1) { return m_speed0 * (t1 - t0) + 0.5f * m_accel * (t1 * t1 - t0 * t0); };
        for (float f : m_flips) {
            if (t < f) break;
            angle += sign * integral(from, f);
            from = f;
            sign = -sign;
        }
        return angle + sign * integral(from, t) + kPi; // it starts at the top
    }
    bool flipped(float t, float dt) {
        for (size_t i = 0; i < m_flips.size() && i < 8; ++i)
            if (t >= m_flips[i] && t - dt < m_flips[i] && !m_flipShown[i]) return (m_flipShown[i] = true);
        return false;
    }
};

} // namespace

std::unique_ptr<Minigame> makeJumpRope() { return std::make_unique<JumpRope>(); }

} // namespace party
