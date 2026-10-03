// Glass Bridge: three panes side by side on every step of a bridge high
// over the floor; one of each row is tempered and holds, the others
// shatter the moment you land on them (Squid Game). Now and then a row is
// all fake: nobody can stand on it, so the only way on is a leap of faith,
// a jump with a dive at the top of it to the row after. Everyone goes at
// once and shoving is allowed, so the bold (or the pushed) find the way
// for the careful. Fall through and you get one chance: dive the moment
// the glass goes, and you're thrown up and forward, maybe onto the next
// row. Reach the far side to finish; the round ends when everyone has
// crossed or fallen.
//
// A breaking pane is real physics: Voronoi shards (Shatter.h), each a Jolt
// body, fall with you to the floor far below and pile up there. Which
// panes hold comes from the round's seed; a break is an event, so every
// machine breaks the same pane the same way.

#include "Common.h"

#include "../Shatter.h"

#include "kke/ImpactSynth.h"

#include <algorithm>
#include <cmath>

namespace party {

namespace {

using namespace common;

constexpr int kRows = 8;
constexpr int kCols = 3;
constexpr float kPaneHalf = 1.0f;    // square panes, 2 m
constexpr float kPaneX = 2.5f;       // column to column
constexpr float kPitch = 3.4f;       // row to row (a 1.4 m gap to jump)
constexpr float kLeapPitch = 4.4f;   // either side of an all-fake row: 6.8 m to clear it, more than a jump
constexpr float kFirstZ = -4.9f;
constexpr float kThick = 0.06f;
constexpr float kFloorY = -22.0f;
constexpr float kSideX = kPaneX + kPaneHalf + 0.6f; // the towers' half width
constexpr int kEventBreak = 1;
constexpr float kStartEdge = -2.5f; // the start platform's front edge
constexpr float kSaveWindow = 0.3f; // s after the glass goes to dive out of it
constexpr float kSaveLift = 5.5f;   // m/s up that dive gives
constexpr int kKnownLeap = -2;      // m_known: the whole row is fake

float colX(int col) { return (static_cast<float>(col) - 1.0f) * kPaneX; }

class GlassBridge final : public Minigame {
public:
    const char* id() const override { return "glass_bridge"; }
    const char* title() const override { return "Glass Bridge"; }
    const char* goal() const override { return "Jump from pane to pane to the far side. One in each row is tempered glass; the others break, and some rows are all fake (jump and dive over them). Shoving is allowed!"; }
    std::string controls() const override { return "{move} run  ·  {jump} jump  ·  {dive} dive (the moment you fall: a save)  ·  {push} push"; }
    const char* mood() const override { return "arena_night"; }
    float timeLimit() const override { return 110.0f; }
    float killY() const override { return -9.0f; }
    float bumpStrength() const override { return 1.2f; } // running into someone: a nudge; pushing them is the push button

