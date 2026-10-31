#include "kke/WorldStream.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace kke {

float StreamBox::distanceTo(const glm::vec3& p) const {
    const glm::vec3 d = glm::max(glm::max(min - p, p - max), glm::vec3(0.0f));
    return glm::length(d);
}

void WorldStreamer::setConfig(const StreamConfig& config) {
    m_config = config;
    m_config.maxLoadsPerUpdate = std::max(1, m_config.maxLoadsPerUpdate);
    m_config.maxInFlight = std::max(1, m_config.maxInFlight);
    m_config.maxUnloadsPerUpdate = std::max(1, m_config.maxUnloadsPerUpdate);
    m_config.unloadRadius = std::max(m_config.unloadRadius, m_config.loadRadius);
}

float WorldStreamer::unloadRadiusOf(const Entry& e) const {
    const float load = loadRadiusOf(e);
    const float unload = e.cell.unloadRadius >= 0.0f ? e.cell.unloadRadius : std::max(m_config.unloadRadius, load);
    return std::max(unload, load);
}

bool WorldStreamer::addCell(const StreamCell& cell) {
    if (cell.id.empty()) return false;
    auto it = m_cells.find(cell.id);
    if (it != m_cells.end()) {
        if (!it->second.implicit) return false;
        it->second.implicit = false; // a script claims a cell the grid had made: it is now theirs
        it->second.cell = cell;
        return true;
    }
    Entry e;
    e.cell = cell;
    m_cells.emplace(cell.id, std::move(e));
    return true;
}

bool WorldStreamer::removeCell(const std::string& id) {
    auto it = m_cells.find(id);
    if (it == m_cells.end()) return false;
    const bool wasResident = it->second.state == CellState::Loaded || it->second.state == CellState::Loading;
    m_cells.erase(it);
    return wasResident;
}

const StreamCell* WorldStreamer::cell(const std::string& id) const {
    auto it = m_cells.find(id);
    return it == m_cells.end() ? nullptr : &it->second.cell;
}

float WorldStreamer::distance(const Entry& e) const {
    float best = std::numeric_limits<float>::max();
    for (const glm::vec3& o : m_observers) best = std::min(best, e.cell.box.distanceTo(o));
    return best;
}

std::string WorldStreamer::gridCellAt(const glm::vec3& p) const {
    if (m_config.gridSize <= 0.0f) return {};
    return std::to_string(static_cast<long>(std::floor(p.x / m_config.gridSize))) + "," + std::to_string(static_cast<long>(std::floor(p.z / m_config.gridSize)));
}

// Grid cells exist where an observer's load radius reaches, and not once they are unloaded and out of reach.
void WorldStreamer::growGrid() {
    const float s = m_config.gridSize;
    if (s <= 0.0f) return;
    const float reach = m_config.loadRadius;
    for (const glm::vec3& o : m_observers) {
        const long x0 = static_cast<long>(std::floor((o.x - reach) / s)), x1 = static_cast<long>(std::floor((o.x + reach) / s));
        const long z0 = static_cast<long>(std::floor((o.z - reach) / s)), z1 = static_cast<long>(std::floor((o.z + reach) / s));
        for (long z = z0; z <= z1; ++z)
            for (long x = x0; x <= x1; ++x) {
                const std::string id = std::to_string(x) + "," + std::to_string(z);
                if (m_cells.count(id)) continue;
                Entry e;
                e.implicit = true;
                e.cell.id = id;
                const float big = 1.0e6f;
                e.cell.box = { { static_cast<float>(x) * s, -big, static_cast<float>(z) * s }, { static_cast<float>(x + 1) * s, big, static_cast<float>(z + 1) * s } };
                m_cells.emplace(id, std::move(e));
            }
    }
    for (auto it = m_cells.begin(); it != m_cells.end();) {
        const Entry& e = it->second;
        if (e.implicit && e.state == CellState::Unloaded && (m_observers.empty() || distance(e) > unloadRadiusOf(e))) it = m_cells.erase(it);
        else ++it;
    }
}

