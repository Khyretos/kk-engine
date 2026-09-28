#pragma once

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace racing {

// What kind of race a track is for.
enum class Event : uint8_t {
    Oval,  // laps of a banked speedway, a big field, contact and a pit lane
    Drift, // a twisty circuit: points for sliding, the most points wins
    Drag,  // a straight quarter mile, side by side, you change gear yourself
    Derby, // a walled arena: wreck the others, the last car running wins
    Rally, // a gravel stage point to point, one car after another, against the clock; no walls
};
const char* eventName(Event e);
// What a ground (a track's `ground` or `verge`) looks like from above.
glm::vec3 groundColor(const std::string& ground);

// A track (games/racing/tracks/*.yaml, JSON works too). Copy one, change
// a few numbers, and it's a new track in the start menu. The keys:
//
//   name: Kompas Speedway    what the menu calls it
//   about: ...               one line under the name
//   event: oval              oval, drift, drag, derby or rally
//   order: 1                 where it sits in the menu
//   mood: golden_hour        the sky and light (assets/moods/)
//   shape: oval              oval: two straights and two turns;
//                            points: a closed loop through `points`;
//                            strip: a straight `length` m long;
//                            arena: an oval pen `size` m, walled all round;
//                            stage: a road from the first point to the last
//   straight: 260            oval: length of each straight, m
//   radius: 75               oval: radius of the turns, m (at the middle of the road)
//   length: 402              strip: m from the start line to the finish
//   points: [[0, 0], ...]    points, stage: x, z (m), a smooth curve goes through them;
//                            [x, z, y]: y is the road's height there (hills, crests)
//   size: [70, 50]           arena: across and along, m
//   ground: gravel           the road's surface: tarmac, concrete, gravel, dirt, mud, snow
//   verge: grass             off the road: the same list, or grass
//   hills: 6                 stage: m the land rises and falls away from the road
//   trees: 0.6               stage: how thick the forest is beside it (0..1)
//   width: 18                m of asphalt
//   bank: 14                 degrees the turns lean in (0 = flat)
//   apron: 10                oval: m of flat pit lane inside the track
//   laps: 5                  the start menu's default
//   wall: 1.2                m of wall above the road's edge
struct TrackDesc {
    std::string id, name, about, mood = "golden_hour";
    Event event = Event::Oval;
    int order = 100;
    std::string shape = "oval";
    float straight = 260.0f, radius = 75.0f, length = 402.0f;
    std::vector<glm::vec2> points;
    std::vector<float> heights;       // per point (stage), 0 when not given
    glm::vec2 size{70.0f, 50.0f};     // arena
    std::string ground = "tarmac", verge = "grass";
    float hills = 6.0f, trees = 0.6f; // stage
    float width = 18.0f, bank = 0.0f, apron = 0.0f, wall = 1.2f;
    int laps = 5;
};

bool trackFromJson(const nlohmann::json& j, TrackDesc& out, std::vector<std::string>& problems);
std::vector<TrackDesc> loadTracks(const std::filesystem::path& folder, std::vector<std::string>& problems);
// Built in, for when the tracks folder is missing: the speedway.
TrackDesc defaultTrack();

// The track as the game uses it: a centre line sampled every ~2 m, each
// sample with its own frame (forward, left across the banked road, up)
// and curvature; where a car is on it; and the road, apron and walls as
// triangles (for drawing and for Jolt).
//
// Lateral positions `u` are metres from the centre line, positive to the
// left (+X of a car facing forward). s is metres along the centre line
// from the start line (0 .. length(), wrapping on a closed track).
class Track {
public:
    struct Sample {
        glm::vec3 p{0.0f};           // centre of the road surface
        glm::vec3 forward{0.0f, 0.0f, 1.0f};
        glm::vec3 left{1.0f, 0.0f, 0.0f}; // across the road, along its (banked) surface
        glm::vec3 up{0.0f, 1.0f, 0.0f};
        float s = 0.0f;
        float curvature = 0.0f;      // 1/m, + = turning left
        float bank = 0.0f;           // radians, + = the right (outside of a left turn) raised
    };
    struct Where {
        int sample = 0;
        float s = 0.0f;              // along the centre line
        float u = 0.0f;              // across it (+ left)
        float height = 0.0f;         // above the road surface there
    };

    explicit Track(const TrackDesc& desc);
    const TrackDesc& desc() const { return m_desc; }
    bool closed() const { return m_closed; }
    float length() const { return m_length; }
    float halfWidth() const { return m_desc.width * 0.5f; }
    // Lateral limits a car's centre can use: the road (and the apron).
    float minU() const { return -halfWidth(); }
    float maxU() const { return halfWidth() + m_desc.apron; }
    const std::vector<Sample>& samples() const { return m_samples; }

    // Nearest centre-line sample to p, searching near `hint` (-1 = everywhere).
    Where locate(const glm::vec3& p, int hint = -1) const;
    // Interpolated frame at s (wraps on a closed track, clamps on a strip).
    Sample at(float s) const;
    // A point on the surface: along s, across u (flat on the apron).
    glm::vec3 point(float s, float u) const;
    // Signed distance a to b along the track (closed: the short way round).
    float delta(float a, float b) const;
    float wrap(float s) const;
    // Largest |curvature| in [s, s + ahead].
    float maxCurvature(float s, float ahead) const;

    // Pit lane (oval, on the apron): s range and the middle of its width.
    bool hasPits() const { return m_desc.apron > 3.0f && m_closed; }
    float pitStart() const { return m_pitStart; }
    float pitEnd() const { return m_pitEnd; }
    float pitU() const { return halfWidth() + m_desc.apron * 0.5f; }
    bool inPits(const Where& w) const;
    // Drag strip, rally stage: where the start and finish lines are (s).
    float finishS() const { return m_closed ? 0.0f : stage() ? m_length - 40.0f : 30.0f + m_desc.length; }
    float startS() const { return m_closed ? 0.0f : stage() ? 90.0f : 30.0f; } // a stage: room for the queue behind it
    bool arena() const { return m_desc.shape == "arena"; }
    bool stage() const { return m_desc.shape == "stage"; }

    // Geometry: the drivable surface (road + apron) and every wall as
    // world triangles; colours per vertex for drawing.
    struct Mesh {
        std::vector<glm::vec3> positions, normals, colors;
        std::vector<uint32_t> indices;
    };
    const Mesh& surface() const { return m_surface; }
    const Mesh& walls() const { return m_walls; }
    const Mesh& markings() const { return m_markings; } // lines, kerbs, start line, pit boxes: just above the road
    // Wall collision: boxes along each wall (centre, half extents, yaw).
    struct WallBox {
        glm::vec3 center{0.0f}, half{0.3f};
        glm::vec3 forward{0.0f, 0.0f, 1.0f};
    };
    const std::vector<WallBox>& wallBoxes() const { return m_wallBoxes; }
    // An arena's floor: inside its wall (a little margin)?
    bool insideArena(const glm::vec3& p, float margin = 0.0f) const;
    // Where the grid spot `slot` is (0 = pole), facing forward. An arena:
    // round the edge, facing the middle; a stage: single file behind the
    // start line.
    glm::vec3 gridPosition(int slot, int cars) const;
    glm::vec3 gridForward(int slot, int cars) const;
    // A stage's land: hills rising and falling away from the road (a
    // height grid, drawn and solid), and its height anywhere on it.
    const Mesh& terrain() const { return m_terrain; }
    float groundHeight(float x, float z) const; // 0 off the grid, or without one

private:
    void sample();
    void buildGeometry();
    float heightOffset(const Sample& s) const; // lift so the inside edge meets the ground
    void buildTerrain();
    TrackDesc m_desc;
    bool m_closed = true;
    float m_length = 0.0f, m_step = 2.0f;
    float m_pitStart = 0.0f, m_pitEnd = 0.0f;
    std::vector<Sample> m_samples;
    Mesh m_surface, m_walls, m_markings, m_terrain;
    glm::vec2 m_gridMin{0.0f};
    float m_cell = 4.0f;
    int m_gx = 0, m_gz = 0;           // terrain grid vertices across and along
    std::vector<float> m_heights;
    std::vector<WallBox> m_wallBoxes;
};

} // namespace racing