    void build(Arena& a) override {
        Rng& rng = a.rng();
        m_panes.clear();
        m_known.assign(kRows, -1);
        m_brokeUnder.clear();
        // Which rows are all fake (never the first or the last, never two
        // in a row), then where every row is.
        m_leap.assign(kRows, false);
        for (int r = 1; r < kRows - 1; ++r)
            if (!m_leap[static_cast<size_t>(r - 1)] && rng.below(4) == 0) m_leap[static_cast<size_t>(r)] = true;
        m_rowZ.assign(kRows, kFirstZ);
        for (int r = 1; r < kRows; ++r) {
            const bool wide = m_leap[static_cast<size_t>(r)] || m_leap[static_cast<size_t>(r - 1)];
            m_rowZ[static_cast<size_t>(r)] = m_rowZ[static_cast<size_t>(r - 1)] - (wide ? kLeapPitch : kPitch);
        }
        m_endZ = rowZ(kRows - 1) - kPitch + kPaneHalf;
        // The start and the far side, on towers.
        a.staticBox({ 0.0f, -0.5f, 1.5f }, { kSideX, 0.5f, 4.0f }, glm::vec3(0.3f, 0.32f, 0.4f));
        a.staticBox({ 0.0f, -0.5f, m_endZ - 3.0f }, { kSideX, 0.5f, 3.0f }, glm::vec3(0.3f, 0.32f, 0.4f));
        MeshBuilder m = a.levelMesh();
        m.box({ 0.0f, -11.0f, 1.5f }, { kSideX - 0.5f, 10.5f, 3.5f }, glm::vec3(0.22f, 0.23f, 0.3f));
        m.box({ 0.0f, -11.0f, m_endZ - 3.0f }, { kSideX - 0.5f, 10.5f, 2.5f }, glm::vec3(0.22f, 0.23f, 0.3f));
        m.box({ 0.0f, 0.01f, m_endZ - 0.4f }, { kSideX, 0.02f, 0.3f }, glm::vec3(1.0f, 0.85f, 0.2f));
        // The steel frame under the panes (drawn only: nobody walks on it).
        const float z0 = rowZ(0) + kPaneHalf, z1 = rowZ(kRows - 1) - kPaneHalf;
        for (int col = 0; col < kCols; ++col)
            for (float dx : { -kPaneHalf - 0.05f, kPaneHalf + 0.05f })
                m.box({ colX(col) + dx, -0.18f, (z0 + z1) * 0.5f }, { 0.05f, 0.1f, (z0 - z1) * 0.5f + 0.4f }, glm::vec3(0.55f, 0.58f, 0.65f));
        for (int r = 0; r < kRows; ++r)
            for (float dz : { -kPaneHalf - 0.05f, kPaneHalf + 0.05f })
                m.box({ 0.0f, -0.18f, rowZ(r) + dz }, { kPaneX + kPaneHalf + 0.1f, 0.05f, 0.05f }, glm::vec3(0.55f, 0.58f, 0.65f));
        // The floor far below, where the shards (and the unlucky) land.
        a.staticBox({ 0.0f, kFloorY - 0.5f, -14.0f }, { 22.0f, 0.5f, 34.0f }, glm::vec3(0.2f, 0.2f, 0.26f));
        // The panes: one tempered per row (none on an all-fake row). They
        // all look the same.
        for (int r = 0; r < kRows; ++r) {
            const int safe = rng.below(kCols);
            for (int col = 0; col < kCols; ++col) {
                Pane p;
                p.row = r;
                p.side = col;
                p.centre = glm::vec3(colX(col), -kThick, rowZ(r));
                p.tempered = col == safe && !m_leap[static_cast<size_t>(r)];
                kke::RigidWorld::BodyDesc d;
                d.motion = kke::RigidWorld::Motion::Static;
                d.halfExtents = { kPaneHalf, kThick, kPaneHalf };
                d.position = p.centre;
                d.material = kke::AudioMaterialTable::Glass;
                std::vector<kke::Vertex> v;
                std::vector<uint32_t> idx;
                MeshBuilder{ v, idx }.box(glm::vec3(0.0f), d.halfExtents, glm::vec3(0.75f, 0.92f, 1.0f));
                makeGlass(v, 0, 1.2f, 0.06f);
                p.part = a.addPart(d, std::move(v), std::move(idx));
                a.part(p.part).translucent = true;
                a.part(p.part).roughness = 0.05f;
                m_panes.push_back(p);
            }
        }
    }

    void spawn(Arena& a, int index, int count, glm::vec3& feet, float& yaw) override {
        (void)a;
        const int perRow = 4;
        const int row = index / perRow, col = index % perRow;
        const int inRow = std::min(perRow, count - row * perRow);
        feet = { (static_cast<float>(col) - static_cast<float>(inRow - 1) * 0.5f) * 1.3f, 0.05f, 2.0f + static_cast<float>(row) * 1.3f };
        yaw = 0.0f;
    }

    void start(Arena& a) override {
        for (Bean& b : a.beans()) b.c = -1.0f; // on the start
    }

