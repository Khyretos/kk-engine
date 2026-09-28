// Hot Potato: someone is holding a bomb with a burning fuse. Touch another
// bean to pass it on (no passing straight back); whoever holds it when it
// goes off is out. Then a new bomb, a shorter fuse. The last bean left
// wins. Holding it makes you a little faster, so you can catch someone.
//
// The host lights each bomb and says who gets it and when it goes off;
// the holder's own machine sees them touch someone and passes it (all as
// events, so every machine agrees who has it).

#include "Common.h"

#include "kke/ImpactSynth.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace party {

namespace {

using namespace common;

constexpr float kHalf = 9.0f;        // the arena, a square
constexpr float kPassReach = 1.05f;  // feet to feet
constexpr float kNoPassBack = 0.8f;  // s before it can go back to who gave it
constexpr float kPause = 2.5f;       // s between a bang and the next bomb
constexpr int kEventPass = 1, kEventNew = 2, kEventBang = 3;

class HotPotato final : public Minigame {
public:
    const char* id() const override { return "hot_potato"; }
    const char* title() const override { return "Hot Potato"; }
    const char* goal() const override { return "Got the bomb? Touch someone to pass it on before it goes BANG! Last one standing wins."; }
    std::string controls() const override { return "{move} run  ·  {dive} dive to tag  ·  {jump} jump"; }
    const char* mood() const override { return "clear_day"; }
    float timeLimit() const override { return 150.0f; }
    float bumpStrength() const override { return 1.5f; }
    CameraStyle camera() const override { return CameraStyle::Overview; }

    void overview(kke::Camera& cam) const override {
        cam.position = { 0.0f, 17.0f, 15.5f };
        cam.target = { 0.0f, 0.0f, 0.5f };
    }

    void build(Arena& a) override {
        m_holder = -1;
        m_from = -1;
        m_passAt = 0.0f;
        m_bangAt = 0.0f;
        m_nextAt = 1.5f;
        m_bombs = 0;
        m_pillars.clear();
        // A padded play room: floor, low walls, a few pillars to dodge round.
        a.staticBox({ 0.0f, -0.5f, 0.0f }, { kHalf, 0.5f, kHalf }, glm::vec3(0.45f, 0.7f, 0.85f));
        const glm::vec3 wall(1.0f, 0.8f, 0.3f);
        for (int s = -1; s <= 1; s += 2) {
            a.staticBox({ static_cast<float>(s) * (kHalf + 0.4f), 0.6f, 0.0f }, { 0.4f, 0.6f, kHalf + 0.8f }, wall);
            a.staticBox({ 0.0f, 0.6f, static_cast<float>(s) * (kHalf + 0.4f) }, { kHalf, 0.6f, 0.4f }, wall);
        }
        Rng& rng = a.rng();
        MeshBuilder m = a.levelMesh();
        for (int k = 0; k < 5; ++k) {
            const glm::vec3 at(rng.range(-5.5f, 5.5f), 0.0f, rng.range(-5.5f, 5.5f));
            if (glm::length(at) < 2.0f) continue;
            a.staticBox(at + glm::vec3(0.0f, 1.0f, 0.0f), { 0.6f, 1.0f, 0.6f }, glm::vec3(1.0f, 0.45f, 0.55f));
            m_pillars.push_back(at);
        }
        for (int k = 0; k < 8; ++k)
            m.box({ -kHalf + 1.125f + 2.25f * static_cast<float>(k), 0.005f, 0.0f }, { 1.0f, 0.005f, kHalf - 0.1f },
                  k % 2 ? glm::vec3(0.5f, 0.75f, 0.9f) : glm::vec3(0.42f, 0.66f, 0.82f));
        // The bomb: a black ball with a fuse.
        std::vector<kke::Vertex> v;
        std::vector<uint32_t> idx;
        MeshBuilder bomb{ v, idx };
        bomb.ellipsoid(glm::vec3(0.0f), glm::vec3(0.34f), glm::vec3(0.08f), 14, 10);
        bomb.cylinder({ 0.0f, 0.3f, 0.0f }, 0.1f, 0.1f, 0.1f, glm::vec3(0.5f), 8);
        bomb.cylinder({ 0.0f, 0.4f, 0.0f }, 0.025f, 0.025f, 0.18f, glm::vec3(0.85f, 0.75f, 0.55f), 6);
        m_bomb = a.addVisual(std::move(v), std::move(idx));
        a.part(m_bomb).visible = false;
        a.part(m_bomb).roughness = 0.25f;
    }

    void spawn(Arena& a, int index, int count, glm::vec3& feet, float& yaw) override {
        (void)a;
        const float ang = 2.0f * kPi * static_cast<float>(index) / static_cast<float>(std::max(1, count));
        feet = { std::cos(ang) * 6.0f, 0.02f, std::sin(ang) * 6.0f };
        yaw = glm::degrees(std::atan2(feet.x, feet.z));
    }

