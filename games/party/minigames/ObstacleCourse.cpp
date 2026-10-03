// Obstacle Dash: a race down a course of spinning sweepers, a gap with
// sliding platforms, swinging hammers, a turning disc, a row of doors
// (some break, some don't) and giant balls rolling down the last slope.
// Fall off and you're back at the last checkpoint. The first 60% over
// the line go through; the round ends when they have (Fall Guys' races).
//
// Everything that moves follows the clock from GO (the same on every
// machine), so nothing but the doors needs sending online.

#include "../Minigame.h"

#include "kke/DevTools.h"
#include "kke/ImpactSynth.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace party {

namespace {

constexpr float kHalfWidth = 5.0f;
constexpr float kPi = 3.14159265f;
const glm::vec3 kTrack(0.98f, 0.86f, 0.72f), kTrackEdge(0.95f, 0.55f, 0.65f), kObstacle(0.35f, 0.6f, 1.0f), kDanger(1.0f, 0.35f, 0.35f);
const glm::quat kNoTurn(1.0f, 0.0f, 0.0f, 0.0f);

// The course, start to finish (z goes negative).
constexpr float kStartZ = 0.0f;
constexpr float kSweepZ0 = -12.0f, kSweepZ1 = -34.0f;
constexpr float kGapZ0 = -36.0f, kGapZ1 = -54.0f;
constexpr float kHammerZ0 = -56.0f, kHammerZ1 = -76.0f;
constexpr float kDiscZ = -88.0f, kDiscR = 8.0f;
constexpr float kDoorZ = -104.0f;
constexpr float kSlopeZ0 = -108.0f, kSlopeZ1 = -134.0f, kSlopeRise = 5.0f;
constexpr float kFinishZ = -140.0f;
const float kCheckpoints[] = { kStartZ - 4.0f, kGapZ0 + 1.0f, kHammerZ0 - 1.0f, kDiscZ + kDiscR + 3.0f, kDoorZ + 3.0f, kSlopeZ0 - 1.0f };

struct Sweeper {
    glm::vec3 centre;
    float half, speed, phase;
    int part = -1;
};
struct Slider {
    float z, range, speed, phase;
    int part = -1;
};
struct Hammer {
    float z, speed, phase;
    int part = -1;
};
struct Ball {
    float offset;
    int part = -1;
};

class ObstacleCourse final : public Minigame {
public:
    const char* id() const override { return "obstacle"; }
    const char* title() const override { return "Obstacle Dash"; }
    const char* goal() const override { return "Race to the finish! Dodge the sweepers, hammers and balls. The first 60% go through."; }
    const char* mood() const override { return "clear_day"; }
    float timeLimit() const override { return 150.0f; }
    float killY() const override { return -6.0f; }

