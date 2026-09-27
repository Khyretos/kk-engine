#include "kke/PlayBlocks.h"

#include "kke/Picking.h"

#include <algorithm>
#include <cmath>

namespace kke {

std::vector<PlayBlock> defaultPlayBlocks() {
    std::vector<PlayBlock> blocks;
    blocks.push_back({ "person", "Person", PlayBlockKind::Character,
                       { "SK_Character_Jock", "SK_Character_Tourist", "SK_Character_FireFighter", "SK_Character_Paramedic",
                         "SK_Character_Grandpa", "SK_Character_Grandma", "SK_Character_HipsterGirl", "SK_Character_PunkGuy",
                         "SK_Character_SummerGirl", "SK_Character_Roadworker", "SK_Character_Hotdog", "SK_Character_Female_Druid" },
                       true, 1.0f, {} });
    blocks.push_back({ "bat", "Bat", PlayBlockKind::Tool, { "SM_Prop_Bat_01" }, false, 1.0f, {} });
    blocks.push_back({ "crate", "Box", PlayBlockKind::Prop, { "SM_Prop_Crate_01", "SM_Prop_Crate_02", "SM_Prop_Barrel_01" }, false, 1.0f, {} });
    blocks.push_back({ "barrel", "Barrel", PlayBlockKind::Prop, { "SM_Prop_Barrel_01" }, false, 1.0f, {} });
    blocks.push_back({ "ball", "Ball", PlayBlockKind::Prop, { "SM_Primitive_SoccerBall_01", "SM_Prop_Bowling_Ball_01" }, false, 1.0f, {} });
    blocks.push_back({ "cone", "Cone", PlayBlockKind::Prop, { "SM_Primitive_Cone_01" }, false, 1.0f, {} });
    // Quaternius' animals are a few metres tall as made; the scales make them life size.
    blocks.push_back({ "sheep", "Sheep", PlayBlockKind::Animal, { "Sheep" }, false, 0.23f, "sheep" });
    blocks.push_back({ "cow", "Cow", PlayBlockKind::Animal, { "Cow" }, false, 0.3f, "cow" });
    blocks.push_back({ "pig", "Pig", PlayBlockKind::Animal, { "Pig" }, false, 0.2f, "pig" });
    blocks.push_back({ "horse", "Horse", PlayBlockKind::Animal, { "Horse" }, false, 0.28f, "horse" });
    return blocks;
}

std::vector<std::string> availableAssets(const PlayBlock& block, const std::function<bool(const std::string&)>& exists) {
    std::vector<std::string> out;
    for (const std::string& a : block.assets)
        if (exists && exists(a)) out.push_back(a);
    return out;
}

std::string chooseAsset(const PlayBlock& block, const std::vector<std::string>& available, uint32_t pick) {
    if (available.empty()) return {};
    return block.randomAsset ? available[pick % available.size()] : available.front();
}

float yawToFace(const glm::vec3& position, const glm::vec3& viewer) {
    const glm::vec2 d(viewer.x - position.x, viewer.z - position.z);
    if (glm::dot(d, d) < 1e-8f) return 0.0f;
    return glm::degrees(std::atan2(d.x, d.y));
}

LongAxis findLongAxis(const std::vector<glm::vec3>& points) {
    LongAxis out;
    if (points.empty()) return out;
    glm::vec3 mn(points.front()), mx(points.front());
    for (const glm::vec3& p : points) { mn = glm::min(mn, p); mx = glm::max(mx, p); }
    const glm::vec3 size = mx - mn;
    int a = 0;
    if (size.y > size[a]) a = 1;
    if (size.z > size[a]) a = 2;
    out.length = size[a];
    if (out.length <= 0.0f) {
        out.handle = mn;
        return out;
    }
    // Mean distance from the long axis over each end's fifth: the handle
    // is the thin end.
    const glm::vec3 center = (mn + mx) * 0.5f;
    double wide[2] = { 0.0, 0.0 };
    int count[2] = { 0, 0 };
    for (const glm::vec3& p : points) {
        const float u = (p[a] - mn[a]) / out.length;
        const int end = u < 0.2f ? 0 : (u > 0.8f ? 1 : -1);
        if (end < 0) continue;
        glm::vec3 off = p - center;
        off[a] = 0.0f;
        wide[end] += glm::length(off);
        ++count[end];
    }
    const double w0 = count[0] ? wide[0] / count[0] : 0.0, w1 = count[1] ? wide[1] / count[1] : 0.0;
    const bool thinAtMin = w0 <= w1;
    out.axis = glm::vec3(0.0f);
    out.axis[a] = thinAtMin ? 1.0f : -1.0f;
    out.handle = center;
    out.handle[a] = thinAtMin ? mn[a] : mx[a];
    return out;
}

glm::vec2 padPointerStep(const glm::vec2& stick, float dt, float screenHeight, const PadPointerSettings& settings) {
    const float length = glm::length(stick);
    const float tilt = std::min(length, 1.0f);
    if (tilt <= settings.deadzone || dt <= 0.0f) return glm::vec2(0.0f);
    // Rescale so movement starts from zero at the deadzone's edge.
    const float t = (tilt - settings.deadzone) / std::max(1.0f - settings.deadzone, 1e-4f);
    const float speed = settings.pixelsPerSecond * (std::max(screenHeight, 1.0f) / 720.0f) * std::pow(t, settings.curve);
    return stick / length * speed * dt;
}

int stepPaletteCell(const std::vector<glm::vec2>& centers, const glm::vec2& cursor, int direction) {
    if (centers.empty()) return -1;
    const int n = static_cast<int>(centers.size());
    // The cell the cursor is on (the nearest by x, if the cursor is on
    // the palette's row), else step in from the nearest end.
    int nearest = 0;
    for (int i = 1; i < n; ++i)
        if (std::abs(centers[static_cast<size_t>(i)].x - cursor.x) < std::abs(centers[static_cast<size_t>(nearest)].x - cursor.x)) nearest = i;
    const float cellGap = n > 1 ? std::abs(centers[1].x - centers[0].x) : 80.0f;
    const bool onRow = std::abs(centers[static_cast<size_t>(nearest)].y - cursor.y) < cellGap * 0.75f &&
                       std::abs(centers[static_cast<size_t>(nearest)].x - cursor.x) < cellGap * 0.75f;
    if (!onRow) return direction >= 0 ? 0 : n - 1;
    return ((nearest + (direction >= 0 ? 1 : -1)) % n + n) % n;
}

BatSwing::BatSwing(SwingSettings settings) : m_settings(settings) {}

glm::vec3 BatSwing::pivotFor(const glm::vec3& target, const glm::vec3& forward) const {
    glm::vec3 f(forward.x, 0.0f, forward.z);
    f = glm::dot(f, f) > 1e-8f ? glm::normalize(f) : glm::vec3(0.0f, 0.0f, -1.0f);
    glm::vec3 p = target - f * (m_settings.reach * 0.8f);
    p.y = target.y + m_settings.pivotHeight;
    return p;
}

bool BatSwing::start(const glm::vec3& pivot, const glm::vec3& forward) {
    if (active()) return false;
    glm::vec3 f(forward.x, 0.0f, forward.z);
    m_forward = glm::dot(f, f) > 1e-8f ? glm::normalize(f) : glm::vec3(0.0f, 0.0f, -1.0f);
    m_right = glm::normalize(glm::cross(m_forward, glm::vec3(0.0f, 1.0f, 0.0f)));
    m_pivot = pivot;
    m_phase = Phase::Swing;
    m_time = 0.0f;
    m_angle = m_prevAngle = m_settings.startDegrees;
    m_sweptFrom = m_sweptTo = 0.0f;
    m_swept = false;
    return true;
}

float BatSwing::angleAt(float t) const {
    const float d = std::max(m_settings.swingSeconds, 1e-4f);
    const float u = std::clamp(t / d, 0.0f, 1.0f);
    const float eased = u * u * (3.0f - 2.0f * u); // slow start and end, fastest through the middle
    return m_settings.startDegrees + (m_settings.endDegrees - m_settings.startDegrees) * eased;
}

float BatSwing::angularSpeedAt(float t) const {
    const float d = std::max(m_settings.swingSeconds, 1e-4f);
    const float u = std::clamp(t / d, 0.0f, 1.0f);
    return (m_settings.endDegrees - m_settings.startDegrees) * 6.0f * u * (1.0f - u) / d; // d/dt of angleAt
}

bool BatSwing::update(float dt) {
    if (m_phase == Phase::Idle) return false;
    dt = std::clamp(dt, 0.0f, std::max(m_settings.maxStepSeconds, 1e-4f));
    m_prevAngle = m_angle;
    m_swept = m_phase == Phase::Swing;
    if (m_phase == Phase::Swing) {
        m_sweptFrom = m_time;
        m_time += dt;
        m_sweptTo = std::min(m_time, m_settings.swingSeconds);
        m_angle = angleAt(m_sweptTo);
        if (m_time >= m_settings.swingSeconds) {
            // This update still hits (it covers the swing's last stretch);
            // the next one is the follow-through.
            m_phase = Phase::Hold;
            m_time -= m_settings.swingSeconds;
        }
        return true;
    }
    m_time += dt;
    if (m_time >= m_settings.holdSeconds) {
        m_phase = Phase::Idle;
        return false;
    }
    return true;
}

glm::vec3 BatSwing::direction(float degrees) const {
    const float r = glm::radians(degrees);
    return m_forward * std::cos(r) + m_right * std::sin(r);
}

BatSwing::Hit BatSwing::sweep(const glm::vec3& boxMin, const glm::vec3& boxMax) const {
    Hit out;
    // Only while swinging, and only over the angles covered since the
    // last update (a hold or a paused frame hits nothing).
    if (!m_swept) return out;
    // Sampled in time (at most 4 degrees apart), so the push is the
    // swing's own speed where it hit, however long the frame was.
    const float span = m_angle - m_prevAngle;
    const int steps = std::max(1, static_cast<int>(std::ceil(std::abs(span) / 4.0f)));
    const float length = m_settings.reach - m_settings.innerReach;
    for (int i = 0; i <= steps; ++i) {
        const float time = m_sweptFrom + (m_sweptTo - m_sweptFrom) * (static_cast<float>(i) / static_cast<float>(steps));
        const float deg = angleAt(time);
        const glm::vec3 dir = direction(deg);
        Ray ray{ m_pivot + dir * m_settings.innerReach, dir };
        const float t = rayAabb(ray, boxMin, boxMax);
        if (t < 0.0f || t > length) continue;
        out.hit = true;
        out.point = ray.at(t);
        const float r = glm::radians(deg);
        const glm::vec3 tangent = (span >= 0.0f ? 1.0f : -1.0f) * (-m_forward * std::sin(r) + m_right * std::cos(r));
        const float radius = m_settings.innerReach + t;
        const float speed = std::clamp(std::abs(glm::radians(angularSpeedAt(time))) * radius, m_settings.minPushSpeed,
                                       std::max(m_settings.minPushSpeed, m_settings.maxPushSpeed));
        out.degrees = deg;
        out.push = glm::normalize(tangent + m_forward * 0.5f) * speed + glm::vec3(0.0f, m_settings.lift, 0.0f);
        return out;
    }
    return out;
}

} // namespace kke
