#include "kke/ScriptTimeline.h"

#include "kke/ScriptVM.h"

#include <lauxlib.h>
#include <lua.h>

#include <nlohmann/json.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace kke {

using nlohmann::json;

// What a timeline reported, waiting for the scripts.
struct ScriptTimeline::Item {
    enum class Kind { Event, Marker, Value, Active, Clip, Done, Loop } kind = Kind::Event;
    int timeline = 0;
    std::string a, b, c; // event: name, data. marker: name. value: target, property. active: target. clip: target, clip, kind
    float time = 0.0f;   // event or marker time, a clip's local time
    ValueKind valueKind = ValueKind::Scalar;
    glm::vec4 v{ 0.0f };
    bool on = false, loop = false;
    float speed = 1.0f, blend = 0.0f;
    int count = 0;
};

struct ScriptTimeline::Sink : TimelineSink {
    ScriptTimeline& owner;
    int id;
    const Entry& entry;
    Sink(ScriptTimeline& o, int i, const Entry& e) : owner(o), id(i), entry(e) {}

    void push(std::shared_ptr<Item> item) {
        item->timeline = id;
        if (owner.m_pending.size() < kMaxPending) owner.m_pending.push_back(std::move(item));
    }
    void value(const std::string& target, const std::string& property, ValueKind kind, const glm::vec4& v) override {
        // only what a script is listening to
        if (!entry.binders.count(target + "\n" + property) && !entry.binders.count(target + "\n")) return;
        auto i = std::make_shared<Item>();
        i->kind = Item::Kind::Value;
        i->a = target;
        i->b = property;
        i->valueKind = kind;
        i->v = v;
        push(std::move(i));
    }
    void event(const TimelineEvent& e) override {
        auto i = std::make_shared<Item>();
        i->kind = Item::Kind::Event;
        i->a = e.name;
        i->b = e.data;
        i->time = e.time;
        push(std::move(i));
    }
    void marker(const TimelineMarker& m) override {
        auto i = std::make_shared<Item>();
        i->kind = Item::Kind::Marker;
        i->a = m.name;
        i->time = m.time;
        push(std::move(i));
    }
    void active(const std::string& target, bool on) override {
        auto i = std::make_shared<Item>();
        i->kind = Item::Kind::Active;
        i->a = target;
        i->on = on;
        push(std::move(i));
    }
    void clip(const ClipEvent& e) override {
        auto i = std::make_shared<Item>();
        i->kind = Item::Kind::Clip;
        i->a = e.target;
        i->b = e.clip;
        i->c = e.kind == ClipEvent::Kind::Start ? "start" : e.kind == ClipEvent::Kind::Stop ? "stop" : "seek";
        i->time = e.localTime;
        i->loop = e.loop;
        i->speed = e.speed;
        i->blend = e.blend;
        push(std::move(i));
    }
    void finished() override {
        auto i = std::make_shared<Item>();
        i->kind = Item::Kind::Done;
        push(std::move(i));
    }
    void looped(int n) override {
        auto i = std::make_shared<Item>();
        i->kind = Item::Kind::Loop;
        i->count = n;
        push(std::move(i));
    }
};

namespace {

std::string inside(const std::string& folder, const std::string& relative) {
    namespace fs = std::filesystem;
    if (folder.empty() || relative.empty()) return {};
    const fs::path rel(relative);
    if (rel.is_absolute()) return {};
    for (const auto& part : rel)
        if (part == "..") return {};
    return (fs::path(folder) / rel).string();
}

void pushValue(lua_State* L, ValueKind kind, const glm::vec4& v) {
    switch (kind) {
    case ValueKind::Scalar: lua_pushnumber(L, static_cast<double>(v.x)); break;
    case ValueKind::Vec3: ScriptVM::pushVec3(L, glm::vec3(v)); break;
    case ValueKind::Quat:
        lua_createtable(L, 0, 4);
        for (int i = 0; i < 4; ++i) {
            lua_pushnumber(L, static_cast<double>(v[i]));
            lua_setfield(L, -2, std::string(1, "xyzw"[i]).c_str());
        }
        break;
    case ValueKind::Color:
        lua_createtable(L, 0, 4);
        for (int i = 0; i < 4; ++i) {
            lua_pushnumber(L, static_cast<double>(v[i]));
            lua_setfield(L, -2, std::string(1, "rgba"[i]).c_str());
        }
        break;
    }
}

} // namespace

Timeline* ScriptTimeline::timeline(int id) {
    const auto it = m_timelines.find(id);
    return it == m_timelines.end() ? nullptr : &it->second->tl;
}

