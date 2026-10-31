#pragma once
// ScriptTimeline: the timeline.* Lua API over kke::Timeline (docs/TIMELINE.md).
#include "kke/Timeline.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

struct lua_State;

namespace kke {

class ScriptVM;

// Timelines scripts make and play. They advance with the game each frame
// (update); what they report (events, markers, values for bound targets,
// spans, clips) waits in a queue and reaches the scripts at the start of the
// next update, so a script calling seek() or play() is never called back
// from inside its own call.
class ScriptTimeline {
public:
    explicit ScriptTimeline(std::string folder = {}) : m_folder(std::move(folder)) {}
    void bind(ScriptVM& vm);
    // Advances every playing timeline by dt and delivers what happened.
    void update(float dt, ScriptVM& vm);
    // Frees the timelines `source` made.
    void release(const std::string& source);
    void setFolder(std::string f) { m_folder = std::move(f); }

    size_t count() const { return m_timelines.size(); }
    Timeline* timeline(int id);
    static constexpr size_t kMaxTimelines = 256, kMaxPending = 100000;

private:
    struct Item;
    struct Entry {
        Timeline tl;
        std::string source;
        std::map<std::string, int> binders; // "target\nproperty" ("" = any property) -> Lua function ref
        bool ticking = false;
    };
    struct Sink;
    void deliver(ScriptVM& vm);

    std::string m_folder;
    std::map<int, std::unique_ptr<Entry>> m_timelines;
    std::vector<std::shared_ptr<Item>> m_pending;
    int m_nextId = 1;
    ScriptVM* m_vm = nullptr;
};

} // namespace kke
