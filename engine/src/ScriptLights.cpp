#include "kke/ScriptLights.h"
#include <algorithm>

namespace kke {

int ScriptLights::add(Lighting& lighting, const std::string& owner, const Desc& desc) {
    for (int i = 1; i < Lighting::kMaxLights; ++i) {
        if (lighting.lights[i].enabled || std::find_if(m_entries.begin(), m_entries.end(), [slot=i](const Entry& e){ return e.slot == slot; }) != m_entries.end()) continue;
        Light& l = lighting.lights[i];
        l = Light{};
        l.enabled = true;
        l.isDirectional = false;
        l.position = desc.position;
        l.color = desc.color;
        l.intensity = desc.intensity;
        l.range = desc.range;
        l.castsShadows = desc.shadows;
        m_entries.push_back({i, owner});
        return i;
    }
    // The key slots are full: a light without shadows goes to the many lights (ENG-F08), reusing a freed one first.
    if (desc.shadows) return -1;
    size_t many = 0;
    while (many < lighting.manyLights.size() && lighting.manyLights[many].enabled) ++many;
    if (many == lighting.manyLights.size()) {
        if (many >= Lighting::kMaxManyLights) return -1;
        lighting.manyLights.emplace_back();
    }
    Light& l = lighting.manyLights[many];
    l = Light{};
    l.enabled = true;
    l.isDirectional = false;
    l.position = desc.position;
    l.color = desc.color;
    l.intensity = desc.intensity;
    l.range = desc.range > 0.0f ? desc.range : Lighting::kDefaultManyRange;
    const int slot = Lighting::kMaxLights + static_cast<int>(many);
    m_entries.push_back({slot, owner});
    return slot;
}

bool ScriptLights::owns(int slot, const std::string& owner) const {
    return std::find_if(m_entries.begin(), m_entries.end(), [slot, owner](const Entry& e){ return e.slot == slot && e.owner == owner; }) != m_entries.end();
}

void ScriptLights::remove(Lighting& lighting, int slot) {
    if (slot >= 0 && (slot < Lighting::kMaxLights || static_cast<size_t>(slot - Lighting::kMaxLights) < lighting.manyLights.size())) {
        lighting.at(slot).enabled = false;
        m_entries.erase(std::remove_if(m_entries.begin(), m_entries.end(), [slot](const Entry& e){ return e.slot == slot; }), m_entries.end());
    }
}

void ScriptLights::releaseAll(Lighting& lighting, const std::string& owner) {
    for (auto& e : m_entries) {
        if (e.owner == owner && e.slot >= 0 && (e.slot < Lighting::kMaxLights || static_cast<size_t>(e.slot - Lighting::kMaxLights) < lighting.manyLights.size())) {
            lighting.at(e.slot).enabled = false;
        }
    }
    m_entries.erase(std::remove_if(m_entries.begin(), m_entries.end(), [owner](const Entry& e){ return e.owner == owner; }), m_entries.end());
}

void ScriptLights::clear(Lighting& lighting) {
    for (auto& e : m_entries) {
        if (e.slot >= 0 && (e.slot < Lighting::kMaxLights || static_cast<size_t>(e.slot - Lighting::kMaxLights) < lighting.manyLights.size())) {
            lighting.at(e.slot).enabled = false;
        }
    }
    m_entries.clear();
}

size_t ScriptLights::count(const std::string& owner) const {
    return static_cast<size_t>(std::count_if(m_entries.begin(), m_entries.end(), [owner](const Entry& e){ return e.owner == owner; }));
}

} // namespace kke