    void build(Arena& a) override {
        m_sweepers.clear();
        m_sliders.clear();
        m_hammers.clear();
        m_balls.clear();
        m_doorParts.clear();
        m_doorBroken.clear();
        Rng& rng = a.rng();
        auto floor = [&](float z0, float z1, float y = 0.0f) {
            const float mid = (z0 + z1) * 0.5f, half = std::abs(z0 - z1) * 0.5f;
            a.staticBox({ 0.0f, y - 0.5f, mid }, { kHalfWidth, 0.5f, half }, kTrack);
            // Low kerbs down the sides (you can still be knocked over them).
            a.levelMesh().box({ -kHalfWidth - 0.15f, y + 0.1f, mid }, { 0.15f, 0.2f, half }, kTrackEdge);
            a.levelMesh().box({ kHalfWidth + 0.15f, y + 0.1f, mid }, { 0.15f, 0.2f, half }, kTrackEdge);
        };
        // Start pad and the sweepers' floor.
        floor(6.0f, kSweepZ1 - 2.0f);
        a.levelMesh().box({ 0.0f, 0.01f, kStartZ - 3.0f }, { kHalfWidth, 0.02f, 0.15f }, glm::vec3(1.0f));
        for (int i = 0; i < 3; ++i) {
            Sweeper s;
            s.centre = glm::vec3(0.0f, 0.0f, kSweepZ0 - 4.0f - 7.0f * static_cast<float>(i));
            s.half = kHalfWidth - 0.2f;
            s.speed = (i % 2 ? -1.0f : 1.0f) * rng.range(1.6f, 2.3f);
            s.phase = rng.range(0.0f, 2.0f * kPi);
            std::vector<kke::Vertex> v;
            std::vector<uint32_t> idx;
            MeshBuilder mb{ v, idx };
            mb.cylinder({ 0.0f, 0.0f, 0.0f }, 0.35f, 0.35f, 0.9f, kObstacle, 16);
            mb.box({ 0.0f, 0.55f, 0.0f }, { s.half, 0.18f, 0.18f }, kDanger);
            s.part = a.addVisual(std::move(v), std::move(idx));
            m_sweepers.push_back(s);
        }
        // The gap: three platforms sliding side to side over nothing.
        floor(kGapZ1 - 1.0f, kHammerZ1 - 2.0f);
        for (int i = 0; i < 3; ++i) {
            Slider s;
            s.z = kGapZ0 - 3.0f - 5.5f * static_cast<float>(i);
            s.range = 3.0f;
            s.speed = rng.range(0.9f, 1.4f);
            s.phase = rng.range(0.0f, 2.0f * kPi);
            kke::RigidWorld::BodyDesc d;
            d.motion = kke::RigidWorld::Motion::Kinematic;
            d.halfExtents = { 1.8f, 0.3f, 1.9f };
            d.position = { 0.0f, -0.3f, s.z };
            d.friction = 1.0f;
            std::vector<kke::Vertex> v;
            std::vector<uint32_t> idx;
            MeshBuilder{ v, idx }.box(glm::vec3(0.0f), d.halfExtents, glm::vec3(0.55f, 0.9f, 0.55f));
            s.part = a.addPart(d, std::move(v), std::move(idx));
            m_sliders.push_back(s);
        }
        // Hammers: swinging across the track from a beam overhead.
        for (int i = 0; i < 3; ++i) {
            Hammer h;
            h.z = kHammerZ0 - 4.0f - 6.0f * static_cast<float>(i);
            h.speed = rng.range(1.5f, 2.1f);
            h.phase = rng.range(0.0f, 2.0f * kPi);
            a.levelMesh().box({ 0.0f, 7.2f, h.z }, { kHalfWidth + 0.6f, 0.2f, 0.2f }, kObstacle);
            for (int s = -1; s <= 1; s += 2) a.levelMesh().box({ (kHalfWidth + 0.6f) * static_cast<float>(s), 3.6f, h.z }, { 0.2f, 3.6f, 0.2f }, kObstacle);
            std::vector<kke::Vertex> v;
            std::vector<uint32_t> idx;
            MeshBuilder mb{ v, idx };
            mb.box({ 0.0f, -2.9f, 0.0f }, { 0.08f, 2.9f, 0.08f }, glm::vec3(0.8f));
            mb.box({ 0.0f, -6.4f, 0.0f }, { 0.75f, 0.75f, 1.1f }, kDanger);
            h.part = a.addVisual(std::move(v), std::move(idx));
            m_hammers.push_back(h);
        }
        // The disc: a turning round platform, and a bridge on and off it.
        {
            floor(kDiscZ + kDiscR + 1.0f, kDiscZ + kDiscR - 0.5f);
            kke::RigidWorld::BodyDesc d;
            d.shape = kke::RigidWorld::Shape::ConvexHull;
            d.motion = kke::RigidWorld::Motion::Kinematic;
            d.position = { 0.0f, 0.0f, kDiscZ };
            d.friction = 1.0f;
            for (int k = 0; k < 24; ++k) {
                const float th = 2.0f * kPi * static_cast<float>(k) / 24.0f;
                d.points.push_back({ std::cos(th) * kDiscR, 0.0f, std::sin(th) * kDiscR });
                d.points.push_back({ std::cos(th) * kDiscR, -0.6f, std::sin(th) * kDiscR });
            }
            std::vector<kke::Vertex> v;
            std::vector<uint32_t> idx;
            MeshBuilder mb{ v, idx };
            mb.cylinder({ 0.0f, -0.6f, 0.0f }, kDiscR, kDiscR, 0.6f, glm::vec3(1.0f, 0.8f, 0.3f), 40);
            // Stripes, to see it turn, and bumpers.
            for (int k = 0; k < 6; ++k) {
                const size_t from = v.size();
                mb.box({ kDiscR * 0.5f, 0.02f, 0.0f }, { kDiscR * 0.5f, 0.02f, 0.25f }, glm::vec3(1.0f, 0.55f, 0.2f));
                mb.transform(from, glm::rotate(glm::mat4(1.0f), static_cast<float>(k) * kPi / 3.0f, glm::vec3(0, 1, 0)));
            }
            m_disc = a.addPart(d, std::move(v), std::move(idx));
            m_discSpeed = rng.range(0.35f, 0.5f) * (rng.unit() < 0.5f ? -1.0f : 1.0f);
            floor(kDiscZ - kDiscR + 0.5f, kDoorZ + 1.0f);
        }
        // Doors: five in a wall, two of them solid. Run into the others.
        {
            floor(kDoorZ + 1.0f, kSlopeZ0);
            const int solidA = rng.below(5);
            int solidB = rng.below(4);
            if (solidB >= solidA) ++solidB;
            const float w = 2.0f * kHalfWidth / 5.0f;
            a.staticBox({ 0.0f, 3.2f, kDoorZ }, { kHalfWidth, 0.6f, 0.25f }, kObstacle);
            for (int k = 0; k < 5; ++k) {
                const float x = -kHalfWidth + w * (static_cast<float>(k) + 0.5f);
                kke::RigidWorld::BodyDesc d;
                d.motion = kke::RigidWorld::Motion::Static;
                d.halfExtents = { w * 0.5f - 0.05f, 1.3f, 0.2f };
                d.position = { x, 1.3f, kDoorZ };
                std::vector<kke::Vertex> v;
                std::vector<uint32_t> idx;
                MeshBuilder mb{ v, idx };
                mb.box(glm::vec3(0.0f), d.halfExtents, beanColour(k + 1));
                mb.ellipsoid({ 0.0f, 0.0f, 0.22f }, { 0.25f, 0.25f, 0.05f }, glm::vec3(1.0f), 12, 6);
                const int p = a.addPart(d, std::move(v), std::move(idx));
                m_doorParts.push_back(p);
                m_doorBroken.push_back(false);
                m_doorSolid[k] = k == solidA || k == solidB;
            }
        }
        // The last slope, with balls rolling down it, and the finish.
        {
            const float len = std::abs(kSlopeZ1 - kSlopeZ0);
            const float angle = std::atan2(kSlopeRise, len);
            const glm::quat slope = glm::angleAxis(angle, glm::vec3(1, 0, 0));
            const float half = std::sqrt(len * len + kSlopeRise * kSlopeRise) * 0.5f;
            a.staticBox({ 0.0f, kSlopeRise * 0.5f - 0.5f, (kSlopeZ0 + kSlopeZ1) * 0.5f }, { kHalfWidth, 0.5f, half }, kTrack, slope);
            floor(kSlopeZ1, kFinishZ - 8.0f, kSlopeRise);
            // The finish arch.
            for (int s = -1; s <= 1; s += 2) a.levelMesh().box({ (kHalfWidth - 0.3f) * static_cast<float>(s), kSlopeRise + 2.2f, kFinishZ }, { 0.3f, 2.2f, 0.3f }, kTrackEdge);
            a.levelMesh().box({ 0.0f, kSlopeRise + 4.6f, kFinishZ }, { kHalfWidth, 0.4f, 0.3f }, glm::vec3(1.0f, 0.85f, 0.25f));
            for (int k = 0; k < 10; ++k)
                a.levelMesh().box({ -kHalfWidth + 0.5f + static_cast<float>(k), kSlopeRise + 0.01f, kFinishZ }, { 0.5f, 0.02f, 0.4f },
                                  k % 2 ? glm::vec3(0.1f) : glm::vec3(1.0f));
            // The chute at the top and the drain at the bottom (dark slots across the track).
            a.levelMesh().box({ 0.0f, kSlopeRise + 0.012f, kSlopeZ1 - 3.0f }, { kHalfWidth - 0.4f, 0.01f, kBallR }, glm::vec3(0.08f, 0.06f, 0.1f));
            a.levelMesh().box({ 0.0f, 0.012f, kSlopeZ0 + 2.0f }, { kHalfWidth - 0.4f, 0.01f, kBallR }, glm::vec3(0.08f, 0.06f, 0.1f));
            for (int k = 0; k < 3; ++k) {
                Ball b;
                b.offset = static_cast<float>(k) / 3.0f;
                std::vector<kke::Vertex> v;
                std::vector<uint32_t> idx;
                MeshBuilder{ v, idx }.ellipsoid(glm::vec3(0.0f), glm::vec3(kBallR), beanColour(3 + k * 2), 20, 12);
                b.part = a.addVisual(std::move(v), std::move(idx));
                m_balls.push_back(b);
            }
        }
        // Scenery: a pool of pink slime far below, clouds of balloons.
        a.levelMesh().box({ 0.0f, -14.0f, -70.0f }, { 80.0f, 0.2f, 130.0f }, glm::vec3(0.95f, 0.45f, 0.8f));
        for (int k = 0; k < 24; ++k) {
            const float side = k % 2 ? 1.0f : -1.0f;
            const glm::vec3 p(side * rng.range(9.0f, 26.0f), rng.range(2.0f, 14.0f), rng.range(-150.0f, 10.0f));
            a.levelMesh().ellipsoid(p, glm::vec3(rng.range(0.8f, 1.6f)), beanColour(rng.below(12)), 12, 8);
        }
        m_time = 0.0f;
        place(a, 0.0f);
    }