    void update(Arena& a, float dt) override {
        (void)dt;
        if (!a.playing()) return;
        kke::RigidWorld& w = a.world();
        for (Bean& b : a.beans()) {
            if (!b.active || b.hidden) continue;
            const glm::vec3 p = b.remote ? b.drawFeet : b.feet(w);
            // Standing on a pane: a fake one breaks; a tempered one is now known.
            if (std::abs(p.y) < 0.25f) {
                for (Pane& pane : m_panes) {
                    if (pane.broken || std::abs(p.x - pane.centre.x) > kPaneHalf + 0.1f || std::abs(p.z - pane.centre.z) > kPaneHalf + 0.1f) continue;
                    if (pane.tempered) {
                        m_known[static_cast<size_t>(pane.row)] = pane.side;
                    } else if (!b.remote) {
                        const glm::vec2 hit = glm::clamp(glm::vec2(p.x - pane.centre.x, p.z - pane.centre.z), glm::vec2(-kPaneHalf), glm::vec2(kPaneHalf));
                        const int packed = static_cast<int>(std::lround((hit.x + 1.0f) * 100.0f)) * 1000 + static_cast<int>(std::lround((hit.y + 1.0f) * 100.0f));
                        a.event(kEventBreak, pane.row * kCols + pane.side, packed);
                    }
                }
            }
            if (!b.remote && p.z < m_endZ && p.y > -0.5f) a.finish(b);
            if (!b.remote) {
                int passed = 0;
                for (int r = 0; r < kRows; ++r)
                    if (p.z < rowZ(r) + kPaneHalf && p.y > -1.0f) passed = r + 1;
                b.result.score = std::max(b.result.score, static_cast<float>(passed));
            }
        }
    }

    void onEvent(Arena& a, int kind, int x, int y) override {
        if (kind != kEventBreak || x < 0 || x >= static_cast<int>(m_panes.size())) return;
        Pane& pane = m_panes[static_cast<size_t>(x)];
        if (pane.broken) return;
        pane.broken = true;
        // What the row now tells everyone: all its panes gone is a leap;
        // a normal row down to one is that one.
        int left = 0, last = -1;
        for (const Pane& o : m_panes)
            if (o.row == pane.row && !o.broken) {
                ++left;
                last = o.side;
            }
        if (left == 0) m_known[static_cast<size_t>(pane.row)] = kKnownLeap;
        else if (left == 1 && !m_leap[static_cast<size_t>(pane.row)]) m_known[static_cast<size_t>(pane.row)] = last;
        a.removePart(pane.part);
        // Whoever was on it here gets the moment to dive out.
        kke::RigidWorld& w = a.world();
        for (const Bean& b : a.beans()) {
            if (b.remote || !b.active || b.hidden) continue;
            const glm::vec3 p = b.feet(w);
            if (std::abs(p.y) < 0.5f && std::abs(p.x - pane.centre.x) < kPaneHalf + 0.2f && std::abs(p.z - pane.centre.z) < kPaneHalf + 0.2f) {
                if (m_brokeUnder.size() <= static_cast<size_t>(b.index)) m_brokeUnder.resize(static_cast<size_t>(b.index) + 1, -10.0f);
                m_brokeUnder[static_cast<size_t>(b.index)] = a.time();
            }
        }
        // Shards: Voronoi cells round where it was stepped on, each a thin
        // prism that falls (a Jolt convex hull).
        const glm::vec2 impact(static_cast<float>(y / 1000) / 100.0f - 1.0f, static_cast<float>(y % 1000) / 100.0f - 1.0f);
        ShardDesc sd;
        sd.halfSize = glm::vec2(kPaneHalf);
        sd.impact = impact;
        sd.pieces = 16;
        sd.seed = a.seed() * 31u + static_cast<uint32_t>(x);
        for (const Polygon2& cell : shatterPane(sd)) {
            const glm::vec2 c = polygonCentroid(cell);
            kke::RigidWorld::BodyDesc d;
            d.shape = kke::RigidWorld::Shape::ConvexHull;
            d.motion = kke::RigidWorld::Motion::Dynamic;
            d.position = pane.centre + glm::vec3(c.x, 0.0f, c.y);
            d.density = 2500.0f;
            d.restitution = 0.2f;
            d.material = kke::AudioMaterialTable::Glass;
            // Pushed down and out from the impact.
            const glm::vec2 out = c - impact;
            d.velocity = glm::vec3(out.x * 1.5f, -1.5f, out.y * 1.5f);
            d.angularVelocity = glm::vec3(out.y * 4.0f, 0.0f, -out.x * 4.0f);
            std::vector<kke::Vertex> v;
            std::vector<uint32_t> idx;
            prism(cell, c, d.points, v, idx);
            const int part = a.addPart(d, std::move(v), std::move(idx));
            a.part(part).translucent = true;
            a.part(part).roughness = 0.05f;
        }
        a.sound(pane.centre, kke::AudioMaterialTable::Glass, 1.0f);
        a.burst(pane.centre, glm::vec3(0.8f, 0.95f, 1.0f), 25, 3.0f);
    }

