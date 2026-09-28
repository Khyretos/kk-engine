// Hex-a-Gone: three floors of hexagon tiles, one above the other. A tile
// you step on shakes and drops away a moment later, so keep moving, and
// don't let anyone eat the floor out from under you. Fall through all
// three and you're out; the last bean standing wins.
//
// Tiles are Jolt bodies: kinematic while they hold (they shake), dynamic
// once they drop, tumbling down past the floors below. A tile going is an
// event, so it goes on every machine.

#include "Common.h"

#include "kke/ImpactSynth.h"

#include <algorithm>
#include <cmath>

namespace party {

namespace {

using namespace common;

constexpr int kLayers = 3;
constexpr int kRings = 5;
constexpr float kTileR = 1.05f, kGap = 0.1f, kTileHalfH = 0.2f;
constexpr float kLayerGap = 6.0f;
constexpr float kShake = 0.55f;    // s from stepped on to dropping
constexpr float kGone = 3.0f;      // s a dropped tile falls before it's removed
constexpr int kEventStep = 1;

float layerY(int layer) { return -kLayerGap * static_cast<float>(layer); }

class FallingTiles final : public Minigame {
public:
    const char* id() const override { return "tiles"; }
    const char* title() const override { return "Hex-a-Gone"; }
    const char* goal() const override { return "Tiles fall a moment after you step on them. Keep moving, and be the last one standing on any floor!"; }
    const char* mood() const override { return "playful"; }
    float timeLimit() const override { return 120.0f; }
    float killY() const override { return layerY(kLayers - 1) - 5.0f; }
    float bumpStrength() const override { return 3.0f; }

    void build(Arena& a) override {
        m_tiles.clear();
        const std::vector<glm::vec2> grid = hexGrid(kRings, kTileR, kGap);
        const glm::vec3 tops[kLayers] = { { 1.0f, 0.55f, 0.75f }, { 0.45f, 0.75f, 1.0f }, { 0.55f, 0.9f, 0.45f } };
        for (int layer = 0; layer < kLayers; ++layer) {
            for (const glm::vec2& c : grid) {
                Tile t;
                t.layer = layer;
                t.centre = glm::vec3(c.x, layerY(layer) - kTileHalfH, c.y);
                // A lighter ring pattern so you can read the floor.
                const glm::vec3 top = hexRing(c, kTileR, kGap) % 2 ? tops[layer] * 0.85f : tops[layer];
                t.part = addTile(a, t.centre, top, tops[layer] * 0.6f);
                m_tiles.push_back(t);
            }
        }
        // A far backdrop: a ring of pillars in the sky.
        MeshBuilder m = a.levelMesh();
        for (int k = 0; k < 16; ++k) {
            const float ang = 2.0f * kPi * static_cast<float>(k) / 16.0f;
            m.cylinder({ std::cos(ang) * 26.0f, -30.0f, std::sin(ang) * 26.0f }, 1.2f, 1.0f, 36.0f, glm::vec3(1.0f, 0.9f, 0.7f), 10);
        }
    }

    void spawn(Arena& a, int index, int count, glm::vec3& feet, float& yaw) override {
        (void)a;
        const float ang = 2.0f * kPi * static_cast<float>(index) / static_cast<float>(std::max(1, count));
        feet = { std::cos(ang) * 4.5f, 0.02f, std::sin(ang) * 4.5f };
        // Facing the middle.
        yaw = glm::degrees(std::atan2(feet.x, feet.z));
    }

    void update(Arena& a, float dt) override {
        const float t = a.time();
        kke::RigidWorld& w = a.world();
        // Stepped-on tiles shake, then drop; dropped ones go after a while.
        for (Tile& tile : m_tiles) {
            if (tile.state == 1) {
                const float k = t - tile.at;
                if (k >= kShake) {
                    tile.state = 2;
                    tile.at = t;
                    movePart(a, tile, tile.centre, dt);
                    w.setMotion(a.part(tile.part).body, kke::RigidWorld::Motion::Dynamic);
                } else {
                    const float s = 0.04f * std::sin(k * 70.0f);
                    movePart(a, tile, tile.centre + glm::vec3(s, -k * 0.1f, -s), dt);
                }
            } else if (tile.state == 2 && t - tile.at > kGone) {
                a.removePart(tile.part);
                tile.state = 3;
            }
        }
        if (!a.playing()) return;
        // This machine's beans step on tiles.
        for (Bean& b : a.beans()) {
            if (b.remote || !b.active || b.hidden || !b.grounded) continue;
            const glm::vec3 p = b.feet(w);
            const int under = tileAt(p, 0.6f);
            if (under >= 0 && m_tiles[static_cast<size_t>(under)].state == 0) a.event(kEventStep, under, 0);
            b.result.score = t; // how long they've lasted (ties the ones out at once)
        }
        a.status(std::to_string(a.activeCount()) + " standing");
    }

    void onEvent(Arena& a, int kind, int x, int y) override {
        (void)y;
        if (kind != kEventStep || x < 0 || x >= static_cast<int>(m_tiles.size())) return;
        Tile& tile = m_tiles[static_cast<size_t>(x)];
        if (tile.state != 0) return;
        tile.state = 1;
        tile.at = a.time();
        if (a.botRng().below(4) == 0) a.sound(tile.centre, kke::AudioMaterialTable::Plastic, 0.3f);
    }

