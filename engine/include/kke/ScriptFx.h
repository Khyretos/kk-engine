#pragma once
// ScriptFx remembers which Lua script started which particle emitter
// (fx.start, ENG-G18), so a script moves and stops only its own and its
// emitters stop when the script is reloaded or removed. The emitters
// themselves live in the ScriptModule's kke::ParticleLibrary.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace kke {

class ScriptFx {
public:
    using EmitterId = uint32_t; // kke::ParticleLibrary::EmitterId
    // Records that `owner` (the script's source name) started emitter `id`.
    void track(EmitterId id, const std::string& owner);
    // Returns true if `owner` started emitter `id` and it has not been stopped.
    bool owns(EmitterId id, const std::string& owner) const;
    // Drops emitter `id` (fx.stop).
    void forget(EmitterId id);
    // Drops and returns every emitter `owner` started, for the caller to stop.
    std::vector<EmitterId> releaseAll(const std::string& owner);
    // Drops and returns every emitter of every script.
    std::vector<EmitterId> clear();
    // Returns how many emitters `owner` has running.
    size_t count(const std::string& owner) const;

private:
    struct Entry {
        EmitterId id;
        std::string owner;
    };
    std::vector<Entry> m_entries;
};

} // namespace kke
