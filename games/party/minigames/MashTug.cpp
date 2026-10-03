// Mash Tug of War: two teams on either side of a mud pit, one rope. Mash
// jump as fast as you can: every press pulls. The team that drags the
// other's front bean over the edge wins; when time runs out, the rope's
// side decides.
//
// Uneven teams pull as hard as even ones: each team's pulls count as if
// it had as many beans as the bigger one (one against two: the one pulls
// double; two against three: each of the two counts 1.5). The rope's
// arrow and the line under the clock show which way and how hard.
//
// Each machine counts its own beans' presses (their score, which travels
// with their pose), so the rope is the same everywhere; the host calls
// the win (an event).

#include "Common.h"

#include "kke/ImpactSynth.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace party {

namespace {

using namespace common;

constexpr float kPitHalf = 1.6f;    // the mud pit, along X
constexpr float kFront = 2.3f;      // each team's front bean, from the middle
constexpr float kSpacing = 1.05f;
constexpr float kPull = 0.08f;      // m of rope per tap ahead (per bean, on average)
constexpr float kWin = kFront - kPitHalf + 0.3f; // the front bean is over the edge
constexpr int kEventWin = 1;

class MashTug final : public Minigame {
public:
    const char* id() const override { return "tug"; }
    const char* title() const override { return "Mash Tug of War"; }
    const char* goal() const override { return "Two teams, one rope. Mash jump as fast as you can to pull the other team into the mud!"; }
    std::string controls() const override { return "Mash {jump} to pull!"; }
    const char* mood() const override { return "noon"; }
    float timeLimit() const override { return 30.0f; }
    // The 3-player split's spare quarter shows the arena from here.
    CameraStyle camera() const override { return CameraStyle::Overview; }
    float killY() const override { return -2.4f; }
    // Off to the side and higher: your own team is in a line in front of you.
    float cameraSide() const override { return 2.2f; }
    float cameraPitch() const override { return -24.0f; }
    float bumpStrength() const override { return 0.0f; }

    void overview(kke::Camera& cam) const override {
        cam.position = { 0.0f, 5.5f, 13.0f };
        cam.target = { 0.0f, 0.3f, 0.0f };
    }

    void build(Arena& a) override {
        m_pos = 0.0f;
        m_winner = -1;
        m_decidedAt = 0.0f;
        const glm::vec3 grass(0.4f, 0.68f, 0.3f);
        for (int s = -1; s <= 1; s += 2)
            a.staticBox({ static_cast<float>(s) * (kPitHalf + 5.0f), -0.5f, 0.0f }, { 5.0f, 0.5f, 3.0f }, grass);
        a.staticBox({ 0.0f, -3.2f, 0.0f }, { kPitHalf, 0.2f, 3.0f }, glm::vec3(0.35f, 0.24f, 0.14f)); // the pit's floor
        MeshBuilder m = a.levelMesh();
        m.box({ 0.0f, -1.45f, 0.0f }, { kPitHalf, 0.02f, 3.0f }, glm::vec3(0.42f, 0.28f, 0.15f)); // mud
        for (int k = 0; k < 12; ++k)
            m.ellipsoid({ a.rng().range(-kPitHalf + 0.3f, kPitHalf - 0.3f), -1.43f, a.rng().range(-2.6f, 2.6f) }, { 0.3f, 0.06f, 0.3f },
                        glm::vec3(0.36f, 0.24f, 0.13f), 8, 3);
        // Team colours on each side, and the edge lines.
        m.box({ -kPitHalf - 0.1f, 0.01f, 0.0f }, { 0.1f, 0.01f, 3.0f }, teamColour(0));
        m.box({ kPitHalf + 0.1f, 0.01f, 0.0f }, { 0.1f, 0.01f, 3.0f }, teamColour(1));
        for (int s = -1; s <= 1; s += 2)
            for (int k = 0; k < 3; ++k)
                m.cylinder({ static_cast<float>(s) * (kPitHalf + 10.5f - static_cast<float>(k) * 0.01f), 0.0f, -2.5f + 2.5f * static_cast<float>(k) }, 0.06f, 0.06f, 2.5f,
                           glm::vec3(0.9f), 6);
        // The rope, with a flag in the middle.
        std::vector<kke::Vertex> v;
        std::vector<uint32_t> idx;
        MeshBuilder rope{ v, idx };
        rope.cylinder({ 0.0f, -9.0f, 0.0f }, 0.06f, 0.06f, 18.0f, glm::vec3(0.85f, 0.72f, 0.5f), 8);
        rope.transform(0, glm::rotate(glm::mat4(1.0f), -kPi * 0.5f, glm::vec3(0, 0, 1))); // Y -> X
        rope.box({ 0.0f, -0.25f, 0.0f }, { 0.05f, 0.25f, 0.2f }, glm::vec3(1.0f, 0.85f, 0.15f));
        m_rope = a.addVisual(std::move(v), std::move(idx));
        // The arrow (points +X; turned round for the left team), in gold.
        v.clear();
        idx.clear();
        MeshBuilder arrow{ v, idx };
        arrow.box({ -0.25f, 0.0f, 0.0f }, { 0.35f, 0.09f, 0.09f }, glm::vec3(1.0f, 0.85f, 0.2f));
        for (int k = 0; k < 6; ++k) { // a stepped head
            const float t = static_cast<float>(k) / 6.0f;
            arrow.box({ 0.12f + t * 0.42f, 0.0f, 0.0f }, { 0.04f, 0.3f * (1.0f - t) + 0.03f, 0.1f }, glm::vec3(1.0f, 0.85f, 0.2f));
        }
        m_arrow = a.addVisual(std::move(v), std::move(idx));
        m_rate[0] = m_rate[1] = m_last[0] = m_last[1] = 0.0f;
    }