    // The save: a dive within a blink of the glass going under you.
    float airDiveLift(Arena& a, const Bean& b) const override {
        const size_t i = static_cast<size_t>(b.index);
        return i < m_brokeUnder.size() && a.time() - m_brokeUnder[i] <= kSaveWindow ? kSaveLift : 0.0f;
    }

    bool over(Arena& a) const override { return a.activeCount() == 0; }

    std::string beanStatus(Arena& a, const Bean& b) const override {
        (void)a;
        const int row = static_cast<int>(b.result.score);
        return row >= kRows ? std::string("Across!") : "Step " + std::to_string(std::min(kRows, row + 1)) + " of " + std::to_string(kRows);
    }

    BeanInput bot(Arena& a, Bean& b, float dt) override {
        BeanInput in;
        kke::RigidWorld& w = a.world();
        const glm::vec3 p = b.feet(w);
        // Falling through: the good ones dive out of it in time.
        const size_t bi = static_cast<size_t>(b.index);
        if (!b.grounded && bi < m_brokeUnder.size() && a.time() - m_brokeUnder[bi] < kSaveWindow && b.dive <= 0.0f && a.botRng().unit() < skill(b) * dt * 8.0f) {
            in.dive = true;
            in.move = { 0.0f, -1.0f }; // on, for the next row
            return in;
        }
        // The row they last stood on (-1: the start; c keeps it through a jump).
        if (b.grounded) {
            b.c = p.z > kStartEdge - 0.3f ? -1.0f : b.c;
            for (int r = 0; r < kRows; ++r)
                if (std::abs(p.z - rowZ(r)) < kPaneHalf + 0.3f) b.c = static_cast<float>(r);
        }
        const int row = static_cast<int>(b.c);
        if (row >= kRows - 1 || p.z < rowZ(kRows - 1) - kPaneHalf - 0.3f) {
            // The last pane: jump for the far side.
            in.move = steer(p, { p.x * 0.5f, 0.0f, m_endZ - 2.0f });
            if (b.grounded && p.z < rowZ(kRows - 1) - kPaneHalf + 0.5f && p.z > m_endZ + 0.5f) in.jump = true;
            return in;
        }
        // The next row, or the one after it when it's known to be all fake.
        int next = row + 1;
        const bool leap = m_known[static_cast<size_t>(next)] == kKnownLeap && next + 1 < kRows;
        if (leap) next = next + 1;
        // Choose a pane: known safe, else wait a bit for someone braver, then guess one still there.
        int side = m_known[static_cast<size_t>(next)];
        if (side < 0 && b.grounded) {
            if (b.i != next + 100) {
                b.i = next + 100; // this row's decision is new
                b.a = a.botRng().range(0.5f, 6.0f) * skill(b); // careful bots wait longer
                b.j = a.botRng().below(kCols);
            }
            if (paneBroken(next, b.j)) b.j = (b.j + 1 + a.botRng().below(kCols - 1)) % kCols;
            b.a -= dt;
            side = b.j;
        }
        if (side < 0) side = std::clamp(b.j, 0, kCols - 1);
        const glm::vec3 target(colX(side), 0.0f, rowZ(next));
        if (!b.grounded) {
            in.move = steer(p, target); // in the air: for the pane
            // A leap: dive at the top of the jump.
            if (leap && b.dive <= 0.0f && b.airTime > 0.45f && p.z > target.z + kPaneHalf) in.dive = true;
            return in;
        }
        // On the ground: to the front edge of where we stand (straight
        // ahead on a pane, lined up with the target on the start), then jump.
        const float front = row < 0 ? kStartEdge + 0.35f : rowZ(row) - kPaneHalf + 0.35f;
        const float x = row < 0 ? target.x : colX(std::clamp(static_cast<int>(std::lround(p.x / kPaneX)) + 1, 0, kCols - 1));
        // Wait while it's unknown and we're not brave yet, or someone's on
        // (or jumping for) that pane: two on one pane knock each other off.
        bool busy = false;
        for (const Bean& o : a.beans()) {
            if (&o == &b || !o.active || o.hidden) continue;
            const glm::vec3 q = o.remote ? o.drawFeet : o.feet(w);
            if (q.y > -0.5f && std::abs(q.x - target.x) < kPaneHalf + 0.4f && std::abs(q.z - target.z) < kPaneHalf + 0.6f) busy = true;
        }
        const bool waiting = busy || (m_known[static_cast<size_t>(next)] < 0 && b.a > 0.0f);
        in.move = steer(p, { x, 0.0f, waiting ? front + 0.4f : front - 0.3f }, waiting ? 0.6f : 1.0f);
        if (!waiting && p.z < front + 0.2f && std::abs(p.x - x) < 0.5f) in.jump = true;
        return in;
    }

private:
    struct Pane {
        int row = 0, side = 0, part = -1; // side: the column, 0..2
        glm::vec3 centre{0.0f};
        bool tempered = false, broken = false;
    };
    std::vector<Pane> m_panes;
    std::vector<int> m_known;        // per row: the column known to hold (-1: nobody knows yet, kKnownLeap: all fake)
    std::vector<bool> m_leap;        // per row: all fake
    std::vector<float> m_rowZ;       // per row: where it is
    std::vector<float> m_brokeUnder; // per bean (here): when the glass last went under them
    float m_endZ = 0.0f;

