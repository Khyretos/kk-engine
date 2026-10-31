#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace kke {

// World and level streaming (ENG-F64, ENG-F78, docs/SCENES.md "Streaming"):
// Unreal's World Partition and level streaming, Unity's additive scenes, as
// one small system. A big world is cut into cells; the cells near the
// player load, the ones far away unload, and the game never stops to load
// a level.
//
// The streamer decides; the game (a StreamBackend) does the work, so it can
// load a scene, spawn objects from a file, build terrain, anything, in the
// background or at once. Decisions:
//   - a cell loads when a focus (the player, the camera, a second player) comes
//     within its load radius, and unloads only when every focus is beyond
//     its unload radius (the gap is the hysteresis: standing on a border does
//     not load and unload every frame)
//   - nearest first, at most a few loads in flight and started per frame, so
//     a fast flight does not stall the game; a load that fails is retried
//     after a delay, a few times; one that takes too long is given up on
//   - cells inside `requiredRadius` are the ones a loading screen waits for
//     (progress(), ready())
//   - persistent cells (the always-there part of the world) load first and
//     never unload; a cell can be pinned (kept) or forced (loaded whatever
//     the distance)
//
// OriginShifter is the other half of a big world: floats lose precision far
// from zero (at 10 km a float step is 1 mm; at 100 km it is 8 mm and
// physics jitters), so the world is moved back instead of the player moving
// out. Unit-tested in tests/test_world_stream.cpp.

struct StreamCell {
    std::string name;               // unique
    glm::vec2 min{ 0.0f }, max{ 0.0f }; // the ground it covers (x, z), metres
    float loadRadius = -1.0f;       // this cell's own radii (-1: the streamer's)
    float unloadRadius = -1.0f;
    float bias = 0.0f;              // metres taken off its distance when choosing what loads first (a cell that matters more)
    bool persistent = false;
    std::string data;               // whatever the game ties to it: a scene's name
};

enum class CellState : uint8_t { Unloaded, Loading, Loaded, Failed };
const char* cellStateName(CellState s);

enum class CellLoad : uint8_t { Pending, Ready, Failed };

class StreamBackend {
public:
    virtual ~StreamBackend() = default;
    // Starts loading the cell (in the background or at once). False (with a reason) when it cannot start.
    virtual bool begin(const StreamCell& cell, std::string* error) = 0;
    // Ready when it is loaded and can be added to the world; Failed (with a reason) if it will never be.
    virtual CellLoad poll(const StreamCell& cell, std::string* error) = 0;
    // The cell is ready: put it in the world (on the main thread).
    virtual void activate(const StreamCell& cell) { (void)cell; }
    // Take it out of the world and free it (also what cancels a load that is no longer wanted).
    virtual void unload(const StreamCell& cell) = 0;
};

struct StreamConfig {
    float loadRadius = 100.0f;      // metres from a focus to the cell's edge
    float unloadRadius = 130.0f;    // beyond this from every focus the cell goes (at least loadRadius)
    float requiredRadius = 40.0f;   // cells this close must be loaded before ready()
    int maxLoading = 2;             // loads in flight at once
    int maxStartsPerUpdate = 1;     // loads begun in one update
    int maxUnloadsPerUpdate = 4;
    float loadTimeout = 30.0f;      // seconds before a load that is still pending is given up
    float retryDelay = 5.0f;        // seconds before a failed cell is tried again
    int maxRetries = 3;
};

struct StreamEvent {
    enum class Kind : uint8_t { Loaded, Unloaded, Failed, Retrying } kind = Kind::Loaded;
    std::string name;
    std::string reason; // Failed: why
};

struct StreamStats {
    uint64_t loads = 0, unloads = 0, failures = 0, retries = 0, timeouts = 0, cancelled = 0;
    int loadedNow = 0, loadingNow = 0;
};

class WorldStreamer {
public:
    explicit WorldStreamer(StreamBackend* backend = nullptr, const StreamConfig& config = {});
    void setBackend(StreamBackend* b) { m_backend = b; }
    StreamConfig& config() { return m_config; }
    const StreamConfig& config() const { return m_config; }

