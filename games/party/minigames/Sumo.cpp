// Bean Sumo: everyone on one round floor in the sky. Barge into the
// others and dive at them to knock them off; hold dive to wind up a
// harder one. Two beans diving head-on at the same power clash: both mash
// jump and the loser flies off at twice the power they met with. The edge
// crumbles away ring by ring, so the floor keeps getting smaller. The
// last bean on it wins.
//
// The floor's rings crumble at set times (the same everywhere, no events);
// bumping, the charged dive and the clash are the party's
// (PartyModule::bumpBeans, Beans.cpp), just much harder here.

#include "Common.h"

#include "kke/ImpactSynth.h"

#include <algorithm>
#include <cmath>

namespace party {

namespace {

using namespace common;

constexpr int kRings = 6;
constexpr float kTileR = 1.0f, kGap = 0.04f, kTileHalfH = 0.25f;
constexpr float kWarn = 2.5f;       // s a ring shakes before it crumbles
constexpr float kCrumble = 0.6f;    // s it takes to crumble away
constexpr float kDrops[] = { 25.0f, 42.0f, 58.0f, 72.0f }; // rings 6, 5, 4, 3 go

class Sumo final : public Minigame {
public:
    const char* id() const override { return "sumo"; }
    const char* title() const override { return "Bean Sumo"; }
    const char* goal() const override { return "Barge and dive into the others to knock them off. The edge keeps crumbling. Last bean on the floor wins!"; }
    std::string controls() const override { return "{move} run  ·  hold {dive} and let go: dive (longer, harder)  ·  {push} push  ·  {jump} jump (mash in a clash)"; }
    const char* mood() const override { return "sunset"; }
    float timeLimit() const override { return 90.0f; }
    float killY() const override { return -6.0f; }
    float bumpStrength() const override { return 6.5f; }
    bool chargedDive() const override { return true; }
    // The 3-player split's spare quarter shows the arena from here.
    CameraStyle camera() const override { return CameraStyle::Overview; }

    void overview(kke::Camera& cam) const override {
        cam.position = { 0.0f, 17.0f, 18.0f };
        cam.target = { 0.0f, -1.0f, 0.0f };
    }

    void build(Arena& a) override {
        m_tiles.clear();
        for (const glm::vec2& c : hexGrid(kRings, kTileR, kGap)) {
            Tile t;
            t.ring = hexRing(c, kTileR, kGap);
            t.centre = glm::vec3(c.x, -kTileHalfH, c.y);
            kke::RigidWorld::BodyDesc d;
            d.shape = kke::RigidWorld::Shape::ConvexHull;
            d.motion = kke::RigidWorld::Motion::Kinematic;
            d.points = hexPoints(kTileR, kTileHalfH);
            d.position = t.centre;
            d.density = 900.0f;
            d.material = kke::AudioMaterialTable::Stone;
            std::vector<kke::Vertex> v;
            std::vector<uint32_t> idx;
            // A sumo ring: sand in the middle, a straw band, red at the edge.
            const glm::vec3 top = t.ring == 2 ? glm::vec3(0.85f, 0.75f, 0.45f) : t.ring >= kRings - 1 ? glm::vec3(0.85f, 0.3f, 0.28f)
                                                                                                    : glm::vec3(0.93f, 0.85f, 0.66f);
            MeshBuilder{ v, idx }.hexPrism(glm::vec3(0.0f), kTileR, kTileHalfH, top, glm::vec3(0.55f, 0.42f, 0.3f));
            t.top = top;
            t.part = a.addPart(d, std::move(v), std::move(idx));
            m_tiles.push_back(t);
        }
        // Lanterns round it, far off.
        MeshBuilder m = a.levelMesh();
        for (int k = 0; k < 10; ++k) {
            const float ang = 2.0f * kPi * static_cast<float>(k) / 10.0f;
            const glm::vec3 at(std::cos(ang) * 22.0f, -2.0f + static_cast<float>(k % 3) * 2.0f, std::sin(ang) * 22.0f);
            m.ellipsoid(at, { 0.8f, 1.1f, 0.8f }, glm::vec3(1.0f, 0.45f, 0.3f), 10, 6);
        }
    }

    void spawn(Arena& a, int index, int count, glm::vec3& feet, float& yaw) override {
        (void)a;
        const float ang = 2.0f * kPi * static_cast<float>(index) / static_cast<float>(std::max(1, count));
        feet = { std::cos(ang) * 5.0f, 0.02f, std::sin(ang) * 5.0f };
        yaw = glm::degrees(std::atan2(feet.x, feet.z));
    }