    float rowZ(int row) const { return m_rowZ[static_cast<size_t>(row)]; }
    bool paneBroken(int row, int col) const { return m_panes[static_cast<size_t>(row * kCols + col)].broken; }

    // A shard: the cell as a thin prism, centred on its own middle (its
    // body's origin), in body space for Jolt and as a mesh.
    static void prism(const Polygon2& cell, const glm::vec2& c, std::vector<glm::vec3>& hull, std::vector<kke::Vertex>& v, std::vector<uint32_t>& idx) {
        const size_t n = cell.size();
        for (int top = 0; top < 2; ++top)
            for (const glm::vec2& q : cell) hull.push_back({ q.x - c.x, top ? kThick : -kThick, q.y - c.y });
        const glm::vec3 tint(0.75f, 0.92f, 1.0f);
        auto vert = [&](const glm::vec3& pos, const glm::vec3& nrm) {
            v.push_back({ pos, tint, nrm, glm::vec2(1.2f, 0.06f) });
            return static_cast<uint32_t>(v.size() - 1);
        };
        // Top and bottom fans (the cell is counter-clockwise seen from above).
        for (int top = 0; top < 2; ++top) {
            const glm::vec3 nrm(0.0f, top ? 1.0f : -1.0f, 0.0f);
            const uint32_t first = static_cast<uint32_t>(v.size());
            for (size_t i = 0; i < n; ++i) vert(hull[static_cast<size_t>(top) * n + i], nrm);
            for (uint32_t i = 1; i + 1 < n; ++i) {
                // Seen from its own side: counter-clockwise from above is the top's front.
                if (top) idx.insert(idx.end(), { first, first + i + 1, first + i });
                else idx.insert(idx.end(), { first, first + i, first + i + 1 });
            }
        }
        for (size_t i = 0; i < n; ++i) {
            const glm::vec3 a0 = hull[i], a1 = hull[(i + 1) % n], b0 = hull[n + i], b1 = hull[n + (i + 1) % n];
            glm::vec3 nrm = glm::cross(a1 - a0, b0 - a0);
            if (glm::dot(nrm, a0 + a1) < 0.0f) nrm = -nrm; // outward (the centre is the origin)
            nrm = glm::normalize(nrm);
            const uint32_t s = static_cast<uint32_t>(v.size());
            vert(a0, nrm);
            vert(a1, nrm);
            vert(b1, nrm);
            vert(b0, nrm);
            if (glm::dot(glm::cross(a1 - a0, b1 - a0), nrm) > 0.0f) idx.insert(idx.end(), { s, s + 1, s + 2, s, s + 2, s + 3 });
            else idx.insert(idx.end(), { s, s + 2, s + 1, s, s + 3, s + 2 });
        }
    }
};

} // namespace

std::unique_ptr<Minigame> makeGlassBridge() { return std::make_unique<GlassBridge>(); }

} // namespace party