std::vector<StreamAction> WorldStreamer::update(float dt) {
    std::vector<StreamAction> actions;
    growGrid();
    for (auto& [id, e] : m_cells)
        if (e.state == CellState::Failed && (e.retryIn -= dt) <= 0.0f) e.state = CellState::Unloaded;

    struct Item {
        Entry* e;
        float d;
    };
    std::vector<Item> wanted, toDrop, resident;
    size_t inFlight = 0, residentCount = 0;
    for (auto& [id, e] : m_cells) {
        const float d = m_observers.empty() ? std::numeric_limits<float>::max() : distance(e);
        const bool isResident = e.state == CellState::Loaded || e.state == CellState::Loading;
        if (e.state == CellState::Loading) ++inFlight;
        if (isResident) {
            ++residentCount;
            resident.push_back({ &e, d });
            if (!e.cell.always && d > unloadRadiusOf(e) && e.state == CellState::Loaded) toDrop.push_back({ &e, d }); // (a load in flight is left to finish)
        } else if (e.state == CellState::Unloaded && (e.cell.always || d <= loadRadiusOf(e))) {
            wanted.push_back({ &e, d });
        }
    }
    // Loads: the most important first, then the nearest.
    std::sort(wanted.begin(), wanted.end(), [](const Item& a, const Item& b) {
        if (a.e->cell.priority != b.e->cell.priority) return a.e->cell.priority > b.e->cell.priority;
        if (a.e->cell.always != b.e->cell.always) return a.e->cell.always;
        return a.d < b.d;
    });
    // Unloads: the farthest first.
    std::sort(toDrop.begin(), toDrop.end(), [](const Item& a, const Item& b) { return a.d > b.d; });
    int unloads = 0;
    auto unload = [&](Entry& e) {
        actions.push_back({ StreamAction::Kind::Unload, e.cell.id });
        e.state = CellState::Unloaded;
        ++m_unloadsAsked;
        ++unloads;
        --residentCount;
    };
    for (const Item& it : toDrop) {
        if (unloads >= m_config.maxUnloadsPerUpdate) break;
        unload(*it.e);
    }
    // For room: with a cap, a nearer or more important cell takes the place of the farthest, less important one.
    std::sort(resident.begin(), resident.end(), [](const Item& a, const Item& b) { return a.d > b.d; });
    int loads = 0;
    for (const Item& w : wanted) {
        if (loads >= m_config.maxLoadsPerUpdate || static_cast<int>(inFlight) >= m_config.maxInFlight) break;
        if (m_config.maxResident && residentCount >= m_config.maxResident) {
            // The farthest resident cell that is not persistent, not already going, and less deserving than this one.
            Entry* victim = nullptr;
            for (const Item& r : resident) {
                if (r.e->state != CellState::Loaded || r.e->cell.always) continue;
                if (r.e->cell.priority > w.e->cell.priority) continue;
                if (r.e->cell.priority == w.e->cell.priority && r.d <= w.d) continue;
                victim = r.e;
                break;
            }
            if (!victim || unloads >= m_config.maxUnloadsPerUpdate) break;
            unload(*victim);
            resident.erase(std::remove_if(resident.begin(), resident.end(), [&](const Item& r) { return r.e == victim; }), resident.end());
        }
        actions.push_back({ StreamAction::Kind::Load, w.e->cell.id });
        w.e->state = CellState::Loading;
        ++m_loadsAsked;
        ++loads;
        ++inFlight;
        ++residentCount;
    }
    // Unloads were decided first and loads after, but a load may follow an unload for room: keep unloads first in the list.
    std::stable_sort(actions.begin(), actions.end(), [](const StreamAction& a, const StreamAction& b) { return a.kind == StreamAction::Kind::Unload && b.kind == StreamAction::Kind::Load; });
    return actions;
}

void WorldStreamer::loaded(const std::string& id) {
    if (auto it = m_cells.find(id); it != m_cells.end() && it->second.state == CellState::Loading) it->second.state = CellState::Loaded;
}

void WorldStreamer::failed(const std::string& id) {
    if (auto it = m_cells.find(id); it != m_cells.end() && it->second.state == CellState::Loading) {
        it->second.state = CellState::Failed;
        it->second.retryIn = m_config.retryAfter;
        ++m_failures;
    }
}

CellState WorldStreamer::state(const std::string& id) const {
    auto it = m_cells.find(id);
    return it == m_cells.end() ? CellState::Unknown : it->second.state;
}

std::vector<std::string> WorldStreamer::resident() const {
    std::vector<std::string> out;
    for (const auto& [id, e] : m_cells)
        if (e.state == CellState::Loaded || e.state == CellState::Loading) out.push_back(id);
    return out;
}

WorldStreamer::Stats WorldStreamer::stats() const {
    Stats s;
    s.cells = m_cells.size();
    for (const auto& [id, e] : m_cells) {
        s.loaded += e.state == CellState::Loaded;
        s.loading += e.state == CellState::Loading;
        s.failed += e.state == CellState::Failed;
    }
    s.loadsAsked = m_loadsAsked;
    s.unloadsAsked = m_unloadsAsked;
    s.failures = m_failures;
    return s;
}

} // namespace kke