void ScriptTimeline::deliver(ScriptVM& vm) {
    if (m_pending.empty()) return;
    std::vector<std::shared_ptr<Item>> items;
    items.swap(m_pending);
    for (const std::shared_ptr<Item>& item : items) {
        const int id = item->timeline;
        const auto entry = m_timelines.find(id);
        if (entry == m_timelines.end()) continue; // destroyed since
        const std::string source = entry->second->source;
        const Item& it = *item;
        switch (it.kind) {
        case Item::Kind::Value: {
            const auto& binders = entry->second->binders;
            auto b = binders.find(it.a + "\n" + it.b);
            if (b == binders.end()) b = binders.find(it.a + "\n");
            if (b == binders.end()) break;
            vm.callRef(b->second, source, [&](lua_State* L) {
                lua_pushstring(L, it.b.c_str());
                pushValue(L, it.valueKind, it.v);
                return 2;
            });
            break;
        }
        case Item::Kind::Event:
            vm.callHookWith("TimelineEvent", [&](lua_State* L) {
                lua_createtable(L, 0, 4);
                lua_pushinteger(L, id);
                lua_setfield(L, -2, "timeline");
                lua_pushstring(L, it.a.c_str());
                lua_setfield(L, -2, "name");
                lua_pushstring(L, it.b.c_str());
                lua_setfield(L, -2, "data");
                lua_pushnumber(L, static_cast<double>(it.time));
                lua_setfield(L, -2, "time");
                return 1;
            });
            break;
        case Item::Kind::Marker:
            vm.callHookWith("TimelineMarker", [&](lua_State* L) {
                lua_createtable(L, 0, 3);
                lua_pushinteger(L, id);
                lua_setfield(L, -2, "timeline");
                lua_pushstring(L, it.a.c_str());
                lua_setfield(L, -2, "name");
                lua_pushnumber(L, static_cast<double>(it.time));
                lua_setfield(L, -2, "time");
                return 1;
            });
            break;
        case Item::Kind::Active:
            vm.callHookWith("TimelineActive", [&](lua_State* L) {
                lua_createtable(L, 0, 3);
                lua_pushinteger(L, id);
                lua_setfield(L, -2, "timeline");
                lua_pushstring(L, it.a.c_str());
                lua_setfield(L, -2, "target");
                lua_pushboolean(L, it.on);
                lua_setfield(L, -2, "on");
                return 1;
            });
            break;
        case Item::Kind::Clip:
            vm.callHookWith("TimelineClip", [&](lua_State* L) {
                lua_createtable(L, 0, 8);
                lua_pushinteger(L, id);
                lua_setfield(L, -2, "timeline");
                lua_pushstring(L, it.a.c_str());
                lua_setfield(L, -2, "target");
                lua_pushstring(L, it.b.c_str());
                lua_setfield(L, -2, "clip");
                lua_pushstring(L, it.c.c_str());
                lua_setfield(L, -2, "kind");
                lua_pushnumber(L, static_cast<double>(it.time));
                lua_setfield(L, -2, "time");
                lua_pushboolean(L, it.loop);
                lua_setfield(L, -2, "loop");
                lua_pushnumber(L, static_cast<double>(it.speed));
                lua_setfield(L, -2, "speed");
                lua_pushnumber(L, static_cast<double>(it.blend));
                lua_setfield(L, -2, "blend");
                return 1;
            });
            break;
        case Item::Kind::Done:
        case Item::Kind::Loop: {
            const bool done = it.kind == Item::Kind::Done;
            vm.callHookWith(done ? "TimelineDone" : "TimelineLoop", [&](lua_State* L) {
                lua_createtable(L, 0, 2);
                lua_pushinteger(L, id);
                lua_setfield(L, -2, "timeline");
                if (!done) {
                    lua_pushinteger(L, it.count);
                    lua_setfield(L, -2, "count");
                }
                return 1;
            });
            break;
        }
        }
    }
}

void ScriptTimeline::update(float dt, ScriptVM& vm) {
    deliver(vm); // what script calls since the last frame produced
    for (auto& [id, entry] : m_timelines) {
        if (!entry->tl.playing()) continue;
        Sink sink(*this, id, *entry);
        entry->ticking = true;
        entry->tl.update(dt, sink);
        entry->ticking = false;
    }
    deliver(vm);
}