    void update(Arena& a, float dt) override {
        (void)dt;
        const float t = a.time();
        std::vector<Bean>& beans = a.beans();
        kke::RigidWorld& w = a.world();
        // The bomb rides over its holder's head, fizzing.
        Part& bomb = a.part(m_bomb);
        bomb.visible = m_holder >= 0;
        if (m_holder >= 0) {
            const Bean& h = beans[static_cast<size_t>(m_holder)];
            const glm::vec3 at = (h.remote ? h.drawFeet : h.feet(w)) + glm::vec3(0.0f, kBeanHeight + 0.45f, 0.0f);
            const float left = m_bangAt - t;
            const float pulse = 1.0f + 0.08f * std::sin(t * (left < 3.0f ? 30.0f : 12.0f));
            bomb.transform = glm::scale(glm::translate(glm::mat4(1.0f), at), glm::vec3(pulse));
            a.flame(at + glm::vec3(0.0f, 0.6f, 0.0f), 0.5f);
            if (a.playing()) a.status(left > 0.0f ? h.name + " has the bomb!" : "BANG!");
        }
        if (!a.playing()) return;
        // The host: a new bomb after the pause; the bang when the fuse is done.
        if (a.authority()) {
            if (m_holder < 0 && t >= m_nextAt && a.activeCount() > 1) {
                const int pick = pickHolder(a);
                if (pick >= 0) {
                    const float fuse = std::max(6.0f, a.botRng().range(12.0f, 18.0f) - static_cast<float>(m_bombs) * 1.5f);
                    a.event(kEventNew, pick, static_cast<int>(std::lround((t + fuse) * 100.0f)));
                }
            } else if (m_holder >= 0 && t >= m_bangAt) {
                a.event(kEventBang, m_holder, 0);
            }
        }
        // The holder's machine: touch someone, and it's theirs.
        if (m_holder >= 0 && t >= m_passAt) {
            Bean& h = beans[static_cast<size_t>(m_holder)];
            if (!h.remote && h.active) {
                const glm::vec3 p = h.feet(w);
                for (Bean& o : beans) {
                    if (&o == &h || !o.active || o.hidden || (o.index == m_from && t < m_passAt + kNoPassBack)) continue;
                    const glm::vec3 q = o.remote ? o.drawFeet : o.feet(w);
                    if (glm::length(q - p) < kPassReach + (h.dive > 0.0f ? 0.4f : 0.0f)) {
                        a.event(kEventPass, h.index, o.index);
                        break;
                    }
                }
            }
        }
        // Everyone but the holder is a touch slower.
        for (Bean& b : beans) {
            if (!b.remote && b.index != m_holder) b.input.move *= 0.9f;
            if (!b.remote && b.active) b.result.score = t;
        }
    }

    void onEvent(Arena& a, int kind, int x, int y) override {
        std::vector<Bean>& beans = a.beans();
        if (x < 0 || x >= static_cast<int>(beans.size())) return;
        const float t = a.time();
        if (kind == kEventNew) {
            m_holder = x;
            m_from = -1;
            m_bangAt = static_cast<float>(y) / 100.0f;
            m_passAt = t + 0.5f;
            ++m_bombs;
            a.flash(beans[static_cast<size_t>(x)].name + " has the bomb!", 1.6f);
            a.tone(static_cast<int>(kke::Earcon::Activate));
        } else if (kind == kEventPass && x == m_holder && y >= 0 && y < static_cast<int>(beans.size())) {
            m_from = x;
            m_holder = y;
            m_passAt = t + 0.35f; // a moment before it can move on again
            Bean& to = beans[static_cast<size_t>(y)];
            const glm::vec3 at = to.remote ? to.drawFeet : to.feet(a.world());
            a.burst(at + glm::vec3(0.0f, 1.6f, 0.0f), glm::vec3(1.0f, 0.8f, 0.2f), 10, 2.0f);
            a.sound(at, kke::AudioMaterialTable::Rubber, 0.6f);
        } else if (kind == kEventBang && x == m_holder) {
            Bean& h = beans[static_cast<size_t>(x)];
            const glm::vec3 at = h.remote ? h.drawFeet : h.feet(a.world());
            m_holder = -1;
            m_nextAt = t + kPause;
            a.burst(at + glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(1.0f, 0.5f, 0.1f), 60, 7.0f);
            for (int k = 0; k < 30; ++k) a.flame(at + glm::vec3(a.botRng().range(-1.0f, 1.0f), a.botRng().range(0.2f, 2.0f), a.botRng().range(-1.0f, 1.0f)), 2.0f);
            a.sound(at, kke::AudioMaterialTable::Stone, 1.0f);
            // Everyone near gets blown back; the holder's gone.
            for (Bean& o : beans) {
                if (o.remote || !o.active) continue;
                const glm::vec3 q = o.feet(a.world());
                const glm::vec3 d = q - at;
                const float l = glm::length(glm::vec2(d.x, d.z));
                if (&o == &h) {
                    a.knock(o, glm::vec3(0.0f, 9.0f, 0.0f), 2.0f);
                    a.eliminate(o, "went BANG");
                } else if (l < 3.5f) {
                    const glm::vec2 dir = l > 0.01f ? glm::vec2(d.x, d.z) / l : glm::vec2(1.0f, 0.0f);
                    a.knock(o, glm::vec3(dir.x, 0.0f, dir.y) * (3.5f - l) * 2.5f + glm::vec3(0.0f, 3.0f, 0.0f), 0.8f);
                }
            }
        }
    }