    void update(Arena& a, float dt) override {
        const float t = a.playing() ? a.time() : 0.0f;
        for (Tile& tile : m_tiles) {
            const int drop = dropIndex(tile.ring);
            if (drop < 0 || tile.state == 2) continue;
            const float when = kDrops[drop];
            if (tile.state == 0 && t >= when) {
                tile.state = 1;
                tile.at = t;
                if (a.botRng().below(3) == 0) a.sound(tile.centre, kke::AudioMaterialTable::Stone, 0.4f);
            } else if (tile.state == 0 && t >= when - kWarn) {
                const float s = 0.03f * std::sin((t + tile.centre.x) * 60.0f);
                a.movePart(tile.part, tile.centre + glm::vec3(s, 0.0f, -s), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), dt);
            } else if (tile.state == 1) {
                // Crumbling: sinking slowly and shedding rubble, then gone
                // (kinematic to the end, so nobody is thrown by it).
                const float k = std::min(1.0f, (t - tile.at) / kCrumble);
                a.movePart(tile.part, tile.centre - glm::vec3(0.0f, k * k * 0.35f, 0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), dt);
                if (a.botRng().unit() < dt * 20.0f) a.rubble(tile.centre - glm::vec3(0.0f, kTileHalfH, 0.0f), tile.top, 2, kTileR * 0.9f);
                if (k >= 1.0f) {
                    a.removePart(tile.part);
                    a.rubble(tile.centre - glm::vec3(0.0f, 0.35f, 0.0f), tile.top, 20, kTileR * 0.85f);
                    tile.state = 2;
                }
            }
        }
        if (!a.playing()) return;
        for (size_t k = 0; k < std::size(kDrops); ++k)
            if (t >= kDrops[k] - kWarn && t - dt < kDrops[k] - kWarn) a.flash("The edge is crumbling!", 1.4f);
        for (Bean& b : a.beans())
            if (!b.remote && b.active) b.result.score = t;
        a.status(std::to_string(a.activeCount()) + " left");
    }

    bool over(Arena& a) const override {
        const int n = static_cast<int>(a.beans().size());
        return a.activeCount() == 0 || (n > 1 && a.activeCount() <= 1);
    }
    void timeUp(Arena& a) override {
        for (Bean& b : a.beans())
            if (b.active && !b.remote) b.result.score = 1000.0f;
    }

    void touched(Arena& a, Bean& by, Bean& other) override {
        (void)by;
        a.sound(other.remote ? other.drawFeet : other.feet(a.world()), kke::AudioMaterialTable::Rubber, 0.8f);
    }

    BeanInput bot(Arena& a, Bean& b, float dt) override {
        BeanInput in;
        const glm::vec3 p = b.feet(a.world());
        const float safe = safeRadius(a.time());
        const float fromMiddle = glm::length(glm::vec2(p.x, p.z));
        // Near the edge: back toward the middle first.
        if (fromMiddle > safe - 1.5f * skill(b)) {
            in.move = steer(p, glm::vec3(0.0f));
            return in;
        }
        // Chase someone (a new target now and then), dive when close.
        b.botTimer -= dt;
        std::vector<Bean>& beans = a.beans();
        if (b.botTimer <= 0.0f || b.i <= 0 || b.i > static_cast<int>(beans.size()) || !beans[static_cast<size_t>(b.i - 1)].active) {
            b.botTimer = a.botRng().range(1.5f, 4.0f);
            b.i = 0;
            float best = 1e9f;
            for (size_t k = 0; k < beans.size(); ++k) {
                const Bean& o = beans[k];
                if (&o == &b || !o.active || o.hidden) continue;
                const glm::vec3 q = o.remote ? o.drawFeet : o.feet(a.world());
                // Nearer, and nearer the edge, is juicier.
                const float d = glm::length(q - p) - glm::length(glm::vec2(q.x, q.z)) * 0.3f + a.botRng().range(0.0f, 2.0f);
                if (d < best) {
                    best = d;
                    b.i = static_cast<int>(k) + 1;
                }
            }
        }
        if (b.i <= 0) return in;
        const Bean& o = beans[static_cast<size_t>(b.i - 1)];
        const glm::vec3 q = o.remote ? o.drawFeet : o.feet(a.world());
        // Aim to push them outward: come at them from the middle's side.
        const glm::vec3 outward = glm::length(glm::vec2(q.x, q.z)) > 0.1f ? glm::normalize(glm::vec3(q.x, 0.0f, q.z)) : glm::vec3(1.0f, 0.0f, 0.0f);
        const float d = glm::length(glm::vec2(q.x - p.x, q.z - p.z));
        const glm::vec3 aim = d > 2.5f ? q - outward * 1.2f : q;
        in.move = steer(p, aim);
        // Winding up a dive (b.j: holding, b.b: s left to hold), then letting go.
        if (b.j == 1) {
            b.b -= dt;
            in.diveHeld = b.b > 0.0f && b.grounded;
            if (!in.diveHeld) b.j = 0;
        } else if (d < 3.0f && b.grounded && b.dive <= 0.0f && b.diveCooldown <= 0.0f && a.botRng().unit() < dt * 3.0f * skill(b)) {
            b.j = 1;
            b.b = a.botRng().range(0.05f, 0.9f);
            in.diveHeld = true;
        }
        if (in.diveHeld && b.charge > 0.0f && d < 1.6f) b.b = 0.0f; // too close to wait: go now
        return in;
    }

private:
    struct Tile {
        int ring = 0, part = -1;
        glm::vec3 centre{0.0f};
        glm::vec3 top{1.0f};
        int state = 0;   // 0 solid, 1 crumbling, 2 gone
        float at = 0.0f;
    };
    std::vector<Tile> m_tiles;

    static int dropIndex(int ring) {
        const int k = kRings - ring;
        return k >= 0 && k < static_cast<int>(std::size(kDrops)) ? k : -1;
    }
    // How far out the floor still reaches (a little inside the edge ring).
    static float safeRadius(float t) {
        int rings = kRings;
        for (float d : kDrops)
            if (t >= d - kWarn) --rings;
        return static_cast<float>(rings) * (kTileR + kGap * 0.5f) * 1.5f + 0.5f; // ring n's nearest tiles are 1.5 n tiles out
    }
};

} // namespace

std::unique_ptr<Minigame> makeSumo() { return std::make_unique<Sumo>(); }

} // namespace party