    static constexpr size_t kMaxCells = 100000, kMaxFocus = 16;

    // ---- the world
    // False (with a reason) for a name used already or a cell without an area.
    bool addCell(const StreamCell& cell, std::string* error = nullptr);
    // A grid of square cells named "<prefix>_<x>_<z>" (x and z are cell indices, both ends included); data is the same name.
    size_t addGrid(const std::string& prefix, float cellSize, int x0, int z0, int x1, int z1, std::string* error = nullptr);
    bool hasCell(const std::string& name) const { return m_index.count(name) != 0; }
    const StreamCell* cell(const std::string& name) const;
    size_t cellCount() const { return m_cells.size(); }
    // Unloads everything and forgets the cells.
    void clear();

    // ---- who is looking
    // Up to 16 foci by id (a player, the camera). Moving one is just setting it again.
    bool setFocus(const std::string& id, const glm::vec3& position);
    bool removeFocus(const std::string& id);
    size_t focusCount() const { return m_focus.size(); }

    // ---- control
    // A pinned cell is kept loaded once it is (never unloaded for distance); a forced cell is loaded whatever the distance.
    bool pin(const std::string& name, bool on);
    bool force(const std::string& name, bool on);
    void update(float dt);

    // ---- what is going on
    CellState state(const std::string& name) const;
    std::vector<std::string> loaded() const;
    std::vector<std::string> loading() const;
    // 0..1: of the cells that must be loaded (inside requiredRadius of a focus, persistent, forced), how many are.
    float progress() const;
    // Every cell that must be loaded is (a persistent or forced cell that has failed for good does not count).
    bool ready() const;
    // The distance from the nearest focus to the cell's edge (huge with no focus).
    float distance(const std::string& name) const;
    std::vector<StreamEvent> takeEvents();
    const StreamStats& stats() const { return m_stats; }

private:
    struct Entry {
        StreamCell cell;
        CellState state = CellState::Unloaded;
        double startedAt = 0.0, retryAt = 0.0;
        int failures = 0;
        bool pinned = false, forced = false;
        std::string failure;
    };
    float distanceTo(const Entry& e) const;
    float loadRadiusOf(const Entry& e) const;
    float unloadRadiusOf(const Entry& e) const;
    bool wanted(const Entry& e) const;     // should be loaded now
    bool keep(const Entry& e) const;       // may stay loaded
    bool required(const Entry& e) const;
    void unloadEntry(Entry& e, bool cancelled);
    void fail(Entry& e, const std::string& reason);
    void push(StreamEvent::Kind kind, const std::string& name, const std::string& reason = {});

    StreamBackend* m_backend;
    StreamConfig m_config;
    std::vector<Entry> m_cells;
    std::map<std::string, size_t> m_index;
    std::map<std::string, glm::vec3> m_focus;
    std::vector<StreamEvent> m_events;
    StreamStats m_stats;
    double m_time = 0.0;
};

// A floating origin: the world is shifted instead of the player travelling
// far from zero. update() takes the focus in the world's own (large)
// coordinates and says how far to shift everything this frame (zero most
// frames); the shifts are whole multiples of `step`, so nothing is rounded.
class OriginShifter {
public:
    explicit OriginShifter(double threshold = 2000.0, double step = 1000.0) : m_threshold(threshold > 0.0 ? threshold : 2000.0), m_step(step > 0.0 ? step : 1000.0) {}
    // The shift to apply to every object's local position (subtract it); zero when the focus is still near.
    glm::dvec3 update(const glm::dvec3& focusWorld);
    // Where the local origin is in the world.
    const glm::dvec3& origin() const { return m_origin; }
    glm::dvec3 toWorld(const glm::vec3& local) const { return m_origin + glm::dvec3(local); }
    glm::vec3 toLocal(const glm::dvec3& world) const { return glm::vec3(world - m_origin); }
    int shifts() const { return m_shifts; }

private:
    double m_threshold, m_step;
    glm::dvec3 m_origin{ 0.0 };
    int m_shifts = 0;
};

} // namespace kke