    bool over(Arena& a) const override {
        const int n = static_cast<int>(a.beans().size());
        return a.activeCount() == 0 || (n > 1 && a.activeCount() <= 1);
    }
    void timeUp(Arena& a) override {
        for (Bean& b : a.beans())
            if (b.active && !b.remote) b.result.score = b.index == m_holder ? 999.0f : 1000.0f; // holding it at the end is worse
    }

    std::string beanStatus(Arena& a, const Bean& b) const override {
        if (b.index != m_holder) return {};
        return "YOU HAVE THE BOMB! " + std::to_string(static_cast<int>(std::ceil(std::max(0.0f, m_bangAt - a.time())))) + " s";
    }

    BeanInput bot(Arena& a, Bean& b, float dt) override {
        (void)dt;
        BeanInput in;
        kke::RigidWorld& w = a.world();
        const glm::vec3 p = b.feet(w);
        std::vector<Bean>& beans = a.beans();
        if (m_holder < 0) {
            in.move = steer(p, glm::vec3(0.0f), 0.4f); // drift to the middle
            return in;
        }
        if (b.index == m_holder) {
            // Chase the nearest one we can pass to (dive at them when close).
            float best = 1e9f;
            glm::vec3 target = p;
            for (const Bean& o : beans) {
                if (&o == &b || !o.active || o.hidden || o.index == m_from) continue;
                const glm::vec3 q = o.remote ? o.drawFeet : o.feet(w);
                const float d = glm::length(q - p);
                if (d < best) {
                    best = d;
                    target = q + (o.remote ? glm::vec3(0.0f) : w.characterVelocity(o.id) * 0.25f * skill(b)); // lead them
                }
            }
            in.move = steer(p, target);
            if (best < 2.2f && b.grounded && a.botRng().unit() < 0.05f * skill(b)) in.dive = true;
            return in;
        }
        // Everyone else runs from the bomb (and from the walls and pillars).
        const Bean& h = beans[static_cast<size_t>(m_holder)];
        const glm::vec3 q = h.remote ? h.drawFeet : h.feet(w);
        glm::vec2 away(p.x - q.x, p.z - q.z);
        const float d = glm::length(away);
        if (d > 7.0f + 3.0f * (1.0f - skill(b))) {
            in.move = glm::vec2(0.0f);
            return in;
        }
        away = d > 0.01f ? away / d : glm::vec2(1.0f, 0.0f);
        glm::vec2 push(0.0f);
        // Walls push back harder near them; corners are traps.
        push.x -= std::max(0.0f, p.x - (kHalf - 2.5f)) - std::max(0.0f, -p.x - (kHalf - 2.5f));
        push.y -= std::max(0.0f, p.z - (kHalf - 2.5f)) - std::max(0.0f, -p.z - (kHalf - 2.5f));
        for (const glm::vec3& pil : m_pillars) {
            const glm::vec2 e(p.x - pil.x, p.z - pil.z);
            const float l = glm::length(e);
            if (l < 1.8f && l > 0.01f) push += e / l * (1.8f - l);
        }
        // A slide sideways along a wall rather than into it.
        glm::vec2 dir = away + push * 1.2f + glm::vec2(-away.y, away.x) * 0.35f * (b.index % 2 ? 1.0f : -1.0f);
        in.move = glm::length(dir) > 0.01f ? glm::normalize(dir) * skill(b) : glm::vec2(0.0f);
        return in;
    }

private:
    int m_holder = -1, m_from = -1, m_bomb = -1, m_bombs = 0;
    float m_passAt = 0.0f, m_bangAt = 0.0f, m_nextAt = 0.0f;
    std::vector<glm::vec3> m_pillars;

    static int pickHolder(Arena& a) {
        std::vector<int> in;
        for (const Bean& b : a.beans())
            if (b.active && !b.hidden) in.push_back(b.index);
        return in.empty() ? -1 : in[static_cast<size_t>(a.botRng().below(static_cast<int>(in.size())))];
    }
};

} // namespace

std::unique_ptr<Minigame> makeHotPotato() { return std::make_unique<HotPotato>(); }

} // namespace party