    void spawn(Arena& a, int index, int count, glm::vec3& feet, float& yaw) override {
        (void)a;
        (void)count;
        feet = slot(index, 0.0f);
        yaw = index % 2 ? -90.0f : 90.0f; // facing the middle
    }

    void update(Arena& a, float dt) override {
        std::vector<Bean>& beans = a.beans();
        // Taps: this machine's beans' jump presses (a remote bean's come in its score).
        float sum[2] = { 0.0f, 0.0f };
        int n[2] = { 0, 0 };
        for (Bean& b : beans) {
            const int team = b.index % 2;
            if (!b.remote && a.playing() && m_winner < 0 && b.input.jump) {
                b.result.score += 1.0f;
                b.squashVel -= 6.0f; // a heave
            }
            b.input.move = glm::vec2(0.0f);
            b.input.jump = b.input.dive = b.input.push = false;
            b.pulling = b.active && b.stun <= 0.0f && m_winner < 0;
            sum[team] += b.result.score;
            ++n[team];
        }
        // Each team's strength: its pulls, scaled up to the bigger team's size.
        const float most = static_cast<float>(std::max(1, std::max(n[0], n[1])));
        float strength[2];
        for (int t = 0; t < 2; ++t) strength[t] = n[t] ? sum[t] * most / static_cast<float>(n[t]) : 0.0f;
        // How hard each is pulling right now (pulls a second, smoothed).
        for (int t = 0; t < 2; ++t) {
            const float rate = dt > 0.0f ? std::max(0.0f, strength[t] - m_last[t]) / dt : 0.0f;
            m_rate[t] += (rate - m_rate[t]) * std::min(1.0f, dt * 2.5f);
            m_last[t] = strength[t];
        }
        // The rope: the difference in strength pulls it.
        if (m_winner < 0) {
            const float want = std::clamp((strength[1] - strength[0]) / most * kPull, -kWin - 0.2f, kWin + 0.2f);
            m_pos += (want - m_pos) * std::min(1.0f, dt * 6.0f);
            if (a.authority() && a.playing() && std::abs(m_pos) >= kWin) a.event(kEventWin, m_pos > 0.0f ? 1 : 0, 0);
        } else {
            m_pos = std::clamp(m_pos + (m_winner ? 1.0f : -1.0f) * dt * 2.5f, -kWin - 4.0f, kWin + 4.0f); // the winners haul the losers in
        }
        glm::mat4 r = glm::translate(glm::mat4(1.0f), { m_pos, 0.75f, 0.0f });
        a.part(m_rope).transform = r;
        // The arrow over the pit: points the way the rope is going, bigger the harder.
        const float diff = m_winner >= 0 ? (m_winner ? 1.0f : -1.0f) * 12.0f : m_rate[1] - m_rate[0];
        const float size = std::clamp(std::abs(diff) / 12.0f, 0.15f, 1.0f);
        glm::mat4 arrow = glm::translate(glm::mat4(1.0f), { m_pos, 2.6f + 0.1f * std::sin(a.time() * 6.0f), 0.0f });
        if (diff < 0.0f) arrow = glm::rotate(arrow, kPi, glm::vec3(0, 1, 0));
        a.part(m_arrow).transform = glm::scale(arrow, glm::vec3(0.6f + size, 0.6f + size * 0.6f, 0.6f + size * 0.6f));
        a.part(m_arrow).visible = a.playing() || m_winner >= 0;
        // Everyone holds the rope where it is; a bean dragged over the
        // edge lets go and tumbles into the mud.
        for (Bean& b : beans) {
            if (b.remote || !b.active || b.stun > 0.0f || b.a > 0.0f) continue;
            const glm::vec3 at = slot(b.index, m_pos);
            if (std::abs(at.x) < kPitHalf + 0.1f) {
                b.a = 1.0f;
                a.knock(b, glm::vec3(m_pos > 0.0f ? 2.5f : -2.5f, 2.0f, 0.0f), 1.5f);
                continue;
            }
            a.place(b, at, b.index % 2 ? -90.0f : 90.0f);
        }
        if (a.playing() && m_winner < 0) {
            char line[96];
            std::snprintf(line, sizeof(line), "RED %.0f  %s  %.0f BLUE%s", m_rate[0], m_rate[0] > m_rate[1] + 0.5f ? "<<" : m_rate[1] > m_rate[0] + 0.5f ? ">>" : "==",
                          m_rate[1], std::abs(m_pos) > kWin * 0.6f ? "  ·  Hold on!" : "");
            a.status(line);
        }
    }

