#include "kke/WorldStream.h"

#include <algorithm>
#include <cmath>

namespace kke {

const char* cellStateName(CellState s) {
    switch (s) {
    case CellState::Unloaded: return "unloaded";
    case CellState::Loading: return "loading";
    case CellState::Loaded: return "loaded";
    case CellState::Failed: return "failed";
    }
    return "unloaded";
}

WorldStreamer::WorldStreamer(StreamBackend* backend, const StreamConfig& config) : m_backend(backend), m_config(config) {}

bool WorldStreamer::addCell(const StreamCell& cell, std::string* error) {
    auto fail = [&](const std::string& why) {
        if (error) *error = why;
        return false;
    };
    if (cell.name.empty()) return fail("a cell needs a name");
    if (m_index.count(cell.name)) return fail("there is a cell called '" + cell.name + "' already");
    if (!(cell.max.x > cell.min.x) || !(cell.max.y > cell.min.y) || !std::isfinite(cell.min.x + cell.min.y + cell.max.x + cell.max.y))
        return fail("cell '" + cell.name + "' has no area (max must be above min, both ways)");
    if (m_cells.size() >= kMaxCells) return fail("more than " + std::to_string(kMaxCells) + " cells");
    Entry e;
    e.cell = cell;
    m_index[cell.name] = m_cells.size();
    m_cells.push_back(std::move(e));
    return true;
}

size_t WorldStreamer::addGrid(const std::string& prefix, float size, int x0, int z0, int x1, int z1, std::string* error) {
    if (!(size > 0.0f) || !std::isfinite(size)) {
        if (error) *error = "the cell size must be above 0";
        return 0;
    }
    if (x1 < x0 || z1 < z0) {
        if (error) *error = "the grid's last cell comes before its first";
        return 0;
    }
    size_t added = 0;
    for (int z = z0; z <= z1; ++z)
        for (int x = x0; x <= x1; ++x) {
            StreamCell c;
            c.name = prefix + "_" + std::to_string(x) + "_" + std::to_string(z);
            c.data = c.name;
            c.min = { static_cast<float>(x) * size, static_cast<float>(z) * size };
            c.max = { static_cast<float>(x + 1) * size, static_cast<float>(z + 1) * size };
            if (!addCell(c, error)) return added;
            ++added;
        }
    return added;
}

const StreamCell* WorldStreamer::cell(const std::string& name) const {
    const auto it = m_index.find(name);
    return it == m_index.end() ? nullptr : &m_cells[it->second].cell;
}

void WorldStreamer::clear() {
    if (m_backend)
        for (Entry& e : m_cells)
            if (e.state == CellState::Loaded || e.state == CellState::Loading) m_backend->unload(e.cell);
    m_cells.clear();
    m_index.clear();
    m_events.clear();
    m_stats.loadedNow = m_stats.loadingNow = 0;
}

bool WorldStreamer::setFocus(const std::string& id, const glm::vec3& position) {
    if (!std::isfinite(position.x + position.y + position.z)) return false;
    if (!m_focus.count(id) && m_focus.size() >= kMaxFocus) return false;
    m_focus[id] = position;
    return true;
}

bool WorldStreamer::removeFocus(const std::string& id) { return m_focus.erase(id) != 0; }

bool WorldStreamer::pin(const std::string& name, bool on) {
    const auto it = m_index.find(name);
    if (it == m_index.end()) return false;
    m_cells[it->second].pinned = on;
    return true;
}

bool WorldStreamer::force(const std::string& name, bool on) {
    const auto it = m_index.find(name);
    if (it == m_index.end()) return false;
    m_cells[it->second].forced = on;
    return true;
}

float WorldStreamer::distanceTo(const Entry& e) const {
    float best = 1.0e30f;
    for (const auto& [id, p] : m_focus) {
        const float dx = std::max({ e.cell.min.x - p.x, 0.0f, p.x - e.cell.max.x });
        const float dz = std::max({ e.cell.min.y - p.z, 0.0f, p.z - e.cell.max.y });
        best = std::min(best, std::sqrt(dx * dx + dz * dz));
    }
    return best;
}

float WorldStreamer::loadRadiusOf(const Entry& e) const { return e.cell.loadRadius >= 0.0f ? e.cell.loadRadius : m_config.loadRadius; }
float WorldStreamer::unloadRadiusOf(const Entry& e) const {
    const float u = e.cell.unloadRadius >= 0.0f ? e.cell.unloadRadius : m_config.unloadRadius;
    return std::max(u, loadRadiusOf(e)); // never closer than it loads: it would load and unload every frame
}

bool WorldStreamer::wanted(const Entry& e) const { return e.cell.persistent || e.forced || distanceTo(e) <= loadRadiusOf(e); }
bool WorldStreamer::keep(const Entry& e) const { return e.cell.persistent || e.pinned || e.forced || distanceTo(e) <= unloadRadiusOf(e); }
bool WorldStreamer::required(const Entry& e) const {
    return e.cell.persistent || e.forced || distanceTo(e) <= std::min(m_config.requiredRadius, loadRadiusOf(e));
}

void WorldStreamer::push(StreamEvent::Kind kind, const std::string& name, const std::string& reason) {
    if (m_events.size() < 100000) m_events.push_back({ kind, name, reason });
}

void WorldStreamer::unloadEntry(Entry& e, bool cancelled) {
    if (m_backend) m_backend->unload(e.cell);
    e.state = CellState::Unloaded;
    if (cancelled) {
        ++m_stats.cancelled;
    } else {
        ++m_stats.unloads;
        push(StreamEvent::Kind::Unloaded, e.cell.name);
    }
}

void WorldStreamer::fail(Entry& e, const std::string& reason) {
    e.state = CellState::Failed;
    ++e.failures;
    ++m_stats.failures;
    e.retryAt = m_time + static_cast<double>(m_config.retryDelay);
    e.failure = reason;
    push(StreamEvent::Kind::Failed, e.cell.name, reason);
}

void WorldStreamer::update(float dt) {
    if (dt > 0.0f && std::isfinite(dt)) m_time += static_cast<double>(dt);
    if (!m_backend) return;

    auto settle = [&](Entry& e) { // a load that is under way: has it finished?
        std::string error;
        switch (m_backend->poll(e.cell, &error)) {
        case CellLoad::Ready:
            m_backend->activate(e.cell);
            e.state = CellState::Loaded;
            e.failures = 0;
            ++m_stats.loads;
            push(StreamEvent::Kind::Loaded, e.cell.name);
            break;
        case CellLoad::Failed:
            m_backend->unload(e.cell);
            fail(e, error.empty() ? "the load failed" : error);
            break;
        case CellLoad::Pending:
            if (m_time - e.startedAt > static_cast<double>(m_config.loadTimeout)) {
                ++m_stats.timeouts;
                m_backend->unload(e.cell); // cancels it
                fail(e, "timed out after " + std::to_string(static_cast<int>(m_config.loadTimeout)) + " s");
            }
            break;
        }
    };

    // 1. loads under way: finished, no longer wanted, or too slow
    for (Entry& e : m_cells) {
        if (e.state != CellState::Loading) continue;
        if (!keep(e)) {
            unloadEntry(e, /*cancelled=*/true);
            continue;
        }
        settle(e);
    }
    // 2. failed cells that are wanted again are tried again after their delay, a few times
    for (Entry& e : m_cells)
        if (e.state == CellState::Failed && e.failures <= m_config.maxRetries && m_time >= e.retryAt && wanted(e)) {
            e.state = CellState::Unloaded;
            ++m_stats.retries;
            push(StreamEvent::Kind::Retrying, e.cell.name, e.failure);
        }
    // 3. cells too far from everyone go, the farthest first
    {
        std::vector<std::pair<float, Entry*>> toUnload;
        for (Entry& e : m_cells)
            if (e.state == CellState::Loaded && !keep(e)) toUnload.emplace_back(distanceTo(e), &e);
        std::sort(toUnload.begin(), toUnload.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        int n = 0;
        for (auto& [d, e] : toUnload) {
            if (n++ >= m_config.maxUnloadsPerUpdate) break;
            unloadEntry(*e, false);
        }
    }
    // 4. cells near someone load, the nearest first, a few at a time
    {
        int loading = 0;
        for (const Entry& e : m_cells) loading += e.state == CellState::Loading;
        std::vector<std::pair<float, Entry*>> toLoad;
        for (Entry& e : m_cells)
            if (e.state == CellState::Unloaded && wanted(e)) toLoad.emplace_back(e.cell.persistent ? -1.0e30f : distanceTo(e) - e.cell.bias, &e);
        std::sort(toLoad.begin(), toLoad.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        int started = 0;
        for (auto& [d, e] : toLoad) {
            if (loading >= m_config.maxLoading || started >= m_config.maxStartsPerUpdate) break;
            std::string error;
            if (!m_backend->begin(e->cell, &error)) {
                fail(*e, error.empty() ? "the load could not start" : error);
                continue;
            }
            e->state = CellState::Loading;
            e->startedAt = m_time;
            ++started;
            ++loading;
            settle(*e); // a backend that loads at once is done now
            if (e->state != CellState::Loading) --loading;
        }
    }
    m_stats.loadedNow = 0;
    m_stats.loadingNow = 0;
    for (const Entry& e : m_cells) {
        m_stats.loadedNow += e.state == CellState::Loaded;
        m_stats.loadingNow += e.state == CellState::Loading;
    }
}

CellState WorldStreamer::state(const std::string& name) const {
    const auto it = m_index.find(name);
    return it == m_index.end() ? CellState::Unloaded : m_cells[it->second].state;
}

std::vector<std::string> WorldStreamer::loaded() const {
    std::vector<std::string> out;
    for (const Entry& e : m_cells)
        if (e.state == CellState::Loaded) out.push_back(e.cell.name);
    return out;
}

std::vector<std::string> WorldStreamer::loading() const {
    std::vector<std::string> out;
    for (const Entry& e : m_cells)
        if (e.state == CellState::Loading) out.push_back(e.cell.name);
    return out;
}

float WorldStreamer::progress() const {
    size_t need = 0, have = 0;
    for (const Entry& e : m_cells) {
        if (!required(e)) continue;
        ++need;
        have += e.state == CellState::Loaded;
    }
    return need == 0 ? 1.0f : static_cast<float>(have) / static_cast<float>(need);
}

bool WorldStreamer::ready() const {
    for (const Entry& e : m_cells) {
        if (!required(e) || e.state == CellState::Loaded) continue;
        if (e.state == CellState::Failed && e.failures > m_config.maxRetries) continue; // given up on: it will not come
        return false;
    }
    return true;
}

float WorldStreamer::distance(const std::string& name) const {
    const auto it = m_index.find(name);
    return it == m_index.end() ? 1.0e30f : distanceTo(m_cells[it->second]);
}

std::vector<StreamEvent> WorldStreamer::takeEvents() {
    std::vector<StreamEvent> out;
    out.swap(m_events);
    return out;
}

// ---- the floating origin -------------------------------------------------------------------

glm::dvec3 OriginShifter::update(const glm::dvec3& focusWorld) {
    const glm::dvec3 d = focusWorld - m_origin;
    if (!(std::max({ std::abs(d.x), std::abs(d.y), std::abs(d.z) }) > m_threshold)) return glm::dvec3(0.0);
    const glm::dvec3 shift(std::round(d.x / m_step) * m_step, std::round(d.y / m_step) * m_step, std::round(d.z / m_step) * m_step);
    if (shift == glm::dvec3(0.0)) return shift;
    m_origin += shift;
    ++m_shifts;
    return shift;
}

} // namespace kke