    void spawn(Arena& a, int index, int count, glm::vec3& feet, float& yaw) override {
        (void)a;
        const int perRow = 6;
        const int row = index / perRow, col = index % perRow;
        const int inRow = std::min(perRow, count - row * perRow);
        feet = glm::vec3((static_cast<float>(col) - static_cast<float>(inRow - 1) * 0.5f) * 1.4f, 0.05f, kStartZ + 1.5f + static_cast<float>(row) * 1.6f);
        yaw = 0.0f;
        // KKE_OBSTACLE_FROM=<n>: start at checkpoint n (1..5) instead, to try a later part.
        if (const char* from = kke::dev::env("KKE_OBSTACLE_FROM")) {
            const int k = std::clamp(std::atoi(from), 0, static_cast<int>(sizeof(kCheckpoints) / sizeof(kCheckpoints[0])) - 1);
            feet.z += kCheckpoints[k] - kStartZ + 1.0f;
            if (kCheckpoints[k] < kSlopeZ0) feet.y += kSlopeRise * 0.1f;
        }
    }

    void update(Arena& a, float dt) override {
        m_time = a.time();
        place(a, dt);
        if (!a.playing()) return;
        kke::RigidWorld& w = a.world();
        for (Bean& b : a.beans()) {
            if (b.remote || !b.active || b.hidden) continue;
            const glm::vec3 p = b.feet(w);
            // Checkpoints, and the line.
            for (float z : kCheckpoints)
                if (p.z < z && b.checkpoint.z > z) {
                    b.checkpoint = glm::vec3(std::clamp(p.x, -3.0f, 3.0f), z < kSlopeZ0 ? kSlopeRise * 0.1f + 0.05f : 0.05f, z - 0.5f);
                    b.checkpointYaw = 0.0f;
                }
            if (p.z < kFinishZ && p.y > kSlopeRise - 1.0f) {
                a.finish(b);
                continue;
            }
            if (b.bumped > 0.0f) continue;
            hitBySweepers(a, b, p);
            hitByHammers(a, b, p);
            hitByBalls(a, b, p);
            hitDoors(a, b, p);
        }
    }