void ScriptTimeline::release(const std::string& source) {
    for (auto it = m_timelines.begin(); it != m_timelines.end();) {
        if (it->second->source == source) {
            if (m_vm)
                for (const auto& [key, ref] : it->second->binders) m_vm->unref(ref);
            it = m_timelines.erase(it);
        } else {
            ++it;
        }
    }
}

void ScriptTimeline::bind(ScriptVM& vm) {
    m_vm = &vm;
    auto entryArg = [this](lua_State* L, int idx, const char* what) -> std::pair<int, Entry&> {
        const int id = static_cast<int>(luaL_checkinteger(L, idx));
        const auto it = m_timelines.find(id);
        if (it == m_timelines.end()) luaL_error(L, "%s: no timeline %d (destroyed, or its script was reloaded)", what, id);
        return { id, *it->second };
    };
    auto add = [this](lua_State* L, Timeline&& tl, const std::string& source) {
        if (m_timelines.size() >= kMaxTimelines) luaL_error(L, "timeline.create: %d timelines already (timeline.destroy the ones you are done with)", static_cast<int>(kMaxTimelines));
        auto e = std::make_unique<Entry>();
        e->tl = std::move(tl);
        e->source = source;
        const int id = m_nextId++;
        m_timelines[id] = std::move(e);
        return id;
    };

    // timeline.create{duration, wrap, speed, tracks, events, markers, spans, clips, subs} -> timeline
    vm.registerFunction("timeline", "create", [this, &vm, add](lua_State* L) {
        luaL_checktype(L, 1, LUA_TTABLE);
        Timeline tl;
        std::string error;
        if (!Timeline::fromJson(ScriptVM::toJson(L, 1), tl, &error)) luaL_error(L, "timeline.create: %s", error.c_str());
        lua_pushinteger(L, add(L, std::move(tl), vm.currentSource()));
        return 1;
    });
    // timeline.load("cutscenes/intro.json") -> timeline, or nil and why
    vm.registerFunction("timeline", "load", [this, &vm, add](lua_State* L) {
        const std::string path = inside(m_folder, luaL_checkstring(L, 1));
        auto fail = [&](const std::string& why) {
            lua_pushnil(L);
            lua_pushstring(L, why.c_str());
            return 2;
        };
        if (path.empty()) return fail("the file must be inside the game's folder");
        std::ifstream in(path, std::ios::binary);
        if (!in) return fail(path + ": can't open");
        std::stringstream ss;
        ss << in.rdbuf();
        json j = json::parse(ss.str(), nullptr, false);
        if (j.is_discarded()) return fail(path + ": not valid JSON");
        Timeline tl;
        std::string error;
        if (!Timeline::fromJson(j, tl, &error)) return fail(path + ": " + error);
        lua_pushinteger(L, add(L, std::move(tl), vm.currentSource()));
        return 1;
    });
    // timeline.play(tl [, {from, speed, wrap}])
    vm.registerFunction("timeline", "play", [entryArg](lua_State* L) {
        Entry& e = entryArg(L, 1, "timeline.play").second;
        float from = -1.0f;
        if (lua_istable(L, 2)) {
            from = ScriptVM::fieldNumber(L, 2, "from", -1.0f);
            e.tl.speed = ScriptVM::fieldNumber(L, 2, "speed", e.tl.speed);
            const std::string wrap = ScriptVM::fieldString(L, 2, "wrap", "");
            if (!wrap.empty() && !wrapModeFromName(wrap, e.tl.wrap)) luaL_error(L, "timeline.play: wrap is \"once\", \"loop\" or \"pingpong\"");
        }
        e.tl.play(from);
        return 0;
    });
    vm.registerFunction("timeline", "pause", [entryArg](lua_State* L) {
        entryArg(L, 1, "timeline.pause").second.tl.pause();
        return 0;
    });
    vm.registerFunction("timeline", "resume", [entryArg](lua_State* L) {
        entryArg(L, 1, "timeline.resume").second.tl.resume();
        return 0;
    });
    vm.registerFunction("timeline", "stop", [entryArg, this](lua_State* L) {
        auto [id, e] = entryArg(L, 1, "timeline.stop");
        Sink sink(*this, id, e);
        e.tl.stop(&sink);
        return 0;
    });
    // timeline.seek(tl, seconds [, fireEvents])
    vm.registerFunction("timeline", "seek", [entryArg, this](lua_State* L) {
        auto [id, e] = entryArg(L, 1, "timeline.seek");
        const double t = luaL_checknumber(L, 2);
        if (!std::isfinite(t)) luaL_error(L, "timeline.seek: the time must be a number");
        Sink sink(*this, id, e);
        e.tl.seek(static_cast<float>(t), sink, lua_toboolean(L, 3) != 0);
        return 0;
    });
    vm.registerFunction("timeline", "seekToMarker", [entryArg, this](lua_State* L) {
        auto [id, e] = entryArg(L, 1, "timeline.seekToMarker");
        Sink sink(*this, id, e);
        lua_pushboolean(L, e.tl.seekToMarker(luaL_checkstring(L, 2), sink, lua_toboolean(L, 3) != 0));
        return 1;
    });
    vm.registerFunction("timeline", "time", [entryArg](lua_State* L) {
        lua_pushnumber(L, static_cast<double>(entryArg(L, 1, "timeline.time").second.tl.time()));
        return 1;
    });
    vm.registerFunction("timeline", "duration", [entryArg](lua_State* L) {
        lua_pushnumber(L, static_cast<double>(entryArg(L, 1, "timeline.duration").second.tl.duration()));
        return 1;
    });
    vm.registerFunction("timeline", "playing", [entryArg](lua_State* L) {
        lua_pushboolean(L, entryArg(L, 1, "timeline.playing").second.tl.playing());
        return 1;
    });
    vm.registerFunction("timeline", "finished", [entryArg](lua_State* L) {
        lua_pushboolean(L, entryArg(L, 1, "timeline.finished").second.tl.finished());
        return 1;
    });
    vm.registerFunction("timeline", "loops", [entryArg](lua_State* L) {
        lua_pushinteger(L, entryArg(L, 1, "timeline.loops").second.tl.loops());
        return 1;
    });
    // timeline.speed(tl [, speed]): negative plays backwards
    vm.registerFunction("timeline", "speed", [entryArg](lua_State* L) {
        Entry& e = entryArg(L, 1, "timeline.speed").second;
        if (!lua_isnoneornil(L, 2)) {
            const double s = luaL_checknumber(L, 2);
            if (!std::isfinite(s)) luaL_error(L, "timeline.speed: a number");
            e.tl.speed = static_cast<float>(s);
            return 0;
        }
        lua_pushnumber(L, static_cast<double>(e.tl.speed));
        return 1;
    });
    // timeline.bind(tl, target [, property], function(property, value) end)
    vm.registerFunction("timeline", "bind", [entryArg, &vm](lua_State* L) {
        Entry& e = entryArg(L, 1, "timeline.bind").second;
        const std::string target = luaL_checkstring(L, 2);
        const bool withProperty = lua_isstring(L, 3) != 0 && lua_isfunction(L, 4);
        const std::string property = withProperty ? lua_tostring(L, 3) : "";
        const int fn = withProperty ? 4 : 3;
        luaL_checktype(L, fn, LUA_TFUNCTION);
        const std::string key = target + "\n" + property;
        if (const auto old = e.binders.find(key); old != e.binders.end()) vm.unref(old->second);
        e.binders[key] = vm.ref(L, fn);
        return 0;
    });
    vm.registerFunction("timeline", "unbind", [entryArg, &vm](lua_State* L) {
        Entry& e = entryArg(L, 1, "timeline.unbind").second;
        const std::string key = std::string(luaL_checkstring(L, 2)) + "\n" + (lua_isstring(L, 3) ? lua_tostring(L, 3) : "");
        if (const auto old = e.binders.find(key); old != e.binders.end()) {
            vm.unref(old->second);
            e.binders.erase(old);
        }
        return 0;
    });
    vm.registerFunction("timeline", "destroy", [this, &vm, entryArg](lua_State* L) {
        auto [id, e] = entryArg(L, 1, "timeline.destroy");
        if (e.ticking) luaL_error(L, "timeline.destroy: this timeline is ticking");
        for (const auto& [key, ref] : e.binders) vm.unref(ref);
        m_timelines.erase(id);
        return 0;
    });
    vm.registerFunction("timeline", "exists", [this](lua_State* L) {
        lua_pushboolean(L, m_timelines.count(static_cast<int>(luaL_checkinteger(L, 1))) != 0);
        return 1;
    });
    vm.registerFunction("timeline", "count", [this](lua_State* L) {
        lua_pushinteger(L, static_cast<lua_Integer>(m_timelines.size()));
        return 1;
    });
    // timeline.json(tl) -> the timeline as JSON text (to save, or to show)
    vm.registerFunction("timeline", "json", [entryArg](lua_State* L) {
        const std::string text = entryArg(L, 1, "timeline.json").second.tl.toJson().dump(2);
        lua_pushlstring(L, text.data(), text.size());
        return 1;
    });
}

} // namespace kke
