#include "Track.h"

#include "kke/DataFile.h"
#include "kke/Log.h"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>
#include <system_error>

namespace racing {

const char* eventName(Event e) {
    switch (e) {
    case Event::Oval: return "Oval race";
    case Event::Drift: return "Drift";
    case Event::Drag: return "Drag race";
    case Event::Derby: return "Destruction derby";
    case Event::Rally: return "Rally";
    }
    return "?";
}

namespace {

// Keys a track file may have (anything else is reported: a typo shouldn't
// silently do nothing).
const char* const kKeys[] = { "name", "about", "event", "order", "mood", "shape", "straight", "radius", "length",
                              "points", "width", "bank", "apron", "laps", "wall", "size", "ground", "verge", "hills", "trees" };
const char* const kGrounds[] = { "tarmac", "concrete", "gravel", "dirt", "mud", "snow", "grass" };

template <typename T> void clampTo(T& v, T lo, T hi, const char* key, std::vector<std::string>& problems) {
    if (v < lo || v > hi) {
        problems.push_back(fmt::format("{} {} is outside {}..{}", key, v, lo, hi));
        v = std::clamp(v, lo, hi);
    }
}

// Centripetal Catmull-Rom through a closed loop of points, finely.
std::vector<glm::vec2> smoothLoop(const std::vector<glm::vec2>& pts, float spacing) {
    std::vector<glm::vec2> out;
    const size_t n = pts.size();
    for (size_t i = 0; i < n; ++i) {
        const glm::vec2 p0 = pts[(i + n - 1) % n], p1 = pts[i], p2 = pts[(i + 1) % n], p3 = pts[(i + 2) % n];
        auto knot = [](const glm::vec2& a, const glm::vec2& b) { return std::sqrt(std::max(glm::length(b - a), 1e-3f)); };
        const float t0 = 0.0f, t1 = t0 + knot(p0, p1), t2 = t1 + knot(p1, p2), t3 = t2 + knot(p2, p3);
        const int steps = std::max(4, static_cast<int>(glm::length(p2 - p1) / spacing));
        for (int k = 0; k < steps; ++k) {
            const float t = t1 + (t2 - t1) * static_cast<float>(k) / static_cast<float>(steps);
            const glm::vec2 a1 = (t1 - t) / (t1 - t0) * p0 + (t - t0) / (t1 - t0) * p1;
            const glm::vec2 a2 = (t2 - t) / (t2 - t1) * p1 + (t - t1) / (t2 - t1) * p2;
            const glm::vec2 a3 = (t3 - t) / (t3 - t2) * p2 + (t - t2) / (t3 - t2) * p3;
            const glm::vec2 b1 = (t2 - t) / (t2 - t0) * a1 + (t - t0) / (t2 - t0) * a2;
            const glm::vec2 b2 = (t3 - t) / (t3 - t1) * a2 + (t - t1) / (t3 - t1) * a3;
            out.push_back((t2 - t) / (t2 - t1) * b1 + (t - t1) / (t2 - t1) * b2);
        }
    }
    return out;
}

// Centripetal Catmull-Rom through open points (the ends held), with a
// height (z of the vec3) carried along.
std::vector<glm::vec3> smoothOpen(const std::vector<glm::vec3>& pts, float spacing) {
    std::vector<glm::vec3> out;
    const size_t n = pts.size();
    auto at = [&](long i) { return pts[static_cast<size_t>(std::clamp(i, 0L, static_cast<long>(n) - 1))]; };
    auto flat = [](const glm::vec3& v) { return glm::vec2(v.x, v.y); };
    for (size_t i = 0; i + 1 < n; ++i) {
        const long k = static_cast<long>(i);
        glm::vec3 p0 = at(k - 1), p1 = at(k), p2 = at(k + 1), p3 = at(k + 2);
        if (i == 0) p0 = p1 * 2.0f - p2;         // a straight start
        if (i + 2 >= n) p3 = p2 * 2.0f - p1;     // and end
        auto knot = [&](const glm::vec3& a, const glm::vec3& b) { return std::sqrt(std::max(glm::length(flat(b) - flat(a)), 1e-3f)); };
        const float t0 = 0.0f, t1 = t0 + knot(p0, p1), t2 = t1 + knot(p1, p2), t3 = t2 + knot(p2, p3);
        const int steps = std::max(4, static_cast<int>(glm::length(flat(p2) - flat(p1)) / spacing));
        for (int s = 0; s < steps; ++s) {
            const float t = t1 + (t2 - t1) * static_cast<float>(s) / static_cast<float>(steps);
            const glm::vec3 a1 = (t1 - t) / (t1 - t0) * p0 + (t - t0) / (t1 - t0) * p1;
            const glm::vec3 a2 = (t2 - t) / (t2 - t1) * p1 + (t - t1) / (t2 - t1) * p2;
            const glm::vec3 a3 = (t3 - t) / (t3 - t2) * p2 + (t - t2) / (t3 - t2) * p3;
            const glm::vec3 b1 = (t2 - t) / (t2 - t0) * a1 + (t - t0) / (t2 - t0) * a2;
            const glm::vec3 b2 = (t3 - t) / (t3 - t1) * a2 + (t - t1) / (t3 - t1) * a3;
            out.push_back((t2 - t) / (t2 - t1) * b1 + (t - t1) / (t2 - t1) * b2);
        }
    }
    out.push_back(pts.back());
    return out;
}

// Evenly spaced points along a polyline (closed: back to the start).
std::vector<glm::vec2> resample(const std::vector<glm::vec2>& line, bool closed, float step, float& length) {
    std::vector<float> acc(1, 0.0f);
    const size_t n = line.size(), segs = closed ? n : n - 1;
    for (size_t i = 0; i < segs; ++i) acc.push_back(acc.back() + glm::length(line[(i + 1) % n] - line[i]));
    const float total = acc.back();
    const int count = std::max(8, static_cast<int>(std::round(total / step)));
    const float actual = total / static_cast<float>(count);
    std::vector<glm::vec2> out;
    size_t seg = 0;
    for (int k = 0; k < count + (closed ? 0 : 1); ++k) {
        const float d = std::min(total, actual * static_cast<float>(k));
        while (seg + 1 < acc.size() - 1 && acc[seg + 1] < d) ++seg;
        const float span = std::max(acc[seg + 1] - acc[seg], 1e-6f);
        const float t = std::clamp((d - acc[seg]) / span, 0.0f, 1.0f);
        out.push_back(glm::mix(line[seg], line[(seg + 1) % n], t));
    }
    length = total;
    return out;
}

void quad(Track::Mesh& m, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec3& d, const glm::vec3& normal,
          const glm::vec3& color) {
    // a b c d around the quad; wound so `normal` is the front.
    const uint32_t base = static_cast<uint32_t>(m.positions.size());
    for (const glm::vec3& p : { a, b, c, d }) {
        m.positions.push_back(p);
        m.normals.push_back(normal);
        m.colors.push_back(color);
    }
    if (glm::dot(glm::cross(b - a, c - a), normal) >= 0.0f) m.indices.insert(m.indices.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
    else m.indices.insert(m.indices.end(), { base, base + 2, base + 1, base, base + 3, base + 2 });
}

constexpr float kRoadBase = 0.03f; // the road's lowest edge, just above the grass
const glm::vec3 kAsphalt(0.17f, 0.17f, 0.19f), kApron(0.27f, 0.27f, 0.28f), kWall(0.84f, 0.85f, 0.88f), kWallTop(0.2f, 0.35f, 0.75f);
const glm::vec3 kWhite(0.92f, 0.92f, 0.9f), kYellow(0.95f, 0.78f, 0.15f), kRed(0.8f, 0.12f, 0.1f), kBlack(0.05f, 0.05f, 0.06f);

// The road's colour by its ground.
glm::vec3 roadColor(const std::string& ground) {
    if (ground == "gravel") return { 0.5f, 0.46f, 0.39f };
    if (ground == "dirt") return { 0.42f, 0.33f, 0.24f };
    if (ground == "mud") return { 0.26f, 0.2f, 0.14f };
    if (ground == "snow") return { 0.86f, 0.88f, 0.92f };
    if (ground == "concrete") return { 0.36f, 0.36f, 0.35f };
    if (ground == "grass") return { 0.3f, 0.42f, 0.22f };
    return kAsphalt;
}

} // namespace

glm::vec3 groundColor(const std::string& ground) { return roadColor(ground); }

bool trackFromJson(const nlohmann::json& j, TrackDesc& t, std::vector<std::string>& problems) {
    if (!j.is_object()) return false;
    for (auto it = j.begin(); it != j.end(); ++it)
        if (std::find_if(std::begin(kKeys), std::end(kKeys), [&](const char* k) { return it.key() == k; }) == std::end(kKeys))
            problems.push_back("unknown key '" + it.key() + "'");
    auto str = [&](const char* k, std::string& v) {
        if (j.contains(k) && j[k].is_string()) v = j[k].get<std::string>();
    };
    auto num = [&](const char* k, float& v) {
        if (!j.contains(k)) return;
        if (j[k].is_number()) v = j[k].get<float>();
        else problems.push_back(fmt::format("{} should be a number", k));
    };
    str("name", t.name);
    str("about", t.about);
    str("mood", t.mood);
    str("shape", t.shape);
    std::string event = "oval";
    str("event", event);
    if (event == "oval") t.event = Event::Oval;
    else if (event == "drift") t.event = Event::Drift;
    else if (event == "drag") t.event = Event::Drag;
    else if (event == "derby") t.event = Event::Derby;
    else if (event == "rally") t.event = Event::Rally;
    else problems.push_back("event '" + event + "' is not oval, drift, drag, derby or rally");
    if (j.contains("order") && j["order"].is_number_integer()) t.order = j["order"].get<int>();
    if (j.contains("laps") && j["laps"].is_number_integer()) t.laps = j["laps"].get<int>();
    num("straight", t.straight);
    num("radius", t.radius);
    num("length", t.length);
    num("width", t.width);
    num("bank", t.bank);
    num("apron", t.apron);
    num("wall", t.wall);
    num("hills", t.hills);
    num("trees", t.trees);
    if (j.contains("points")) {
        if (!j["points"].is_array()) problems.push_back("points should be a list of [x, z]");
        else
            for (const auto& p : j["points"])
                if (p.is_array() && (p.size() == 2 || p.size() == 3) && p[0].is_number() && p[1].is_number() && (p.size() == 2 || p[2].is_number())) {
                    t.points.emplace_back(p[0].get<float>(), p[1].get<float>());
                    t.heights.push_back(p.size() == 3 ? p[2].get<float>() : 0.0f);
                } else {
                    problems.push_back("a point should be [x, z] or [x, z, y]");
                }
    }
    if (j.contains("size")) {
        const auto& sz = j["size"];
        if (sz.is_array() && sz.size() == 2 && sz[0].is_number() && sz[1].is_number()) t.size = { sz[0].get<float>(), sz[1].get<float>() };
        else problems.push_back("size should be [across, along]");
    }
    // Each event's usual ground unless the file says.
    t.ground = t.event == Event::Rally ? "gravel" : t.event == Event::Derby ? "dirt" : "tarmac";
    t.verge = t.event == Event::Derby ? t.ground : t.event == Event::Oval || t.event == Event::Rally ? "grass" : "concrete";
    str("ground", t.ground);
    str("verge", t.verge);
    for (std::string* g : { &t.ground, &t.verge })
        if (std::find_if(std::begin(kGrounds), std::end(kGrounds), [&](const char* k) { return *g == k; }) == std::end(kGrounds)) {
            problems.push_back("ground '" + *g + "' is not tarmac, concrete, gravel, dirt, mud, snow or grass");
            *g = "tarmac";
        }
    clampTo(t.width, 8.0f, 40.0f, "width", problems);
    clampTo(t.bank, 0.0f, 30.0f, "bank", problems);
    clampTo(t.apron, 0.0f, 20.0f, "apron", problems);
    clampTo(t.wall, 0.4f, 4.0f, "wall", problems);
    clampTo(t.straight, 20.0f, 1000.0f, "straight", problems);
    clampTo(t.radius, 20.0f, 400.0f, "radius", problems);
    clampTo(t.length, 60.0f, 2000.0f, "length", problems);
    clampTo(t.laps, 1, 99, "laps", problems);
    clampTo(t.hills, 0.0f, 40.0f, "hills", problems);
    clampTo(t.trees, 0.0f, 1.0f, "trees", problems);
    clampTo(t.size.x, 30.0f, 300.0f, "size (across)", problems);
    clampTo(t.size.y, 30.0f, 300.0f, "size (along)", problems);
    if (t.shape != "oval" && t.shape != "points" && t.shape != "strip" && t.shape != "arena" && t.shape != "stage") {
        problems.push_back("shape '" + t.shape + "' is not oval, points, strip, arena or stage");
        t.shape = "oval";
    }
    if ((t.shape == "points" && t.points.size() < 4) || (t.shape == "stage" && t.points.size() < 2)) {
        problems.push_back(fmt::format("a {} track needs at least {} points", t.shape, t.shape == "points" ? 4 : 2));
        t.shape = "oval";
    }
    if (t.event == Event::Drag) t.shape = "strip";
    if (t.event == Event::Derby) t.shape = "arena";
    if (t.event == Event::Rally && t.shape != "stage") {
        problems.push_back("a rally needs shape: stage");
        t.event = Event::Oval;
    }
    if (t.name.empty()) t.name = t.id;
    return true;
}

std::vector<TrackDesc> loadTracks(const std::filesystem::path& folder, std::vector<std::string>& problems) {
    std::vector<TrackDesc> out;
    std::error_code ec;
    if (!std::filesystem::is_directory(folder, ec)) {
        problems.push_back(folder.string() + ": no tracks folder");
        return out;
    }
    // One track per name: speedway.yaml and speedway.json are the same
    // track (the newest wins, kke::datafile says so).
    std::vector<std::string> stems;
    for (const auto& entry : std::filesystem::directory_iterator(folder, ec)) {
        if (!entry.is_regular_file() || !kke::datafile::formatOf(entry.path())) continue;
        const std::string stem = entry.path().stem().string();
        if (std::find(stems.begin(), stems.end(), stem) == stems.end()) stems.push_back(stem);
    }
    for (const std::string& stem : stems) {
        kke::datafile::Loaded loaded;
        std::string error;
        if (!kke::datafile::load(folder, stem, loaded, &error)) {
            problems.push_back(stem + ": " + error);
            continue;
        }
        TrackDesc t;
        t.id = stem;
        std::vector<std::string> own;
        const bool ok = trackFromJson(loaded.data, t, own);
        for (const std::string& p : own) problems.push_back(loaded.file.filename().string() + ": " + p);
        if (ok) out.push_back(std::move(t));
    }
    std::sort(out.begin(), out.end(), [](const TrackDesc& a, const TrackDesc& b) { return a.order != b.order ? a.order < b.order : a.id < b.id; });
    return out;
}

TrackDesc defaultTrack() {
    TrackDesc t;
    t.id = "speedway";
    t.name = "Kompas Speedway";
    t.event = Event::Oval;
    t.width = 18.0f;
    t.bank = 14.0f;
    t.apron = 10.0f;
    return t;
}

Track::Track(const TrackDesc& desc) : m_desc(desc) {
    m_closed = m_desc.shape != "strip" && m_desc.shape != "stage";
    if (arena()) m_desc.width = 4.0f; // the line the wall follows; the floor is the ground
    sample();
    buildGeometry();
    if (stage()) buildTerrain();
}

void Track::sample() {
    std::vector<glm::vec2> line;
    std::vector<glm::vec3> hilly; // stage: x, z and the road's height
    if (m_desc.shape == "oval") {
        // Two straights along Z, turns around (0, +-L/2), run anticlockwise
        // seen from above: every turn a left turn, like NASCAR. s = 0 is
        // the middle of the front straight (x = -R).
        const float L = m_desc.straight, R = m_desc.radius;
        auto push = [&](const glm::vec2& p) { line.push_back(p); };
        for (float z = 0.0f; z < L * 0.5f; z += 0.5f) push({ -R, z });
        for (int k = 0; k < 400; ++k) {
            const float th = glm::pi<float>() * (1.0f - static_cast<float>(k) / 400.0f);
            push({ R * std::cos(th), L * 0.5f + R * std::sin(th) });
        }
        for (float z = L * 0.5f; z > -L * 0.5f; z -= 0.5f) push({ R, z });
        for (int k = 0; k < 400; ++k) {
            const float th = -glm::pi<float>() * static_cast<float>(k) / 400.0f;
            push({ R * std::cos(th), -L * 0.5f + R * std::sin(th) });
        }
        for (float z = -L * 0.5f; z < 0.0f; z += 0.5f) push({ -R, z });
    } else if (m_desc.shape == "arena") {
        // The wall's line: an ellipse just inside the size, the same way
        // round as the oval (the wall on the right).
        const float a = m_desc.size.x * 0.5f - halfWidth() - 0.5f, b = m_desc.size.y * 0.5f - halfWidth() - 0.5f;
        for (int k = 0; k < 720; ++k) {
            const float th = glm::pi<float>() * (1.0f - 2.0f * static_cast<float>(k) / 720.0f);
            line.push_back({ a * std::cos(th), b * std::sin(th) });
        }
    } else if (m_desc.shape == "points") {
        line = smoothLoop(m_desc.points, 0.5f);
    } else if (m_desc.shape == "stage") {
        std::vector<glm::vec3> pts;
        for (size_t i = 0; i < m_desc.points.size(); ++i)
            pts.emplace_back(m_desc.points[i].x, m_desc.points[i].y, i < m_desc.heights.size() ? m_desc.heights[i] : 0.0f);
        hilly = smoothOpen(pts, 0.5f);
        for (const glm::vec3& p : hilly) line.emplace_back(p.x, p.y);
    } else {
        // The strip: 30 m behind the line to stage, the run, then room to stop.
        line = { { 0.0f, -30.0f }, { 0.0f, m_desc.length + 260.0f } };
    }
    const std::vector<glm::vec2> pts = resample(line, m_closed, m_step, m_length);
    const size_t n = pts.size();
    m_step = m_length / static_cast<float>(m_closed ? n : n - 1);
    m_samples.assign(n, Sample{});
    for (size_t i = 0; i < n; ++i) {
        const glm::vec2 prev = pts[m_closed ? (i + n - 1) % n : (i == 0 ? 0 : i - 1)];
        const glm::vec2 next = pts[m_closed ? (i + 1) % n : std::min(i + 1, n - 1)];
        glm::vec2 d = next - prev;
        d = glm::length(d) > 1e-5f ? glm::normalize(d) : glm::vec2(0.0f, 1.0f);
        Sample& s = m_samples[i];
        s.p = glm::vec3(pts[i].x, 0.0f, pts[i].y);
        s.forward = glm::vec3(d.x, 0.0f, d.y);
        s.s = m_step * static_cast<float>(i);
    }
    // Curvature: how fast forward turns toward left, per metre (smoothed:
    // the resampled circle isn't perfectly round).
    std::vector<float> k(n, 0.0f);
    for (size_t i = 0; i < n; ++i) {
        if (!m_closed && (i == 0 || i + 1 == n)) continue;
        const glm::vec3 a = m_samples[(i + n - 1) % n].forward, b = m_samples[(i + 1) % n].forward;
        const glm::vec3 left(a.z, 0.0f, -a.x);
        k[i] = glm::dot(b - a, left) / (2.0f * m_step);
    }
    auto smooth = [&](const std::vector<float>& in, int radius) {
        std::vector<float> out(n, 0.0f);
        for (size_t i = 0; i < n; ++i) {
            float sum = 0.0f;
            int count = 0;
            for (int o = -radius; o <= radius; ++o) {
                const long j = static_cast<long>(i) + o;
                if (!m_closed && (j < 0 || j >= static_cast<long>(n))) continue;
                sum += in[static_cast<size_t>((j + static_cast<long>(n)) % static_cast<long>(n))];
                ++count;
            }
            out[i] = sum / static_cast<float>(std::max(count, 1));
        }
        return out;
    };
    k = smooth(k, 3);
    float kMax = 0.0f;
    for (float c : k) kMax = std::max(kMax, std::fabs(c));
    // Banking: the full angle in the tightest turns, less in gentler ones,
    // eased in and out over ~40 m.
    std::vector<float> bank(n, 0.0f);
    const float full = glm::radians(m_desc.bank);
    for (size_t i = 0; i < n; ++i) bank[i] = kMax > 1e-5f ? full * std::clamp(k[i] / (kMax * 0.8f), -1.0f, 1.0f) : 0.0f;
    bank = smooth(bank, static_cast<int>(20.0f / m_step));
    for (size_t i = 0; i < n; ++i) {
        Sample& s = m_samples[i];
        s.curvature = k[i];
        s.bank = bank[i];
        const glm::vec3 leftFlat(s.forward.z, 0.0f, -s.forward.x);
        s.left = leftFlat * std::cos(s.bank) - glm::vec3(0.0f, 1.0f, 0.0f) * std::sin(s.bank);
        s.up = glm::vec3(0.0f, 1.0f, 0.0f) * std::cos(s.bank) + leftFlat * std::sin(s.bank);
        s.p.y = kRoadBase + heightOffset(s);
    }
    // A stage's hills: each sample the height of the nearest point of the
    // smoothed line (they were made from it, so it's close).
    if (!hilly.empty()) {
        size_t j = 0;
        for (Sample& s : m_samples) {
            float best = 1e30f;
            for (size_t k = j; k < hilly.size() && k < j + 400; ++k) {
                const float d = glm::length(glm::vec2(hilly[k].x - s.p.x, hilly[k].y - s.p.z));
                if (d < best) {
                    best = d;
                    j = k;
                }
            }
            s.p.y += hilly[j].z;
        }
        // Up is square to the slope (forward stays level: s and u are
        // measured flat, as everywhere else).
        const size_t count = m_samples.size();
        for (size_t i = 0; i < count; ++i) {
            Sample& s = m_samples[i];
            const glm::vec3 d = m_samples[std::min(i + 1, count - 1)].p - m_samples[i ? i - 1 : 0].p;
            const glm::vec3 slope = glm::length(d) > 1e-4f ? glm::normalize(d) : s.forward;
            s.up = glm::normalize(glm::cross(slope, s.left));
        }
    }
    if (hasPits()) {
        // The pit boxes: on the apron of the front straight, before the line.
        m_pitStart = m_length - m_desc.straight * 0.45f;
        m_pitEnd = m_length - 12.0f;
    }
}

float Track::heightOffset(const Sample& s) const { return halfWidth() * std::fabs(std::sin(s.bank)); }

bool Track::inPits(const Where& w) const {
    if (!hasPits()) return false;
    return w.s >= m_pitStart && w.s <= m_pitEnd && w.u > halfWidth() + 0.5f;
}

float Track::wrap(float s) const {
    if (!m_closed) return std::clamp(s, 0.0f, m_length);
    s = std::fmod(s, m_length);
    return s < 0.0f ? s + m_length : s;
}

float Track::delta(float a, float b) const {
    float d = b - a;
    if (m_closed) {
        d = std::fmod(d, m_length);
        if (d > m_length * 0.5f) d -= m_length;
        if (d < -m_length * 0.5f) d += m_length;
    }
    return d;
}

Track::Sample Track::at(float s) const {
    s = wrap(s);
    const size_t n = m_samples.size();
    const float f = s / m_step;
    size_t i = std::min(static_cast<size_t>(f), n - 1);
    const size_t j = m_closed ? (i + 1) % n : std::min(i + 1, n - 1);
    const float t = std::clamp(f - static_cast<float>(i), 0.0f, 1.0f);
    const Sample& a = m_samples[i];
    const Sample& b = m_samples[j];
    Sample out = a;
    out.p = glm::mix(a.p, b.p, t);
    out.forward = glm::normalize(glm::mix(a.forward, b.forward, t));
    out.left = glm::normalize(glm::mix(a.left, b.left, t));
    out.up = glm::normalize(glm::mix(a.up, b.up, t));
    out.curvature = glm::mix(a.curvature, b.curvature, t);
    out.bank = glm::mix(a.bank, b.bank, t);
    out.s = s;
    return out;
}

glm::vec3 Track::point(float s, float u) const {
    const Sample f = at(s);
    const float hw = halfWidth();
    if (u <= hw) return f.p + f.left * u;
    // The apron: flat, from the road's inner edge.
    const glm::vec3 edge = f.p + f.left * hw;
    const glm::vec3 leftFlat(f.forward.z, 0.0f, -f.forward.x);
    return edge + leftFlat * (u - hw);
}

Track::Where Track::locate(const glm::vec3& p, int hint) const {
    const int n = static_cast<int>(m_samples.size());
    int best = 0;
    float bestD = 1e30f;
    auto test = [&](int i) {
        const glm::vec3 d = p - m_samples[static_cast<size_t>(i)].p;
        const float dd = d.x * d.x + d.z * d.z; // horizontal: a banked turn is still that turn
        if (dd < bestD) {
            bestD = dd;
            best = i;
        }
    };
    if (hint < 0 || hint >= n) {
        for (int i = 0; i < n; ++i) test(i);
    } else {
        for (int o = -40; o <= 40; ++o) {
            int i = hint + o;
            if (m_closed) i = (i % n + n) % n;
            else if (i < 0 || i >= n) continue;
            test(i);
        }
    }
    const Sample& s = m_samples[static_cast<size_t>(best)];
    Where w;
    w.sample = best;
    glm::vec3 d = p - s.p;
    const float along = std::clamp(glm::dot(glm::vec3(d.x, 0.0f, d.z), s.forward), -m_step, m_step);
    w.s = wrap(s.s + along);
    const Sample f = at(w.s);
    d = p - f.p;
    const glm::vec3 leftFlat(f.forward.z, 0.0f, -f.forward.x);
    const float flat = glm::dot(glm::vec3(d.x, 0.0f, d.z), leftFlat);
    const float c = std::max(std::cos(f.bank), 0.3f);
    w.u = flat / c <= halfWidth() ? flat / c : halfWidth() + (flat - halfWidth() * c);
    w.height = p.y - point(w.s, w.u).y;
    return w;
}

float Track::maxCurvature(float s, float ahead) const {
    float k = 0.0f;
    for (float d = 0.0f; d <= ahead; d += m_step) {
        if (!m_closed && s + d > m_length) break;
        k = std::max(k, std::fabs(at(s + d).curvature));
    }
    return k;
}

bool Track::insideArena(const glm::vec3& p, float margin) const {
    const float a = m_desc.size.x * 0.5f - margin, b = m_desc.size.y * 0.5f - margin;
    return a > 0.0f && b > 0.0f && (p.x * p.x) / (a * a) + (p.z * p.z) / (b * b) < 1.0f;
}

glm::vec3 Track::gridPosition(int slot, int cars) const {
    const float hw = halfWidth();
    if (arena()) {
        // Round the edge, evenly, a car's length off the wall.
        const float th = glm::two_pi<float>() * static_cast<float>(slot) / static_cast<float>(std::max(cars, 1)) + 0.3f;
        return glm::vec3(std::cos(th) * (m_desc.size.x * 0.5f - 6.0f), kRoadBase, std::sin(th) * (m_desc.size.y * 0.5f - 6.0f));
    }
    if (stage()) return point(startS() - 2.8f - static_cast<float>(slot) * 8.0f, 0.0f); // single file: one car at a time
    if (!m_closed) {
        // Side by side at the line, one lane each.
        const int lanes = std::max(1, std::min(cars, 8));
        const float lane = m_desc.width / static_cast<float>(lanes);
        return point(30.0f - 2.8f, hw - lane * (static_cast<float>(slot % lanes) + 0.5f));
    }
    // Two by two behind the line, pole on the inside.
    const int row = slot / 2, col = slot % 2;
    return point(-8.0f - static_cast<float>(row) * 9.0f, (col == 0 ? 1.0f : -1.0f) * hw * 0.38f);
}

glm::vec3 Track::gridForward(int slot, int cars) const {
    const glm::vec3 p = gridPosition(slot, cars);
    if (arena()) return glm::length(glm::vec2(p.x, p.z)) > 1e-3f ? -glm::normalize(glm::vec3(p.x, 0.0f, p.z)) : glm::vec3(0.0f, 0.0f, 1.0f);
    return at(locate(p).s).forward;
}

void Track::buildGeometry() {
    const size_t n = m_samples.size();
    const size_t segs = m_closed ? n : n - 1;
    const float hw = halfWidth(), apron = m_desc.apron;
    const glm::vec3 Y(0.0f, 1.0f, 0.0f);
    // The road and the apron, sample to sample (an arena's floor is the ground).
    const glm::vec3 road = roadColor(m_desc.ground);
    for (size_t i = 0; i < segs && !arena(); ++i) {
        const Sample& a = m_samples[i];
        const Sample& b = m_samples[(i + 1) % n];
        const glm::vec3 up = glm::normalize(a.up + b.up);
        quad(m_surface, a.p - a.left * hw, b.p - b.left * hw, b.p + b.left * hw, a.p + a.left * hw, up, road);
        if (apron > 0.0f) {
            const float sa = a.s, sb = sa + m_step;
            quad(m_surface, point(sa, hw), point(sb, hw), point(sb, hw + apron), point(sa, hw + apron), Y, kApron);
        }
    }
    // Walls: the outside (right) at the road's edge, rising from the
    // ground past the banked edge; the inside past the apron. Drawn as
    // the face toward the road, a top and a back; collided as boxes.
    auto wall = [&](float u, float thickness, bool facesLeft, const glm::vec3& color) {
        for (size_t i = 0; i < segs; ++i) {
            const float sa = m_samples[i].s, sb = sa + m_step;
            const glm::vec3 pa = point(sa, u), pb = point(sb, u);
            const float topA = pa.y + m_desc.wall, topB = pb.y + m_desc.wall;
            const glm::vec3 fwd = glm::normalize(pb - pa);
            const glm::vec3 side = glm::normalize(glm::vec3(fwd.z, 0.0f, -fwd.x)) * (facesLeft ? 1.0f : -1.0f); // toward the road
            const glm::vec3 inA(pa.x, 0.0f, pa.z), inB(pb.x, 0.0f, pb.z);
            const glm::vec3 backA = inA - side * thickness, backB = inB - side * thickness;
            quad(m_walls, inA, inB, glm::vec3(inB.x, topB, inB.z), glm::vec3(inA.x, topA, inA.z), side, color);
            quad(m_walls, glm::vec3(inA.x, topA, inA.z), glm::vec3(inB.x, topB, inB.z), glm::vec3(backB.x, topB, backB.z),
                 glm::vec3(backA.x, topA, backA.z), Y, kWallTop);
            quad(m_walls, backA, backB, glm::vec3(backB.x, topB, backB.z), glm::vec3(backA.x, topA, backA.z), -side, color * 0.8f);
            WallBox box;
            const float top = std::max(topA, topB);
            box.center = (inA + inB) * 0.5f - side * (thickness * 0.5f) + glm::vec3(0.0f, top * 0.5f, 0.0f);
            box.half = glm::vec3(thickness * 0.5f, top * 0.5f, glm::length(glm::vec2(pb.x - pa.x, pb.z - pa.z)) * 0.5f + 0.08f);
            box.forward = glm::normalize(glm::vec3(fwd.x, 0.0f, fwd.z));
            m_wallBoxes.push_back(box);
        }
    };
    // A stage has none (off the road is off the road); an arena only the
    // outside one, all round.
    if (!stage()) wall(-hw - 0.4f, arena() ? 0.8f : 0.6f, true, kWall);
    if (!stage() && !arena()) wall(hw + apron + 0.4f, 0.6f, false, apron > 0.0f ? kWall * glm::vec3(0.9f, 0.95f, 1.0f) : kWall);
    if (arena()) return; // no lines on the dirt
    if (stage()) {
        // The start and the finish: a chequered line across the gravel.
        auto flagLine = [&](float at) {
            const int squares = static_cast<int>(m_desc.width);
            for (int row = 0; row < 2; ++row)
                for (int c = 0; c < squares; ++c) {
                    const float u0 = -hw + m_desc.width * static_cast<float>(c) / static_cast<float>(squares);
                    const float u1 = -hw + m_desc.width * static_cast<float>(c + 1) / static_cast<float>(squares);
                    const float s0 = at + static_cast<float>(row) - 1.0f;
                    const Sample fa = this->at(s0), fb = this->at(s0 + 1.0f);
                    const glm::vec3 lift = (fa.up + fb.up) * 0.006f;
                    quad(m_markings, point(s0, u0) + lift, point(s0 + 1.0f, u0) + lift, point(s0 + 1.0f, u1) + lift, point(s0, u1) + lift, fa.up,
                         (c + row) % 2 ? kWhite : kBlack);
                }
        };
        flagLine(startS());
        flagLine(finishS());
        return;
    }
    if (!m_closed) {
        // The end of the strip: a catch wall across it.
        const Sample& e = m_samples.back();
        WallBox box;
        box.center = e.p + e.forward * 0.5f + glm::vec3(0.0f, 1.0f, 0.0f);
        box.half = glm::vec3(0.5f, 1.0f, hw + 1.0f);
        box.forward = glm::vec3(e.forward.z, 0.0f, -e.forward.x);
        m_wallBoxes.push_back(box);
        quad(m_walls, e.p - e.left * (hw + 1.0f), e.p + e.left * (hw + 1.0f), e.p + e.left * (hw + 1.0f) + Y * 2.0f, e.p - e.left * (hw + 1.0f) + Y * 2.0f,
             -e.forward, kWall);
    }

    // Markings, a centimetre above the road along its own up.
    auto stripe = [&](float s0, float s1, float u0, float u1, const glm::vec3& color) {
        const float step = std::max(0.5f, std::min(m_step, s1 - s0));
        for (float s = s0; s < s1 - 1e-3f; s += step) {
            const float e = std::min(s + step, s1);
            const Sample fa = at(s), fb = at(e);
            const glm::vec3 lift = (fa.up + fb.up) * 0.006f;
            quad(m_markings, point(s, u0) + lift, point(e, u0) + lift, point(e, u1) + lift, point(s, u1) + lift, glm::normalize(fa.up + fb.up), color);
        }
    };
    const float total = m_closed ? m_length : m_length;
    stripe(0.0f, total, -hw + 0.3f, -hw + 0.55f, kWhite);   // right edge line
    stripe(0.0f, total, hw - 0.55f, hw - 0.3f, apron > 0.0f ? kYellow : kWhite); // left edge line (the apron's border on the oval)
    // Start / finish: two rows of chequers across the road.
    const float line = m_closed ? 0.0f : 30.0f;
    const int squares = static_cast<int>(m_desc.width);
    for (int row = 0; row < 2; ++row)
        for (int c = 0; c < squares; ++c) {
            const float u0 = -hw + m_desc.width * static_cast<float>(c) / static_cast<float>(squares);
            const float u1 = -hw + m_desc.width * static_cast<float>(c + 1) / static_cast<float>(squares);
            stripe(line + static_cast<float>(row) * 1.0f - 1.0f, line + static_cast<float>(row) * 1.0f, u0, u1, (c + row) % 2 ? kWhite : kBlack);
        }
    if (!m_closed) {
        // Lane lines and the finish line of the strip.
        const int lanes = 8;
        for (int l = 1; l < lanes; ++l) {
            const float u = hw - m_desc.width * static_cast<float>(l) / static_cast<float>(lanes);
            stripe(0.0f, finishS() + 40.0f, u - 0.08f, u + 0.08f, kWhite);
        }
        stripe(finishS() - 0.6f, finishS() + 0.6f, -hw, hw, kWhite);
    }
    if (hasPits()) {
        // The pit lane: a line where it starts and ends, boxes along it.
        stripe(m_pitStart, m_pitStart + 0.6f, hw, hw + apron, kYellow);
        stripe(m_pitEnd - 0.6f, m_pitEnd, hw, hw + apron, kYellow);
        for (float s = m_pitStart + 10.0f; s < m_pitEnd - 4.0f; s += 12.0f) stripe(s, s + 0.25f, hw + 2.0f, hw + apron - 1.0f, kWhite);
    }
    // Kerbs on the inside of tight corners, red and white.
    for (size_t i = 0; i < segs; ++i) {
        const Sample& a = m_samples[i];
        if (std::fabs(a.curvature) < 1.0f / 70.0f || apron > 0.0f) continue;
        const float side = a.curvature > 0.0f ? 1.0f : -1.0f;
        const bool red = (i / 1) % 2 == 0;
        stripe(a.s, a.s + m_step, side > 0 ? hw - 1.2f : -hw, side > 0 ? hw : -hw + 1.2f, red ? kRed : kWhite);
    }
}

namespace {

// Rolling land: a few waves at odd angles, -1..1, no repeats you'd notice.
float hillNoise(float x, float z) {
    const float a = std::sin(x * 0.019f + 1.3f) * std::cos(z * 0.016f - 0.4f);
    const float b = std::sin(x * 0.043f - z * 0.037f + 2.1f);
    const float c = std::cos(x * 0.071f + z * 0.083f + 0.7f);
    return 0.55f * a + 0.3f * b + 0.15f * c;
}

} // namespace

// The land round a stage: a grid of 4 m squares over everything the road
// covers and a wide margin. Near the road it's the road's own height (a
// few cm under, so the gravel shows and a wheel over the edge barely
// drops); from a few metres out it rises and falls with the hills, the
// full `hills` from ~40 m away. Trees and rocks stand on it (Race.cpp).
void Track::buildTerrain() {
    glm::vec2 mn(1e9f), mx(-1e9f);
    for (const Sample& s : m_samples) {
        mn = glm::min(mn, glm::vec2(s.p.x, s.p.z));
        mx = glm::max(mx, glm::vec2(s.p.x, s.p.z));
    }
    const float margin = 140.0f;
    m_cell = 4.0f;
    m_gridMin = mn - glm::vec2(margin);
    m_gx = static_cast<int>(std::ceil((mx.x - mn.x + 2.0f * margin) / m_cell)) + 1;
    m_gz = static_cast<int>(std::ceil((mx.y - mn.y + 2.0f * margin) / m_cell)) + 1;
    m_heights.assign(static_cast<size_t>(m_gx) * static_cast<size_t>(m_gz), 0.0f);
    const float hw = halfWidth(), flatTo = hw + 3.0f, fullAt = hw + 40.0f;
    std::vector<float> nearness(m_heights.size(), 1e9f); // m to the road's middle
    const glm::vec3 verge = groundColor(m_desc.verge);
    for (int z = 0; z < m_gz; ++z)
        for (int x = 0; x < m_gx; ++x) {
            const glm::vec2 p = m_gridMin + glm::vec2(static_cast<float>(x), static_cast<float>(z)) * m_cell;
            // The nearest sample: every one (a stage is a few hundred).
            float best = 1e30f;
            size_t k = 0;
            for (size_t i = 0; i < m_samples.size(); ++i) {
                const float dx = m_samples[i].p.x - p.x, dz = m_samples[i].p.z - p.y;
                const float d = dx * dx + dz * dz;
                if (d < best) {
                    best = d;
                    k = i;
                }
            }
            const float d = std::sqrt(best);
            const float t = std::clamp((d - flatTo) / (fullAt - flatTo), 0.0f, 1.0f);
            const float rise = t * t * (3.0f - 2.0f * t);
            const size_t at = static_cast<size_t>(z) * static_cast<size_t>(m_gx) + static_cast<size_t>(x);
            m_heights[at] = m_samples[k].p.y - 0.06f + m_desc.hills * rise * hillNoise(p.x, p.y);
            nearness[at] = d;
        }
    // Triangles, each square split the same way; normals from the heights.
    auto h = [&](int x, int z) { return m_heights[static_cast<size_t>(std::clamp(z, 0, m_gz - 1)) * static_cast<size_t>(m_gx) + static_cast<size_t>(std::clamp(x, 0, m_gx - 1))]; };
    Mesh& m = m_terrain;
    m.positions.reserve(m_heights.size());
    for (int z = 0; z < m_gz; ++z)
        for (int x = 0; x < m_gx; ++x) {
            const glm::vec2 p = m_gridMin + glm::vec2(static_cast<float>(x), static_cast<float>(z)) * m_cell;
            m.positions.emplace_back(p.x, h(x, z), p.y);
            m.normals.push_back(glm::normalize(glm::vec3(h(x - 1, z) - h(x + 1, z), 2.0f * m_cell, h(x, z - 1) - h(x, z + 1))));
            // A little light and shade in the grass, bare earth by the road.
            const float shade = 0.9f + 0.1f * hillNoise(p.x * 3.1f, p.y * 2.7f);
            const float bare = std::clamp(1.0f - (nearness[static_cast<size_t>(z) * static_cast<size_t>(m_gx) + static_cast<size_t>(x)] - hw) / 4.0f, 0.0f, 1.0f);
            m.colors.push_back(glm::mix(verge * shade, groundColor(m_desc.ground) * 0.85f, bare * 0.6f));
        }
    for (int z = 0; z + 1 < m_gz; ++z)
        for (int x = 0; x + 1 < m_gx; ++x) {
            const uint32_t a = static_cast<uint32_t>(z * m_gx + x), b = a + 1, c = a + static_cast<uint32_t>(m_gx), d = c + 1;
            m.indices.insert(m.indices.end(), { a, c, b, b, c, d });
        }
}

float Track::groundHeight(float x, float z) const {
    if (m_heights.empty()) return 0.0f;
    const float fx = (x - m_gridMin.x) / m_cell, fz = (z - m_gridMin.y) / m_cell;
    if (fx < 0.0f || fz < 0.0f || fx >= static_cast<float>(m_gx - 1) || fz >= static_cast<float>(m_gz - 1)) return 0.0f;
    const int ix = static_cast<int>(fx), iz = static_cast<int>(fz);
    const float tx = fx - static_cast<float>(ix), tz = fz - static_cast<float>(iz);
    auto h = [&](int a, int b) { return m_heights[static_cast<size_t>(b) * static_cast<size_t>(m_gx) + static_cast<size_t>(a)]; };
    // The same triangle the mesh has (a, c, b | b, c, d).
    if (tx + tz <= 1.0f) return h(ix, iz) + (h(ix + 1, iz) - h(ix, iz)) * tx + (h(ix, iz + 1) - h(ix, iz)) * tz;
    return h(ix + 1, iz + 1) + (h(ix, iz + 1) - h(ix + 1, iz + 1)) * (1.0f - tx) + (h(ix + 1, iz) - h(ix + 1, iz + 1)) * (1.0f - tz);
}

} // namespace racing
