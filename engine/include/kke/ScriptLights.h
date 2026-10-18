#pragma once
// ScriptLights manages point lights placed by Lua scripts in free engine slots.
#include "kke/Application.h"

#include <cstddef>
#include <string>
#include <vector>

namespace kke {

class ScriptLights {
public:
    struct Desc {
        glm::vec3 position{0.0f};
        glm::vec3 color{1.0f};
        float intensity = 1.0f;
        float range = 10.0f;
        bool shadows = false;
    };
    // Adds a new point light to the first available free slot and returns its index, or -1 if none are free.
    // With the 16 key slots full, a light without shadows goes to Lighting::manyLights (slot 16 and up).
    int add(Lighting& lighting, const std::string& owner, const Desc& desc);
    // Returns true if the specified slot is owned by the given owner; otherwise false.
    bool owns(int slot, const std::string& owner) const;
    // Turns off the light in the specified slot and frees it for reuse.
    void remove(Lighting& lighting, int slot);
    // Removes all lights placed by the specified owner from their slots.
    void releaseAll(Lighting& lighting, const std::string& owner);
    // Clears every script-placed light from all slots in the engine.
    void clear(Lighting& lighting);
    // Returns the number of lights currently owned by the specified owner.
    size_t count(const std::string& owner) const;

private:
    struct Entry {
        int slot;
        std::string owner;
    };
    std::vector<Entry> m_entries;
};

} // namespace kke
