#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <array>
#include <vector>

// A tennis court's measurements and the sport center's layout (pure data,
// no engine: unit-tested in tests/test_tennis.cpp).
//
// Court space: the net runs along X at z = 0, the baselines are at
// z = +-kHalfLength, y is up and the court surface is y = 0. A team plays
// on one half, its "side": +1 (z > 0) or -1 (z < 0).
namespace tennis {

// ITF sizes, metres.
constexpr float kHalfLength = 11.885f;      // net to baseline
constexpr float kServiceLine = 6.40f;       // net to service line
constexpr float kSinglesHalfWidth = 4.115f;
constexpr float kDoublesHalfWidth = 5.485f;
constexpr float kNetHeightCentre = 0.914f;
constexpr float kNetHeightPost = 1.07f;
constexpr float kPostX = kDoublesHalfWidth + 0.914f; // net posts, 0.914 m outside the doubles lines
constexpr float kLineWidth = 0.05f;
// Room around the lines, to the fence (smaller than a tournament court's,
// so ten courts fit a sport center).
constexpr float kRunBack = 5.5f, kRunSide = 3.2f;
constexpr float kFenceHalfX = kDoublesHalfWidth + kRunSide;  // 8.685
constexpr float kFenceHalfZ = kHalfLength + kRunBack;        // 17.385
constexpr float kFenceHeight = 3.6f;                         // what people see
constexpr float kLidHeight = 14.0f;                          // the invisible roof that keeps a lob in
// The ball: a real tennis ball is 6.7 cm across; drawn a touch bigger so
// it reads on a phone.
constexpr float kBallRadius = 0.045f;

// The height of the net's top at x (it sags from the posts to the centre strap).
float netHeight(float x);

// Is a bounce at (x, z) in? `doubles` uses the tramlines. The ball is in
// when any part of it touches a line (its centre within the radius).
bool inCourt(float x, float z, int side, bool doubles);
// A serve: into the service box diagonally across. `serverSide` is the
// server's half (+1 / -1); `deuceCourt` = served from the right half as
// the server looks at the net (the first point of every game).
bool inServiceBox(float x, float z, int serverSide, bool deuceCourt);
// Where a server stands: behind the baseline, right or left of centre.
glm::vec3 servePosition(int side, bool deuceCourt, bool doubles);
// Where the receiver waits.
glm::vec3 receivePosition(int side, bool deuceCourt, bool doubles);
// Doubles: the server's (or receiver's) partner.
glm::vec3 partnerPosition(int side, bool deuceCourt, bool serving);
// The half a point is in: +1 or -1 (z = 0 counts as +1).
inline int sideOf(float z) { return z < 0.0f ? -1 : 1; }

// The sport center: courts in rows, each a transform from court space to
// the world. Court 0 is the one a match against the CPU uses.
struct CourtPlace {
    glm::vec3 origin{0.0f};   // world position of the court's centre (net middle, on the ground)
    float yawDegrees = 0.0f;  // about +Y
    glm::vec3 toWorld(const glm::vec3& local) const;
    glm::vec3 dirToWorld(const glm::vec3& local) const;
    glm::vec3 toLocal(const glm::vec3& world) const;
    glm::vec3 dirToLocal(const glm::vec3& world) const;
    // Inside this court's fence (with a margin > 0 grows it).
    bool contains(const glm::vec3& world, float margin = 0.0f) const;
};

struct SportCenter {
    static constexpr int kCourts = 10;
    static constexpr int kPerRow = 5;
    static constexpr float kGapX = 7.0f;      // between neighbouring fences: room for a bleacher
    static constexpr float kPromenade = 16.0f; // between the two rows: the walkway and the stands
    std::array<CourtPlace, kCourts> courts;
    glm::vec3 hallMin{0.0f}, hallMax{0.0f}; // the floor, walls around it
    SportCenter();
    // The court whose fence holds `world`, or -1.
    int courtAt(const glm::vec3& world) const;
    // Where someone arriving stands: the promenade's middle, spread out a bit.
    glm::vec3 arrival(int index) const;
    // Spectator spots beside court c (standing or sitting), facing the
    // court. Yaw: the way someone faces, (sin yaw, 0, cos yaw) in the world.
    struct Seat { glm::vec3 pos; float yawDegrees; bool sitting; };
    std::vector<Seat> seats(int court) const;
};

} // namespace tennis