    void fell(Arena& a, Bean& b) override { a.respawn(b); }

    bool over(Arena& a) const override {
        const int n = static_cast<int>(a.beans().size());
        const int qualify = std::max(1, static_cast<int>(std::ceil(static_cast<float>(n) * 0.6f)));
        return a.finishedCount() >= qualify || a.activeCount() == 0;
    }

    void timeUp(Arena& a) override {
        // Whoever's furthest along ranks best among those who didn't finish.
        for (Bean& b : a.beans())
            if (!b.finished) b.result.score = -b.feet(a.world()).z;
    }

    std::string beanStatus(Arena& a, const Bean& b) const override {
        const int n = static_cast<int>(a.beans().size());
        const int qualify = std::max(1, static_cast<int>(std::ceil(static_cast<float>(n) * 0.6f)));
        const float left = std::max(0.0f, b.feet(a.world()).z - kFinishZ);
        return std::to_string(static_cast<int>(left)) + " m to go  ·  " + std::to_string(a.finishedCount()) + "/" + std::to_string(qualify) + " through";
    }

    void onEvent(Arena& a, int kind, int x, int y) override {
        (void)y;
        if (kind == 1) breakDoor(a, x);
    }

    BeanInput bot(Arena& a, Bean& b, float dt) override {
        BeanInput in;
        kke::RigidWorld& w = a.world();
        const glm::vec3 p = b.feet(w);
        const float skill = 0.75f + 0.1f * static_cast<float>(b.difficulty);
        // Each bot keeps a lane (b.a) and re-picks it now and then.
        b.botTimer -= dt;
        if (b.botTimer <= 0.0f) {
            b.botTimer = a.botRng().range(1.5f, 4.0f);
            b.a = a.botRng().range(-3.0f, 3.0f);
        }
        float targetX = b.a;
        const float ahead = p.z - 3.0f;
        // The gap: aim for the platform in front, wait at the edge for it.
        if (p.z < kGapZ0 + 1.5f && p.z > kGapZ1) {
            for (const Slider& s : m_sliders)
                if (s.z < p.z + 0.5f && s.z > p.z - 6.5f) {
                    const float sx = a.part(s.part).body != kke::RigidWorld::kNoBody ? w.position(a.part(s.part).body).x : 0.0f;
                    targetX = sx;
                    const bool onIt = std::abs(p.z - s.z) < 1.8f;
                    if (!onIt && std::abs(sx - p.x) > 1.2f && b.grounded) {
                        in.move = glm::vec2((sx - p.x) * 0.3f, 0.0f);
                        return in;
                    }
                    break;
                }
        }
        // The doors: head for the one picked; a solid one sends you to another.
        if (p.z < kDoorZ + 8.0f && p.z > kDoorZ - 1.0f) {
            const float wd = 2.0f * kHalfWidth / 5.0f;
            if (b.i < 0 || b.i > 4 || b.j != 1) {
                b.i = a.botRng().below(5);
                b.j = 1;
            }
            targetX = -kHalfWidth + wd * (static_cast<float>(b.i) + 0.5f);
            if (p.z < kDoorZ + 0.9f && std::abs(b.velocity.z) < 0.5f && b.grounded) b.c += dt;
            if (b.c > 0.6f) { // stuck at a solid door
                b.c = 0.0f;
                b.i = (b.i + 1 + a.botRng().below(4)) % 5;
            }
        }
        glm::vec3 to(targetX - p.x, 0.0f, ahead - p.z);
        to = glm::normalize(to);
        in.move = glm::vec2(to.x, to.z) * std::min(1.0f, skill);
        // Jump over a sweeper about to pass, or when the floor ends ahead.
        for (const Sweeper& s : m_sweepers) {
            const glm::vec2 r(p.x - s.centre.x, p.z - s.centre.z);
            if (glm::length(r) > s.half + 0.5f) continue;
            const float angle = s.speed * m_time + s.phase;
            const glm::vec2 d(std::cos(angle), -std::sin(angle)); // the bar's direction (x, z)
            const float perp = std::abs(r.x * d.y - r.y * d.x);
            if (perp < 1.1f + 0.25f * static_cast<float>(3 - b.difficulty) && b.grounded) in.jump = a.botRng().unit() < skill;
        }
        const glm::vec3 look = p + glm::vec3(to.x, 0.0f, to.z) * 1.4f + glm::vec3(0.0f, 0.5f, 0.0f);
        if (b.grounded && !w.raycast(look, glm::vec3(0, -1, 0), 2.5f).hit && a.botRng().unit() < skill) in.jump = true;
        // A dive over the line.
        if (p.z < kFinishZ + 2.5f && p.z > kFinishZ && p.y > kSlopeRise - 1.0f) in.dive = true;
        return in;
    }

private:
    static constexpr float kBallR = 1.3f;
    float m_time = 0.0f;
    std::vector<Sweeper> m_sweepers;
    std::vector<Slider> m_sliders;
    std::vector<Hammer> m_hammers;
    std::vector<Ball> m_balls;
    int m_disc = -1;
    float m_discSpeed = 0.4f;
    std::vector<int> m_doorParts;
    std::vector<bool> m_doorBroken;
    bool m_doorSolid[5] = {};

