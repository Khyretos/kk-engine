#include "Court.h"

#include <algorithm>
#include <cmath>

namespace tennis {

float netHeight(float x) {
    const float t = std::clamp(std::abs(x) / kPostX, 0.0f, 1.0f);
    return kNetHeightCentre + (kNetHeightPost - kNetHeightCentre) * t * t;
}

bool inCourt(float x, float z, int side, bool doubles) {
    const float r = kBallRadius;
    const float half = doubles ? kDoublesHalfWidth : kSinglesHalfWidth;
    const float into = z * static_cast<float>(side); // metres into that half
    return into >= -r && into <= kHalfLength + r && std::abs(x) <= half + r;
}

bool inServiceBox(float x, float z, int serverSide, bool deuceCourt) {
    const float r = kBallRadius;
    const float s = static_cast<float>(serverSide);
    const float into = -z * s; // into the receiver's half
    if (into < -r || into > kServiceLine + r) return false;
    // The server's right is +x on the +1 side: the deuce box across the
    // net is the receiver's right, so on the server's left (x * s < 0).
    const float xs = x * s;
    return deuceCourt ? (xs >= -kSinglesHalfWidth - r && xs <= r) : (xs >= -r && xs <= kSinglesHalfWidth + r);
}

glm::vec3 servePosition(int side, bool deuceCourt, bool doubles) {
    const float s = static_cast<float>(side);
    const float x = (doubles ? 1.6f : 0.7f) * (deuceCourt ? 1.0f : -1.0f) * s;
    return { x, 0.0f, s * (kHalfLength + 0.35f) };
}

glm::vec3 receivePosition(int side, bool deuceCourt, bool doubles) {
    // The receiver's right is +x on the +1 side too.
    const float s = static_cast<float>(side);
    const float x = (doubles ? 3.6f : 2.6f) * (deuceCourt ? 1.0f : -1.0f) * s;
    return { x, 0.0f, s * (kHalfLength + 0.6f) };
}

glm::vec3 partnerPosition(int side, bool deuceCourt, bool serving) {
    // The server's partner at the net on the other half; the receiver's
    // partner on the service line, same.
    const float s = static_cast<float>(side);
    const float x = 2.6f * (deuceCourt ? -1.0f : 1.0f) * s;
    return { x, 0.0f, s * (serving ? 3.2f : kServiceLine) };
}

glm::vec3 CourtPlace::toWorld(const glm::vec3& local) const { return origin + dirToWorld(local); }

glm::vec3 CourtPlace::dirToWorld(const glm::vec3& local) const {
    const float a = glm::radians(yawDegrees), c = std::cos(a), s = std::sin(a);
    return { c * local.x + s * local.z, local.y, -s * local.x + c * local.z };
}

glm::vec3 CourtPlace::toLocal(const glm::vec3& world) const { return dirToLocal(world - origin); }

glm::vec3 CourtPlace::dirToLocal(const glm::vec3& world) const {
    const float a = glm::radians(yawDegrees), c = std::cos(a), s = std::sin(a);
    return { c * world.x - s * world.z, world.y, s * world.x + c * world.z };
}

bool CourtPlace::contains(const glm::vec3& world, float margin) const {
    const glm::vec3 l = toLocal(world);
    return std::abs(l.x) <= kFenceHalfX + margin && std::abs(l.z) <= kFenceHalfZ + margin;
}

SportCenter::SportCenter() {
    // Two rows of five with their ends to the promenade: people walking
    // by look down the court like a camera behind the baseline, and the
    // bleachers sit in the gaps between the courts.
    const float pitchX = 2.0f * kFenceHalfX + kGapX;
    const float rowZ = kFenceHalfZ + kPromenade * 0.5f;
    for (int i = 0; i < kCourts; ++i) {
        const int row = i / kPerRow, col = i % kPerRow;
        CourtPlace& c = courts[static_cast<size_t>(i)];
        c.origin = { (static_cast<float>(col) - (kPerRow - 1) * 0.5f) * pitchX, 0.0f, row == 0 ? -rowZ : rowZ };
        c.yawDegrees = 0.0f;
    }
    const float halfX = (kPerRow * 0.5f) * pitchX + 4.0f;
    const float halfZ = rowZ + kFenceHalfZ + 4.0f;
    hallMin = { -halfX, 0.0f, -halfZ };
    hallMax = { halfX, 16.0f, halfZ };
}

int SportCenter::courtAt(const glm::vec3& world) const {
    for (int i = 0; i < kCourts; ++i)
        if (courts[static_cast<size_t>(i)].contains(world)) return i;
    return -1;
}

glm::vec3 SportCenter::arrival(int index) const {
    // A loose grid along the promenade, 1.5 m apart.
    const int col = index % 12, row = (index / 12) % 5;
    return { (static_cast<float>(col) - 5.5f) * 1.5f, 0.0f, (static_cast<float>(row) - 2.0f) * 1.5f };
}

std::vector<SportCenter::Seat> SportCenter::seats(int court) const {
    std::vector<Seat> out;
    if (court < 0 || court >= kCourts) return out;
    const CourtPlace& c = courts[static_cast<size_t>(court)];
    // Along both long fences, in the gap between courts: people sitting
    // on a bench (their feet on the ground in front of it) and people
    // standing behind it.
    for (int sideX : { -1, 1 }) {
        const float x = static_cast<float>(sideX) * (kFenceHalfX + 1.2f);
        for (int k = 0; k < 9; ++k) {
            const float z = (static_cast<float>(k) - 4.0f) * 2.6f;
            const glm::vec3 w = c.toWorld({ x, 0.0f, z });
            // Facing the court: toward -sideX in court space.
            const float yaw = c.yawDegrees + (sideX > 0 ? -90.0f : 90.0f);
            out.push_back({ w, yaw, true });
            out.push_back({ c.toWorld({ x + static_cast<float>(sideX) * 1.6f, 0.0f, z + 1.3f }), yaw, false });
        }
    }
    // Behind the baseline on the promenade side: a standing crowd.
    const float end = c.origin.z < 0.0f ? 1.0f : -1.0f; // the promenade is toward z = 0
    for (int k = 0; k < 8; ++k) {
        const float x = (static_cast<float>(k) - 3.5f) * 1.6f;
        const glm::vec3 w = c.toWorld({ x, 0.0f, end * (kFenceHalfZ + 1.5f) });
        out.push_back({ w, c.yawDegrees + (end > 0.0f ? 180.0f : 0.0f), false });
    }
    return out;
}

} // namespace tennis
