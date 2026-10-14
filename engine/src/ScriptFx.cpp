#include "kke/ScriptFx.h"

#include <algorithm>

namespace kke {

void ScriptFx::track(EmitterId id, const std::string& owner) { m_entries.push_back({ id, owner }); }

bool ScriptFx::owns(EmitterId id, const std::string& owner) const {
    return std::any_of(m_entries.begin(), m_entries.end(), [&](const Entry& e) { return e.id == id && e.owner == owner; });
}

void ScriptFx::forget(EmitterId id) {
    m_entries.erase(std::remove_if(m_entries.begin(), m_entries.end(), [&](const Entry& e) { return e.id == id; }), m_entries.end());
}

std::vector<ScriptFx::EmitterId> ScriptFx::releaseAll(const std::string& owner) {
    std::vector<EmitterId> ids;
    for (const Entry& e : m_entries)
        if (e.owner == owner) ids.push_back(e.id);
    m_entries.erase(std::remove_if(m_entries.begin(), m_entries.end(), [&](const Entry& e) { return e.owner == owner; }), m_entries.end());
    return ids;
}

std::vector<ScriptFx::EmitterId> ScriptFx::clear() {
    std::vector<EmitterId> ids;
    for (const Entry& e : m_entries) ids.push_back(e.id);
    m_entries.clear();
    return ids;
}

size_t ScriptFx::count(const std::string& owner) const {
    return static_cast<size_t>(std::count_if(m_entries.begin(), m_entries.end(), [&](const Entry& e) { return e.owner == owner; }));
}

} // namespace kke
