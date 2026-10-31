#pragma once

#include <glm/glm.hpp>

#include <cstddef>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace kke {

// World and level streaming (ENG-F78, ENG-F64, docs/WORLD_STREAMING.md): which parts of a big world are loaded
// for the players that are in it. Pure logic: it decides, the caller (the Lua `world.*` functions, or C++) loads
// and unloads and says how it went. A "cell" is a box of the world: a square of an open world's grid, or a level
// volume the player walks into. A cell is loaded while an observer (a player, the camera) is within `loadRadius` of
// its box, stays loaded until every observer is beyond `unloadRadius` (so walking back and forth over the edge does
// not load and unload it again and again), and is unloaded farthest first.
//
// Loads are spread out: at most `maxLoadsPerUpdate` start in one update and at most `maxInFlight` are going at
// once, nearest and most important first, so a long walk into new country is a trickle of small loads and not one
// hitch. With `maxResident` the streamer never asks for more than that many cells at once: to load a nearer cell it
// first unloads the farthest, less important one.
struct StreamBox {
    glm::vec3 min{0.0f}, max{0.0f};
    // 0 inside, else the distance from p to the nearest point of the box.
    float distanceTo(const glm::vec3& p) const;
};

struct StreamCell {
    std::string id;
    StreamBox box;
    int priority = 0;       // higher loads first and is the last to be unloaded for room
    bool always = false;    // a persistent level: loaded whatever the observers do, never unloaded for room
    float loadRadius = -1.0f;   // < 0: the streamer's
    float unloadRadius = -1.0f;
};

struct StreamConfig {
    float loadRadius = 100.0f;    // metres from an observer to a cell's box
    float unloadRadius = 130.0f;  // (at least the load radius)
    int maxLoadsPerUpdate = 1;
    int maxInFlight = 1;          // loads asked for and not yet confirmed
    int maxUnloadsPerUpdate = 2;
    size_t maxResident = 0;       // 0 = no limit
    float retryAfter = 5.0f;      // seconds before a cell whose load failed is tried again
    // An open world as a grid: a square cell of this size (0 = none) exists wherever an observer's load radius reaches,
    // with the id "x,z" (cell numbers), unbounded above and below, and is forgotten once unloaded and out of reach.
    float gridSize = 0.0f;
};

enum class CellState { Unknown, Unloaded, Loading, Loaded, Failed };

struct StreamAction {
    enum class Kind { Load, Unload } kind;
    std::string id;
};

class WorldStreamer {
public:
    void setConfig(const StreamConfig& config);
    const StreamConfig& config() const { return m_config; }

    // False when the id is taken. Removing a loaded cell tells the caller to unload it (returned true: it was resident).
    bool addCell(const StreamCell& cell);
    bool removeCell(const std::string& id);
    const StreamCell* cell(const std::string& id) const;

    // Where the players are. With none, only `always` cells are wanted.
    void setObservers(std::vector<glm::vec3> observers) { m_observers = std::move(observers); }

    // What to load and unload now, in the order to do it (unloads first). Each Load is `Loading` until the caller
    // says loaded() or failed(); each Unload is final (the cell is Unloaded at once).
    std::vector<StreamAction> update(float dt);
    void loaded(const std::string& id);
    void failed(const std::string& id);

    CellState state(const std::string& id) const;
    bool isLoaded(const std::string& id) const { return state(id) == CellState::Loaded; }
    std::vector<std::string> resident() const; // loaded cells, and loading ones
    // The grid cell that contains p, as the id the streamer uses ("" without a grid).
    std::string gridCellAt(const glm::vec3& p) const;

    struct Stats {
        size_t cells = 0, loaded = 0, loading = 0, failed = 0;
        size_t loadsAsked = 0, unloadsAsked = 0, failures = 0;
    };
    Stats stats() const;

private:
    struct Entry {
        StreamCell cell;
        CellState state = CellState::Unloaded;
        float retryIn = 0.0f;
        bool implicit = false; // made by the grid
    };
    float distance(const Entry& e) const;
    float loadRadiusOf(const Entry& e) const { return e.cell.loadRadius >= 0.0f ? e.cell.loadRadius : m_config.loadRadius; }
    float unloadRadiusOf(const Entry& e) const;
    void growGrid();

    StreamConfig m_config;
    std::map<std::string, Entry> m_cells;
    std::vector<glm::vec3> m_observers;
    size_t m_loadsAsked = 0, m_unloadsAsked = 0, m_failures = 0;
};

} // namespace kke