    float hammerAngle(const Hammer& h) const { return std::sin(h.speed * m_time + h.phase) * 1.15f; }
    // A ball rises out of the chute at the top, rolls down the slope and
    // drops into the drain at the bottom (before the doors), then comes
    // round again: it never rolls through the doors or jumps back up.
    glm::vec3 ballPos(const Ball& b) const {
        const float top = kSlopeZ1 - 3.0f, bottom = kSlopeZ0 + 2.0f; // the drain: on the flat between the doors and the slope
        const float s = std::fmod(m_time * 0.12f + b.offset, 1.0f); // 0 at the top .. 1 at the bottom
        const float z = top + s * (bottom - top);
        const float t = std::clamp((z - kSlopeZ1) / (kSlopeZ0 - kSlopeZ1), 0.0f, 1.0f);
        float y = z < kSlopeZ1 ? kSlopeRise : kSlopeRise * (1.0f - t);
        // Coming up out of the chute, going down the drain.
        const float dip = s < 0.06f ? 1.0f - s / 0.06f : s > 0.94f ? (s - 0.94f) / 0.06f : 0.0f;
        y -= dip * (kBallR * 2.0f + 0.3f);
        const float x = std::sin(b.offset * 11.0f + m_time * 0.7f) * 2.5f;
        return { x, y + kBallR, z };
    }
    static bool ballUp(const glm::vec3& c) { return c.y > kBallR * 0.6f; } // out of the chute or drain: it can hit

