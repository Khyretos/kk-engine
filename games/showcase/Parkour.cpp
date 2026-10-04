// The parkour park (layout::kZones[1], north of the yard): Kees, 2026-10-03,
// "a bigger parkour park" with "wall climb, vault from a hang, shimmy".
//
// Nothing here is marked for the movement: Locomotion's area awareness
// reads every box (docs/MOVEMENT.md). Each section is built to show one
// move, and the HUD names it when you're there:
//
//   VAULT FIELD      fences, low walls and boxes in a row: run and vault
//   WALL CLIMB       walls of 3, 3.6 and 4.2 m: run at one, jump, run up it
//   HANG VAULT       thin walls: hang from the top, jump again to go over
//   SHIMMY WALL      an L of thin wall: hang, shimmy round the corner, leap the gap
//   LEDGE LEAPS      pillars with rising tops: hang, leap to the next
//   WALL RUN         two tall walls side by side: run along one, kick to the other
//   ROOFTOPS         seven roofs with gaps between: steps up, then run and jump
//
// KKE_DEMO_PARKOUR=1 runs the wall climb, the hang vault, the shimmy and the
// rooftops by itself (logs each move: checks, screenshots).

#include "ShowcaseModule.h"
#include "Geometry.h"

#include "kke/Log.h"
#include "kke/modules/RigidBodyModule.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <iterator>