    void onEvent(Arena& a, int kind, int x, int y) override {
        (void)y;
        if (kind != kEventWin || m_winner >= 0) return;
        m_winner = x;
        m_decidedAt = a.time();
        a.flash(x ? "Right team wins!" : "Left team wins!", 2.0f);
        a.tone(static_cast<int>(kke::Earcon::ToggleOn));
        for (Bean& b : a.beans()) {
            if (b.remote) continue;
            if (b.index % 2 == x) b.result.score += 1000.0f; // winners above every loser
        }
    }

    void fell(Arena& a, Bean& b) override { a.eliminate(b, "pulled into the mud"); }

    bool over(Arena& a) const override { return m_winner >= 0 && a.time() > m_decidedAt + 2.5f; }

    void timeUp(Arena& a) override {
        if (m_winner >= 0) return;
        const int winner = m_pos > 0.0f ? 1 : 0;
        for (Bean& b : a.beans())
            if (!b.remote && b.index % 2 == winner) b.result.score += 1000.0f;
    }

    std::string beanStatus(Arena& a, const Bean& b) const override {
        (void)a;
        return std::string(b.index % 2 ? "Right" : "Left") + " team · " + std::to_string(static_cast<int>(b.result.score) % 1000) + " pulls";
    }

    BeanInput bot(Arena& a, Bean& b, float dt) override {
        BeanInput in;
        b.botTimer -= dt;
        if (b.botTimer <= 0.0f) {
            // 4 to 8 presses a second, by skill, and a little ragged.
            const float rate = 3.5f + 4.5f * skill(b) * a.botRng().range(0.8f, 1.1f);
            b.botTimer = 1.0f / rate;
            in.jump = true;
        }
        return in;
    }

private:
    float m_pos = 0.0f;      // how far the rope has moved (+: toward the right team)
    int m_winner = -1;
    float m_decidedAt = 0.0f;
    int m_rope = -1, m_arrow = -1;
    float m_rate[2] = { 0.0f, 0.0f }, m_last[2] = { 0.0f, 0.0f }; // each team's pulls a second; last frame's strength

    static glm::vec3 teamColour(int team) { return team ? glm::vec3(0.25f, 0.5f, 1.0f) : glm::vec3(1.0f, 0.35f, 0.3f); }

    // Where bean `index` stands when the rope has moved by `pos`.
    static glm::vec3 slot(int index, float pos) {
        const float side = index % 2 ? 1.0f : -1.0f;
        const float k = static_cast<float>(index / 2);
        return { side * (kFront + k * kSpacing) + pos, 0.02f, (index / 2) % 2 ? 0.25f : -0.25f };
    }
};

} // namespace

std::unique_ptr<Minigame> makeMashTug() { return std::make_unique<MashTug>(); }

} // namespace party