    // Moving parts where the clock says.
    void place(Arena& a, float dt) {
        for (const Sweeper& s : m_sweepers)
            a.movePart(s.part, s.centre, glm::angleAxis(s.speed * m_time + s.phase, glm::vec3(0, 1, 0)), dt);
        for (const Slider& s : m_sliders)
            a.movePart(s.part, { std::sin(s.speed * m_time + s.phase) * s.range, -0.3f, s.z }, kNoTurn, dt);
        for (const Hammer& h : m_hammers)
            a.movePart(h.part, { 0.0f, 7.0f, h.z }, glm::angleAxis(hammerAngle(h), glm::vec3(0, 0, 1)), dt);
        if (m_disc >= 0) a.movePart(m_disc, { 0.0f, 0.0f, kDiscZ }, glm::angleAxis(m_discSpeed * m_time, glm::vec3(0, 1, 0)), dt);
        for (const Ball& b : m_balls)
            a.movePart(b.part, ballPos(b), glm::angleAxis(m_time * 2.4f + b.offset * 5.0f, glm::vec3(1, 0, 0)), dt);
    }

    void hitBySweepers(Arena& a, Bean& b, const glm::vec3& p) {
        if (p.y > 0.9f) return; // jumped over it
        for (const Sweeper& s : m_sweepers) {
            const glm::vec2 r(p.x - s.centre.x, p.z - s.centre.z);
            const float angle = s.speed * m_time + s.phase;
            const glm::vec2 d(std::cos(angle), -std::sin(angle));
            const float along = std::clamp(glm::dot(r, d), -s.half, s.half);
            const glm::vec2 perp = r - d * along;
            if (glm::length(perp) > 0.2f + kBeanRadius) continue;
            // Swept along the way the bar moves there: w x r, turning about +Y.
            glm::vec2 sweep = s.speed * along * glm::vec2(d.y, -d.x);
            const float speed = std::max(5.0f, glm::length(sweep) * 1.4f);
            sweep = glm::length(sweep) > 1e-3f ? glm::normalize(sweep) : glm::vec2(perp.x, perp.y) / std::max(glm::length(perp), 1e-3f);
            a.knock(b, glm::vec3(sweep.x, 0.0f, sweep.y) * speed + glm::vec3(0.0f, 4.5f, 0.0f), 0.7f);
            a.sound(p + glm::vec3(0.0f, 0.6f, 0.0f), kke::AudioMaterialTable::Plastic, 0.7f);
            b.bumped = 0.5f;
            return;
        }
    }
    void hitByHammers(Arena& a, Bean& b, const glm::vec3& p) {
        for (const Hammer& h : m_hammers) {
            const float ang = hammerAngle(h);
            const glm::vec3 head = glm::vec3(0.0f, 7.0f, h.z) + glm::vec3(std::sin(ang) * 6.4f, -std::cos(ang) * 6.4f, 0.0f);
            const glm::vec3 body = p + glm::vec3(0.0f, kBeanHeight * 0.5f, 0.0f);
            const glm::vec3 d = body - head;
            if (std::abs(d.x) > 0.75f + kBeanRadius || std::abs(d.y) > 0.75f + 0.65f || std::abs(d.z) > 1.1f + kBeanRadius) continue;
            const float swing = std::cos(h.speed * m_time + h.phase) * h.speed; // sign: which way it's going
            a.knock(b, glm::vec3(swing >= 0.0f ? 9.0f : -9.0f, 5.0f, 0.0f), 1.0f);
            a.sound(body, kke::AudioMaterialTable::Wood, 0.9f);
            b.bumped = 0.6f;
            return;
        }
    }
    void hitByBalls(Arena& a, Bean& b, const glm::vec3& p) {
        for (const Ball& ball : m_balls) {
            const glm::vec3 c = ballPos(ball);
            if (!ballUp(c)) continue;
            const glm::vec3 d = p + glm::vec3(0.0f, kBeanHeight * 0.5f, 0.0f) - c;
            if (glm::length(d) > kBallR + kBeanRadius + 0.1f) continue;
            glm::vec3 out = glm::normalize(glm::vec3(d.x, 0.0f, d.z) + glm::vec3(0.0f, 0.0f, 0.6f));
            a.knock(b, out * 8.0f + glm::vec3(0.0f, 5.0f, 0.0f), 0.9f);
            a.sound(c, kke::AudioMaterialTable::Rubber, 0.9f);
            b.bumped = 0.6f;
            return;
        }
    }
    void hitDoors(Arena& a, Bean& b, const glm::vec3& p) {
        if (std::abs(p.z - kDoorZ) > 0.2f + kBeanRadius + 0.15f) return;
        const float wd = 2.0f * kHalfWidth / 5.0f;
        const int k = std::clamp(static_cast<int>((p.x + kHalfWidth) / wd), 0, 4);
        if (m_doorBroken[static_cast<size_t>(k)]) return;
        if (m_doorSolid[k]) {
            a.knock(b, glm::vec3(0.0f, 1.5f, 3.0f), 0.3f);
            a.sound(p + glm::vec3(0.0f, 1.0f, 0.0f), kke::AudioMaterialTable::Wood, 0.5f);
            b.bumped = 0.4f;
            return;
        }
        a.event(1, k, 0); // it breaks, everywhere
    }
    void breakDoor(Arena& a, int k) {
        if (k < 0 || k >= static_cast<int>(m_doorParts.size()) || m_doorBroken[static_cast<size_t>(k)]) return;
        m_doorBroken[static_cast<size_t>(k)] = true;
        const float wd = 2.0f * kHalfWidth / 5.0f;
        const glm::vec3 at(-kHalfWidth + wd * (static_cast<float>(k) + 0.5f), 1.3f, kDoorZ);
        a.removePart(m_doorParts[static_cast<size_t>(k)]);
        a.burst(at, beanColour(k + 1), 50, 6.0f);
        a.sound(at, kke::AudioMaterialTable::Wood, 1.0f);
    }
};

} // namespace

std::unique_ptr<Minigame> makeObstacleCourse() { return std::make_unique<ObstacleCourse>(); }

} // namespace party