namespace kke_showcase {

namespace {


// The rooftops, west to east: x from, x to, roof height (all z -198..-186).
struct Roof { float x0, x1, h; };
constexpr Roof kRoofs[] = {
    { -40.0f, -32.0f, 3.5f }, { -30.0f, -21.0f, 4.5f }, { -18.8f, -10.0f, 4.0f }, { -7.5f, 2.0f, 5.5f },
    { 4.0f, 14.0f, 5.0f },    { 16.4f, 26.0f, 4.2f },   { 28.0f, 38.0f, 3.5f },
};
constexpr float kRoofZ0 = -198.0f, kRoofZ1 = -186.0f;
// The wall climb walls: x middle and height (faces at z = -118, 3 m deep).
struct Climb { float x, h; };
constexpr Climb kClimbWalls[] = { { 12.0f, 3.0f }, { 22.0f, 3.6f }, { 32.0f, 4.2f } };
constexpr float kClimbFace = -118.0f;

bool inRect(const glm::vec3& p, float x0, float x1, float z0, float z1) { return p.x >= x0 && p.x <= x1 && p.z >= z0 && p.z <= z1; }

} // namespace

void ShowcaseModule::buildParkourPark(std::vector<kke::Vertex>& v, std::vector<uint32_t>& idx) {
    const glm::vec3 concrete(0.6f, 0.6f, 0.58f), dark(0.42f, 0.43f, 0.45f), fence(0.75f, 0.62f, 0.35f), blue(0.35f, 0.55f, 0.85f),
        orange(0.9f, 0.55f, 0.2f), green(0.35f, 0.7f, 0.45f), purple(0.6f, 0.45f, 0.75f), red(0.8f, 0.35f, 0.3f), paint(0.85f, 0.85f, 0.3f);
    auto box = [&](glm::vec3 c, glm::vec3 half, glm::vec3 color) { addStaticBox({ c, half, color }, v, idx); };

    // VAULT FIELD (x -29..-21): run toward the far end (-Z), vault each one.
    const float vx = -25.0f;
    box({ vx, 0.005f, -126.0f }, { 4.0f, 0.005f, 15.0f }, dark); // the lane
    box({ vx, 0.5f, -114.0f }, { 4.0f, 0.5f, 0.12f }, fence);   // 1.0 m fence
    box({ vx, 0.35f, -119.0f }, { 4.0f, 0.35f, 0.25f }, orange); // 0.7 m wall
    box({ vx, 0.6f, -124.0f }, { 4.0f, 0.6f, 0.5f }, blue);     // 1.2 m box, 1 m deep
    box({ vx, 0.55f, -129.0f }, { 4.0f, 0.55f, 0.12f }, fence); // 1.1 m fence
    box({ vx, 0.45f, -134.0f }, { 4.0f, 0.45f, 0.25f }, orange); // 0.9 m wall
    box({ vx, 0.5f, -138.5f }, { 4.0f, 0.5f, 0.1f }, fence);    // two fences close together
    box({ vx, 0.5f, -140.0f }, { 4.0f, 0.5f, 0.1f }, fence);

    // WALL CLIMB: three walls, a runway painted in front of each.
    for (const Climb& c : kClimbWalls) {
        box({ c.x, c.h * 0.5f, kClimbFace - 1.5f }, { 3.5f, c.h * 0.5f, 1.5f }, c.h > 4.0f ? red : c.h > 3.3f ? purple : green);
        box({ c.x, 0.005f, kClimbFace + 6.0f }, { 0.6f, 0.005f, 6.0f }, paint);
        // A ladder of steps down the back, to come down and go again.
        box({ c.x + 2.5f, c.h * 0.25f, kClimbFace - 3.5f }, { 1.0f, c.h * 0.25f, 0.5f }, concrete);
    }

    // HANG VAULT: two thin walls (2.6 and 3.0 m), one behind the other.
    box({ -30.0f, 1.3f, -150.0f }, { 5.0f, 1.3f, 0.2f }, green);
    box({ -30.0f, 1.5f, -158.0f }, { 5.0f, 1.5f, 0.2f }, purple);

    // SHIMMY WALL: 3 m, thin (too thin to stand on, an edge to hang from),
    // along X, round an outside corner, then along -Z; a 1 m gap and a
    // taller piece (3.5 m) to leap to.
    box({ -5.0f, 1.5f, -150.3f }, { 7.0f, 1.5f, 0.3f }, blue);   // z face at -150 (x -12..2)
    box({ 1.7f, 1.5f, -156.6f }, { 0.3f, 1.5f, 6.0f }, blue);    // x face at 2 (z -162.6..-150.6)
    box({ 1.7f, 1.75f, -166.6f }, { 0.3f, 1.75f, 3.0f }, orange); // after a 1 m gap: top 3.5

    // LEDGE LEAPS: thin pillars, 1.2 m wide, 1.2 m apart, rising tops.
    const float tops[] = { 2.8f, 3.2f, 3.6f, 4.0f, 3.6f };
    for (size_t k = 0; k < std::size(tops); ++k)
        box({ 14.0f + 2.4f * static_cast<float>(k), tops[k] * 0.5f, -150.0f }, { 0.6f, tops[k] * 0.5f, 0.2f }, k % 2 ? purple : green);

    // WALL RUN: two 4 m walls, 3.5 m apart, 14 m long.
    box({ 41.8f, 2.0f, -135.0f }, { 0.2f, 2.0f, 7.0f }, red);
    box({ 45.7f, 2.0f, -135.0f }, { 0.2f, 2.0f, 7.0f }, red);
    box({ 43.75f, 0.005f, -118.0f }, { 1.5f, 0.005f, 10.0f }, paint); // run-up

    // ROOFTOPS: steps up onto the first, then roof to roof.
    for (size_t k = 0; k < std::size(kRoofs); ++k) {
        const Roof& r = kRoofs[k];
        const glm::vec3 c((r.x0 + r.x1) * 0.5f, r.h * 0.5f, (kRoofZ0 + kRoofZ1) * 0.5f);
        box(c, { (r.x1 - r.x0) * 0.5f, r.h * 0.5f, (kRoofZ1 - kRoofZ0) * 0.5f }, k % 2 ? concrete : dark);
        box({ c.x, r.h + 0.04f, c.z }, { (r.x1 - r.x0) * 0.5f - 0.25f, 0.04f, (kRoofZ1 - kRoofZ0) * 0.5f - 0.25f }, glm::vec3(0.33f, 0.33f, 0.36f));
    }
    box({ -36.0f, 0.6f, -181.0f }, { 4.0f, 0.6f, 1.0f }, concrete);  // steps: 1.2 m
    box({ -36.0f, 1.15f, -183.5f }, { 4.0f, 1.15f, 1.5f }, concrete); // 2.3 m, then the 3.5 m roof
    box({ 9.0f, 5.0f + 0.5f, -192.0f }, { 0.1f, 0.5f, 5.0f }, fence); // a rail across roof 5: vault it
    box({ -14.4f, 4.0f + 0.6f, -192.0f }, { 1.2f, 0.6f, 2.0f }, blue); // a box on roof 3: vault or climb
    box({ 40.0f, 1.0f, -192.0f }, { 1.5f, 1.0f, 3.0f }, concrete);   // a step down off the last roof
}

// The section you're in (HUD): true with its name and what to do.
bool ShowcaseModule::parkourStation(const glm::vec3& p, std::string& station, std::string& text) const {
    if (inRect(p, -30.0f, -20.0f, -142.0f, -100.0f)) {
        station = "VAULT FIELD";
        text = "Run at them and press jump {jump} to vault. Sprint {sprint} for a speed vault that keeps the run going.";
    } else if (inRect(p, 7.0f, 37.0f, -124.0f, -100.0f) && p.y < 0.5f) {
        station = "WALL CLIMB";
        text = "Run at a wall and press jump {jump}: you run up it and catch the top. Jump again to climb onto it.";
    } else if (inRect(p, -36.0f, -24.0f, -162.0f, -144.0f)) {
        station = "HANG VAULT";
        text = "Jump at the wall to hang from the top, then jump {jump} again: over it you go, down the other side.";
    } else if (inRect(p, -13.0f, 6.0f, -171.0f, -144.0f)) {
        station = "SHIMMY WALL";
        text = "Hang from the top {jump}, shimmy along with left and right {move}, round the corner. At the gap: jump sideways.";
    } else if (inRect(p, 12.0f, 26.0f, -156.0f, -144.0f)) {
        station = "LEDGE LEAPS";
        text = "Hang from a pillar, then jump {jump} with left or right to leap to the next. Up with nothing sideways.";
    } else if (inRect(p, 40.0f, 48.0f, -143.0f, -108.0f)) {
        station = "WALL RUN";
        text = "Sprint {sprint} beside a wall and jump {jump}: you run along it. Jump again to kick across to the other.";
    } else if (inRect(p, -42.0f, 42.0f, -200.0f, -178.0f)) {
        station = "ROOFTOPS";
        text = "Up the steps, then sprint {sprint} and jump {jump} the gaps. A roof too high: you catch the edge, jump to climb.";
    } else {
        return false;
    }
    return true;
}

// KKE_DEMO_PARKOUR=1: each move in turn, driven by what Locomotion is
// doing (not by the clock: physics runs slower than the frames on a slow
// machine). Steps: the 3.6 m wall climb and the climb onto it, the hang
// vault over the 2.6 m wall, the shimmy round the corner and the leap
// over the gap, then the rooftops. Logs each.
void ShowcaseModule::updateParkourDemo(float dt, kke::Locomotion::Input& in) {
    using State = kke::Locomotion::State;
    kke::RigidWorld& w = m_rigid->world();
    const glm::vec3 feet = w.characterPosition(m_player);
    const State st = m_loco->state();
    m_demoParkourT += dt;
    const float t = m_demoParkourT;
    auto log = [&](const char* what) {
        kke::log::get(name())->info("parkour demo: {} at {:.2f} {:.2f} {:.2f} ({:.1f} s, step {})", what, feet.x, feet.y, feet.z, t, m_demoParkourStep);
    };
    auto next = [&](int step) {
        m_demoParkourStep = step;
        m_demoParkourT = 0.0f;
    };
    auto start = [&](const glm::vec3& at, const glm::vec3& face, float yaw) {
        m_loco->teleport(at);
        m_loco->setFacing(face);
        m_rig.yaw = yaw;
        m_rig.pitch = -10.0f;
    };
    if (st != m_demoParkourState) {
        static const char* const names[] = { "ground", "air", "vault", "climb", "hang", "leap", "wall run" };
        kke::log::get(name())->info("parkour demo:   {} at {:.2f} {:.2f} {:.2f}", names[static_cast<int>(st)], feet.x, feet.y, feet.z);
        m_demoParkourState = st;
    }
    // Frozen mid-move for a screenshot.
    const std::string& fz = m_demoParkourFreeze;
    if ((fz == "wallclimb" && m_loco->wallClimbing() && m_loco->traversalProgress() > 0.45f) ||
        (fz == "hangvault" && m_loco->hangVaulting() && m_loco->traversalProgress() > 0.55f) ||
        (fz == "shimmy" && m_demoParkourStep == 8 && feet.z < -153.0f)) {
        if (!m_demoParkourFrozen) log("frozen for a screenshot");
        m_demoParkourFrozen = true;
    }
    if (m_demoParkourFrozen) {
        m_rig.yaw = fz == "shimmy" ? -55.0f : fz == "wallclimb" ? 50.0f : 120.0f;
        m_rig.pitch = -8.0f;
        return;
    }
    in.move = glm::vec3(0.0f);
    m_sprint = false;
    switch (m_demoParkourStep) {
    case 0: // to the 3.6 m wall
        if (t > 0.3f) {
            start({ 22.0f, 0.05f, -106.0f }, { 0, 0, -1 }, 25.0f);
            next(1);
        }
        break;
    case 1: // run at it, jump a step away: up the wall to the hang
        in.move = glm::vec3(0, 0, -1);
        if (st == State::Ground && feet.z < kClimbFace + 1.3f && t > 0.3f) m_jumpQueued = true;
        if (st == State::Leap && m_loco->wallClimbing() && m_demoParkourLast != 1) {
            m_demoParkourLast = 1;
            log("wall climb: running up the wall");
        }
        if (st == State::Hang) {
            log("wall climb: hanging from the top");
            next(2);
        }
        if (t > 40.0f) {
            log("wall climb: FAILED (no hang)");
            next(3);
        }
        break;
    case 2: // climb onto it
        if (t > 0.6f && t - dt <= 0.6f) m_jumpQueued = true;
        if (st == State::Ground && feet.y > 3.0f) {
            log("wall climb: on top of the 3.6 m wall");
            next(3);
        }
        if (t > 20.0f) {
            log("wall climb: FAILED (no climb)");
            next(3);
        }
        break;
    case 3: // to the 2.6 m thin wall, a run-up away
        start({ -30.0f, 0.05f, -140.0f }, { 0, 0, -1 }, 30.0f);
        m_demoParkourLast = 0;
        next(4);
        break;
    case 4: // run at it, jump: up the wall to the hang
        in.move = glm::vec3(0, 0, -1);
        if (st == State::Ground && feet.z < -148.5f && m_demoParkourLast != 4) {
            m_demoParkourLast = 4;
            m_jumpQueued = true;
        }
        if (st == State::Hang && t > 0.3f) {
            log("hang vault: hanging from the thin wall");
            next(5);
        }
        if (t > 30.0f) {
            log("hang vault: FAILED (no hang)");
            next(6);
        }
        break;
    case 5: // over it
        if (t > 0.6f && t - dt <= 0.6f) m_jumpQueued = true;
        if (st == State::Vault && m_loco->hangVaulting() && m_demoParkourLast != 5) {
            m_demoParkourLast = 5;
            log("hang vault: over the top");
        }
        if (st == State::Ground && t > 0.8f) {
            log(feet.z < -150.3f ? "hang vault: down on the far side" : "hang vault: FAILED (still this side)");
            next(6);
        }
        if (t > 20.0f) {
            log("hang vault: FAILED (no vault)");
            next(6);
        }
        break;
    case 6: // to the shimmy wall
        start({ -6.0f, 0.05f, -146.5f }, { 0, 0, -1 }, 20.0f);
        m_rig.pitch = -5.0f;
        next(7);
        break;
    case 7: // jump at it: hang
        in.move = glm::vec3(0, 0, -1) * 0.6f;
        if (st == State::Ground && feet.z < -148.6f && t > 0.2f) m_jumpQueued = true;
        if (st == State::Hang && t > 0.3f) {
            log("shimmy: hanging");
            next(8);
        }
        if (t > 30.0f) {
            log("shimmy: FAILED (no hang)");
            next(10);
        }
        break;
    case 8: // shimmy right (+X), round the corner; held, it keeps going along -Z
        in.move = glm::vec3(1, 0, 0);
        m_rig.yaw = feet.x > 1.0f ? 70.0f : 20.0f;
        if (m_demoParkourLast != 8 && feet.x > 2.0f) {
            m_demoParkourLast = 8;
            log("shimmy: round the corner");
        }
        if (st == State::Hang && feet.z < -161.2f) { // near the end (round it, it would go on along the back)
            log("shimmy: near the end of the wall");
            next(9);
        }
        if (st != State::Hang || t > 60.0f) {
            log("shimmy: FAILED (stopped)");
            next(10);
        }
        break;
    case 9: // leap over the gap to the taller piece
        in.move = glm::vec3(0, 0, -1);
        if (t > 0.3f && t - dt <= 0.3f) m_jumpQueued = true;
        if (st == State::Hang && t > 0.5f) {
            log(feet.z < -163.0f && m_loco->hangEdge().y > 3.4f ? "shimmy: leapt the gap, hanging from the 3.5 m piece" : "shimmy: FAILED (no leap)");
            next(10);
        }
        if (t > 15.0f) {
            log("shimmy: FAILED (no leap)");
            next(10);
        }
        break;
    case 10: // onto the first roof
        start({ -38.5f, kRoofs[0].h + 0.1f, -192.0f }, { 1, 0, 0 }, 90.0f);
        m_rig.pitch = -15.0f;
        m_demoParkourRoof = 0;
        next(11);
        break;
    case 11: { // sprint east, jumping each gap, climbing what's in the way
        in.move = glm::vec3(1, 0, 0);
        m_sprint = true;
        int on = -1;
        for (size_t k = 0; k < std::size(kRoofs); ++k)
            if (feet.x >= kRoofs[k].x0 - 0.2f && feet.x <= kRoofs[k].x1 + 0.2f && std::abs(feet.y - (kRoofs[k].h + 0.08f)) < 0.3f) on = static_cast<int>(k);
        if (on > m_demoParkourRoof) {
            m_demoParkourRoof = on;
            log(("rooftops: on roof " + std::to_string(on + 1)).c_str());
        }
        if (st == State::Ground && on >= 0 && feet.x > kRoofs[on].x1 - 0.9f && on + 1 < static_cast<int>(std::size(kRoofs))) m_jumpQueued = true;
        if (st == State::Ground) {
            const kke::Locomotion::Obstacle o = m_loco->probe(glm::vec3(1, 0, 0), m_loco->settings().sprintSensor);
            if (o.kind != kke::Locomotion::Obstacle::Kind::None) m_jumpQueued = true; // the rail, the box
        }
        if (st == State::Hang && m_loco->stateTime() > 0.3f) m_jumpQueued = true; // caught an edge: climb up
        if (m_demoParkourRoof == static_cast<int>(std::size(kRoofs)) - 1 || t > 120.0f || feet.y < 1.0f) {
            kke::log::get(name())->info("parkour demo: rooftops: reached roof {} of {}{}", m_demoParkourRoof + 1, std::size(kRoofs),
                                        feet.y < 1.0f && m_demoParkourRoof + 1 < static_cast<int>(std::size(kRoofs)) ? " (fell)" : "");
            next(12);
        }
        break;
    }
    default:
        if (m_demoParkourStep == 12) {
            kke::log::get(name())->info("parkour demo: done");
            m_demoParkourStep = 13;
        }
        break;
    }
}

} // namespace kke_showcase