    bool over(Arena& a) const override {
        const int n = static_cast<int>(a.beans().size());
        return a.activeCount() == 0 || (n > 1 && a.activeCount() <= 1);
    }
    void timeUp(Arena& a) override {
        for (Bean& b : a.beans())
            if (b.active && !b.remote) b.result.score = 1000.0f; // everyone still up shares the win
    }

    std::string beanStatus(Arena& a, const Bean& b) const override {
        if (!b.active) return {};
        const glm::vec3 p = b.remote ? b.drawFeet : b.feet(a.world());
        return "Floor " + std::to_string(std::clamp(static_cast<int>(std::lround(-p.y / kLayerGap)) + 1, 1, kLayers));
    }

    BeanInput bot(Arena& a, Bean& b, float dt) override {
        BeanInput in;
        kke::RigidWorld& w = a.world();
        const glm::vec3 p = b.feet(w);
        b.botTimer -= dt;
        const int layer = std::clamp(static_cast<int>(std::lround(-p.y / kLayerGap)), 0, kLayers - 1);
        const bool targetGone = b.i <= 0 || b.i > static_cast<int>(m_tiles.size()) || m_tiles[static_cast<size_t>(b.i - 1)].state != 0 ||
                                m_tiles[static_cast<size_t>(b.i - 1)].layer != layer;
        if (b.botTimer <= 0.0f || targetGone) {
            // A solid tile a few steps away, nearer the middle, away from where we've been.
            b.botTimer = a.botRng().range(0.5f, 1.2f);
            float best = 1e9f;
            int pick = -1;
            for (size_t i = 0; i < m_tiles.size(); ++i) {
                const Tile& tile = m_tiles[i];
                if (tile.state != 0 || tile.layer != layer) continue;
                const float d = glm::length(glm::vec2(tile.centre.x - p.x, tile.centre.z - p.z));
                if (d < 1.5f || d > 6.0f) continue;
                const float score = std::abs(d - 3.0f) + glm::length(glm::vec2(tile.centre.x, tile.centre.z)) * 0.35f * skill(b) + a.botRng().range(0.0f, 2.0f);
                if (score < best) {
                    best = score;
                    pick = static_cast<int>(i);
                }
            }
            b.i = pick + 1;
        }
        if (b.i > 0) {
            const Tile& tile = m_tiles[static_cast<size_t>(b.i - 1)];
            in.move = steer(p, tile.centre);
            // A gap (or a hole) ahead: hop it.
            const glm::vec2 dir = glm::length(in.move) > 0.01f ? glm::normalize(in.move) : glm::vec2(0.0f);
            const glm::vec3 ahead = p + glm::vec3(dir.x, 0.0f, dir.y) * 0.9f;
            const int next = tileAt(ahead, 0.0f);
            if (b.grounded && (next < 0 || m_tiles[static_cast<size_t>(next)].state >= 2) && a.botRng().unit() < skill(b)) in.jump = true;
        } else {
            in.move = steer(p, { 0.0f, p.y, 0.0f }, 0.6f);
        }
        return in;
    }

private:
    struct Tile {
        int layer = 0, part = -1;
        glm::vec3 centre{0.0f};
        int state = 0;       // 0 solid, 1 shaking, 2 dropping, 3 gone
        float at = 0.0f;     // when it started shaking / dropping
    };
    std::vector<Tile> m_tiles;

    static int addTile(Arena& a, const glm::vec3& centre, const glm::vec3& top, const glm::vec3& side) {
        kke::RigidWorld::BodyDesc d;
        d.shape = kke::RigidWorld::Shape::ConvexHull;
        d.motion = kke::RigidWorld::Motion::Kinematic;
        d.points = hexPoints(kTileR, kTileHalfH);
        d.position = centre;
        d.density = 600.0f;
        d.friction = 0.8f;
        d.material = kke::AudioMaterialTable::Plastic;
        std::vector<kke::Vertex> v;
        std::vector<uint32_t> idx;
        MeshBuilder{ v, idx }.hexPrism(glm::vec3(0.0f), kTileR, kTileHalfH, top, side);
        const int p = a.addPart(d, std::move(v), std::move(idx));
        a.part(p).roughness = 0.35f;
        return p;
    }

    static void movePart(Arena& a, const Tile& t, const glm::vec3& at, float dt) { a.movePart(t.part, at, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), dt); }

    // The tile under a point (the one it's over, on its floor), or -1.
    int tileAt(const glm::vec3& p, float slack) const {
        const int layer = static_cast<int>(std::lround(-p.y / kLayerGap));
        if (layer < 0 || layer >= kLayers || std::abs(p.y - layerY(layer)) > 0.6f) return -1;
        int best = -1;
        float bestD = kTileR * 0.9f + slack;
        for (size_t i = 0; i < m_tiles.size(); ++i) {
            const Tile& t = m_tiles[i];
            if (t.layer != layer || t.state == 3) continue;
            const float d = glm::length(glm::vec2(t.centre.x - p.x, t.centre.z - p.z));
            if (d < bestD) {
                bestD = d;
                best = static_cast<int>(i);
            }
        }
        return best;
    }
};

} // namespace

std::unique_ptr<Minigame> makeFallingTiles() { return std::make_unique<FallingTiles>(); }

} // namespace party
