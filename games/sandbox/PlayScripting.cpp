// The sandbox's node graphs: play-to-make's Intermediate level
// (docs/PLAY_TO_MAKE.md). The play blocks are bound in a ScriptVM over the
// sandbox's world (kke/PlayScript.h), every graph is compiled to Lua and
// loaded into it (kke/NodeGraph.h), and GraphEditor (RmlUi) edits them
// live. Recipes are the palette blocks' own graphs: the bat's is what
// knocks people over.

#include "GraphEditor.h"
#include "SandboxModule.h"

#include "kke/Application.h"
#include "kke/DevTools.h"
#include "kke/Log.h"
#include "kke/NodeGraph.h"
#include "kke/PlayScript.h"
#include "kke/ai/AiWorld.h"
#include "kke/ai/Clips.h"
#include "kke/modules/AudioModule.h"
#include "kke/modules/OrbitCameraModule.h"
#include "kke/modules/UiModule.h"
#if KKE_ENABLE_LUA
#include "kke/ScriptVM.h"
#include "kke/ai/AiScript.h"

#include <lua.h>
#endif


#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <cstdio>

namespace kke_sandbox {

namespace {

// How many things one graph may bring out at once (docs/OPTIMIZATION.md
// rule 5: everything that can pile up has a cap).
constexpr size_t kMaxSpawnsPerGraph = 200;

const char* const kLevelSource = "graph:level";
std::string thingSource(uint32_t id) { return "graph:thing:" + std::to_string(id); }
std::string recipeSource(const std::string& block) { return "graph:recipe:" + block; }

} // namespace

struct SandboxModule::PlayGraphs {
    // The sandbox's world as the play blocks see it.
    struct World : kke::IPlayWorld {
        SandboxModule& s;
        PlayGraphs& g;
        World(SandboxModule& sandbox, PlayGraphs& graphs) : s(sandbox), g(graphs) {}

        Object* thing(uint32_t id) const {
            if (std::find(g.removing.begin(), g.removing.end(), id) != g.removing.end()) return nullptr;
            return s.find(id);
        }
        std::vector<BlockInfo> blocks() const override {
            std::vector<BlockInfo> out;
            for (size_t i = 0; i < s.m_blocks.size(); ++i) {
                if (s.m_blockAssets[i].empty()) continue;
                const kke::PlayBlock& b = s.m_blocks[i];
                out.push_back({ b.id, b.label,
                                b.kind == kke::PlayBlockKind::Character ? "character"
                                : b.kind == kke::PlayBlockKind::Tool    ? "tool"
                                : b.kind == kke::PlayBlockKind::Animal  ? "animal"
                                                                        : "prop" });
            }
            return out;
        }
        uint32_t spawn(const std::string& block, const glm::vec3& pos, float yaw, const std::string& owner) override {
            const size_t mine = size_t(std::count_if(s.m_objects.begin(), s.m_objects.end(), [&](const Object& o) { return o.owner == owner; }));
            if (mine >= kMaxSpawnsPerGraph) return 0;
            for (size_t i = 0; i < s.m_blocks.size(); ++i) {
                if (s.m_blocks[i].id != block || s.m_blocks[i].kind == kke::PlayBlockKind::Tool) continue;
                const std::string asset = kke::chooseAsset(s.m_blocks[i], s.m_blockAssets[i], s.m_lookPick++);
                if (asset.empty()) return 0;
                Object* o = s.spawnObject(asset, pos, yaw);
                if (!o) return 0;
                o->owner = owner;
                return o->id;
            }
            return 0;
        }
        bool remove(uint32_t id) override {
            // At the end of the update: the thing's own graph may be what's running.
            if (!thing(id)) return false;
            g.removing.push_back(id);
            return true;
        }
        bool exists(uint32_t id) const override { return thing(id) != nullptr; }
        bool ragdoll(uint32_t id, const glm::vec3& push) override {
            Object* o = thing(id);
            if (!o || !o->character || s.isDown(*o)) return false;
            s.ragdoll(*o, push);
            return s.isDown(*o);
        }
        bool standUp(uint32_t id) override {
            Object* o = thing(id);
            if (!o || !o->ragdoll) return false;
            s.standUp(*o);
            return true;
        }
        bool isDown(uint32_t id) const override {
            const Object* o = thing(id);
            return o && s.isDown(*o);
        }
        bool stagger(uint32_t id, const glm::vec3& push) override {
            Object* o = thing(id);
            return o && s.stagger(*o, push);
        }
        bool lookAt(uint32_t id, uint32_t at, const glm::vec3& point) override {
            Object* o = thing(id);
            if (!o || !o->character || (at && !thing(at))) return false;
            s.lookAt(*o, at, point);
            return o->looking;
        }
        bool lookAway(uint32_t id) override {
            Object* o = thing(id);
            if (!o || !o->looking) return false;
            s.lookAway(*o);
            return true;
        }
        bool swingAt(uint32_t id) override {
            Object* o = thing(id);
            if (!o) return false;
            glm::vec3 mn, mx;
            s.worldBounds(*o, mn, mx);
            return s.swingBatAt(glm::vec3((mn.x + mx.x) * 0.5f, mn.y, (mn.z + mx.z) * 0.5f));
        }
        void sound(const std::string& name, const glm::vec3& pos) override {
            auto* audio = s.m_app->getModule<kke::AudioModule>();
            if (!audio) return;
            using M = kke::AudioMaterialTable;
            static const std::map<std::string, uint32_t> kMaterials{
                { "bonk", M::Wood }, { "wood", M::Wood },     { "stone", M::Stone }, { "metal", M::Metal },
                { "glass", M::Glass }, { "rubber", M::Rubber }, { "dirt", M::Dirt },   { "plastic", M::Plastic },
            };
            auto it = kMaterials.find(name);
            const glm::vec3 at = std::isfinite(pos.x) && std::isfinite(pos.y) && std::isfinite(pos.z) ? pos : s.m_app->camera().target;
            audio->playImpact(at, it != kMaterials.end() ? it->second : uint32_t(M::Wood), 1.0f);
        }
        void say(const std::string& text) override {
            g.said = text.substr(0, 160);
            g.sayUntil = g.now + 4.0;
        }
        double addScore(double points) override {
            g.score += points;
            g.scored = true;
            return g.score;
        }
        double score() const override { return g.score; }
        glm::vec3 position(uint32_t id) const override {
            const Object* o = thing(id);
            return o ? o->position : glm::vec3(0.0f);
        }
        std::string blockOf(uint32_t id) const override {
            const Object* o = thing(id);
            return o ? s.blockOf(o->asset) : std::string();
        }
        void removeOwnedBy(const std::string& owner) override {
            std::vector<uint32_t> ids;
            for (const Object& o : s.m_objects)
                if (o.owner == owner) ids.push_back(o.id);
            for (uint32_t id : ids) s.removeObject(id);
        }
    };

    struct Event {
        enum class Kind { Hit, Clicked, Placed, FellOver, StoodUp } kind;
        uint32_t thing = 0;
        glm::vec3 point{0.0f}, push{0.0f};
    };

    GraphEditor editor;
    kke::NodeLibrary library;
    kke::NodeGraph level;
    std::map<std::string, kke::NodeGraph> recipes; // block id -> graph

    // What the editor has open: a copy, written back on every change (the
    // things' graphs live in a vector that moves).
    enum class Target { None, Level, Recipe, Thing };
    Target target = Target::None;
    std::string targetRecipe;
    uint32_t targetThing = 0;
    kke::NodeGraph editing;
    std::string editingSource;

    std::vector<Event> events;
    std::vector<uint32_t> removing;
    double now = 0.0;
    std::string said;
    double sayUntil = -1.0;
    double score = 0.0;
    bool scored = false;
    std::vector<std::pair<std::string, int>> ran;

    // Animals and the people they see (ai.add from the animal recipes).
    kke::ai::AiWorld ai{ 2026 };
    std::map<uint32_t, std::string> animPlaying; // thing -> the AI anim its model shows

#if KKE_ENABLE_LUA
    std::unique_ptr<kke::ScriptVM> vm;
    std::unique_ptr<World> world;
    struct Loaded { kke::CompiledGraph compiled; };
    std::map<std::string, Loaded> loaded; // by source
#endif
};

SandboxModule::SandboxModule() = default;
SandboxModule::~SandboxModule() = default;

bool SandboxModule::lookAvailable() const {
#if KKE_ENABLE_LUA
    return m_graphs && m_graphs->vm != nullptr;
#else
    return false;
#endif
}

bool SandboxModule::graphEditorOpen() const { return m_graphs && m_graphs->editor.isOpen(); }

bool SandboxModule::graphEditorContains(const glm::vec2& point) const { return m_graphs && m_graphs->editor.contains(point); }

std::string SandboxModule::blockOf(const std::string& asset) const {
    for (size_t i = 0; i < m_blocks.size(); ++i)
        if (std::find(m_blocks[i].assets.begin(), m_blocks[i].assets.end(), asset) != m_blocks[i].assets.end()) return m_blocks[i].id;
    return {};
}

std::vector<std::string> SandboxModule::playBlockIds() const {
    std::vector<std::string> ids;
    for (size_t i = 0; i < m_blocks.size(); ++i)
        if (!m_blockAssets[i].empty() && m_blocks[i].kind != kke::PlayBlockKind::Tool) ids.push_back(m_blocks[i].id);
    return ids;
}

void SandboxModule::initGraphs() {
    m_graphs = std::make_unique<PlayGraphs>();
    PlayGraphs& g = *m_graphs;
    for (const kke::PlayBlock& b : m_blocks) g.recipes[b.id] = kke::playBlockRecipe(b.id);
#if KKE_ENABLE_LUA
    g.vm = std::make_unique<kke::ScriptVM>();
    g.world = std::make_unique<PlayGraphs::World>(*this, g);
    kke::bindPlayBlocks(*g.vm, *g.world);
    // ai.* before the node library is made, so the animal recipes' nodes exist.
    kke::ai::bindAi(*g.vm, g.ai, [this](uint32_t id, glm::vec3& at) {
        const Object* o = find(id);
        if (!o) return false;
        at = o->position;
        return true;
    });
    // What just ran, for the editor to light up (kke::CompileOptions::trace).
    g.vm->registerFunction("graph", "ran", [this](lua_State* L) {
        if (m_graphs->ran.size() < 512) m_graphs->ran.emplace_back(m_graphs->vm->currentSource(), int(lua_tointeger(L, 1)));
        return 0;
    });
    g.vm->printSink = [this](const std::string& source, const std::string& text) { kke::log::get(name())->info("{}: {}", source, text); };
    g.library = kke::NodeLibrary::fromApi(g.vm->apiFunctions(), g.vm->apiEvents());
    if (auto* ui = m_app->getModule<kke::UiModule>(); ui && ui->context())
        g.editor.attach(ui->context(), *ui);
    else
        kke::log::get(name())->info("no RmlUi context: the node graph editor (Look) is off");
#endif
    m_graphsDirty = true;
    // KKE_SANDBOX_LOOK=<block id>|level: start with that graph open
    // (screenshots, trying the editor headless).
    if (const char* look = kke::dev::env("KKE_SANDBOX_LOOK"); look && *look && lookAvailable()) {
        if (std::strcmp(look, "level") == 0) openThingGraph(0);
        else openRecipe(look);
    }
}

void SandboxModule::shutdownGraphs() {
    if (!m_graphs) return;
    m_graphs->editor.detach(); // RmlUi goes down after us (modules shut down in reverse)
#if KKE_ENABLE_LUA
    // Unloading takes back what graphs brought out; the level is going anyway.
    if (m_graphs->vm) m_graphs->vm->onUnload = nullptr;
    m_graphs->loaded.clear();
    m_graphs->vm.reset();
    m_graphs->world.reset();
#endif
}

void SandboxModule::queueHit(uint32_t target, const glm::vec3& point, const glm::vec3& push) {
    if (m_graphs) m_graphs->events.push_back({ PlayGraphs::Event::Kind::Hit, target, point, push });
}
void SandboxModule::queueClicked(uint32_t thing, const glm::vec3& point) {
    if (m_graphs) m_graphs->events.push_back({ PlayGraphs::Event::Kind::Clicked, thing, point, glm::vec3(0.0f) });
}
void SandboxModule::queuePlaced(uint32_t thing, const glm::vec3& point) {
    if (m_graphs) m_graphs->events.push_back({ PlayGraphs::Event::Kind::Placed, thing, point, glm::vec3(0.0f) });
}
void SandboxModule::queueFellOver(uint32_t thing) {
    if (m_graphs) m_graphs->events.push_back({ PlayGraphs::Event::Kind::FellOver, thing, glm::vec3(0.0f), glm::vec3(0.0f) });
}
void SandboxModule::queueStoodUp(uint32_t thing) {
    if (m_graphs) m_graphs->events.push_back({ PlayGraphs::Event::Kind::StoodUp, thing, glm::vec3(0.0f), glm::vec3(0.0f) });
}

// ---------------------------------------------------------------- opening graphs

void SandboxModule::openThingGraph(uint32_t id) {
    if (!lookAvailable()) return;
    PlayGraphs& g = *m_graphs;
    closeGraphEditor();
    std::string title = "the level";
    if (const Object* o = find(id)) {
        g.target = PlayGraphs::Target::Thing;
        g.targetThing = id;
        g.editing = o->graph;
        g.editingSource = thingSource(id);
        std::string label = blockOf(o->asset);
        for (const kke::PlayBlock& b : m_blocks)
            if (b.id == label) label = b.label;
        title = (label.empty() ? o->asset : label) + " " + std::to_string(id);
    } else {
        g.target = PlayGraphs::Target::Level;
        g.editing = g.level;
        g.editingSource = kLevelSource;
    }
    if (g.editing.name.empty()) g.editing.name = title;
    g.editor.setBlockChoices(playBlockIds());
    g.editor.open(&g.editing, &g.library, title);
    m_graphsDirty = true; // shows its compile result
#if KKE_ENABLE_LUA
    g.loaded.erase(g.editingSource);
#endif
}

void SandboxModule::openRecipe(const std::string& block) {
    if (!lookAvailable()) return;
    PlayGraphs& g = *m_graphs;
    closeGraphEditor();
    std::string label = block;
    for (const kke::PlayBlock& b : m_blocks)
        if (b.id == block) label = b.label;
    g.target = PlayGraphs::Target::Recipe;
    g.targetRecipe = block;
    g.editing = g.recipes[block];
    if (g.editing.name.empty()) g.editing.name = label;
    g.editingSource = recipeSource(block);
    g.editor.setBlockChoices(playBlockIds());
    g.editor.open(&g.editing, &g.library, "every " + label);
    m_graphsDirty = true;
#if KKE_ENABLE_LUA
    g.loaded.erase(g.editingSource);
#endif
}

void SandboxModule::closeGraphEditor() {
    if (!m_graphs) return;
    PlayGraphs& g = *m_graphs;
    if (g.editor.isOpen()) g.editor.close();
    g.target = PlayGraphs::Target::None;
    g.editingSource.clear();
}

bool SandboxModule::graphEditorKey(SDL_Keycode key) {
    if (!graphEditorOpen()) return false;
    if (key == SDLK_ESCAPE) {
        closeGraphEditor();
        return true;
    }
    // Not while typing a value into a block.
    if ((key == SDLK_DELETE || key == SDLK_BACKSPACE) && !SDL_TextInputActive(m_app->window().handle())) {
        m_graphs->editor.removeSelected();
        return true;
    }
    return false;
}

bool SandboxModule::graphEditorPadButton(uint8_t button, bool down) {
    if (!graphEditorOpen()) return false;
    switch (button) {
    case SDL_GAMEPAD_BUTTON_EAST: // B: done
        if (down) closeGraphEditor();
        return true;
    case SDL_GAMEPAD_BUTTON_WEST: // X: remove what's selected
        if (down) m_graphs->editor.removeSelected();
        return true;
    case SDL_GAMEPAD_BUTTON_NORTH: // Y: everything in view
        if (down) m_graphs->editor.showAll();
        return true;
    default:
        return false; // A presses like a mouse; the shoulders still walk the palette
    }
}

bool SandboxModule::graphEditorPadSticks(float dt, const glm::vec2& rightStick, float zoom) {
    if (!graphEditorOpen() || !graphEditorContains(m_padCursor)) return false;
    // The stick pushes the view the way it points (the graph slides the other way).
    if (rightStick != glm::vec2(0.0f)) m_graphs->editor.pan(-rightStick * (900.0f * dt));
    if (zoom != 0.0f) m_graphs->editor.zoom(-zoom * 6.0f * dt, m_padCursor);
    return true;
}

// ---------------------------------------------------------------- scenes

void SandboxModule::graphsToScene(kke::SceneFile& scene) const {
    if (!m_graphs) return;
    scene.graph = m_graphs->level;
    // Recipes only where they differ from the built-in ones.
    for (const auto& [block, graph] : m_graphs->recipes)
        if (graph.toJson() != kke::playBlockRecipe(block).toJson()) scene.recipes[block] = graph;
}

void SandboxModule::graphsFromScene(const kke::SceneFile& scene) {
    if (!m_graphs) return;
    closeGraphEditor();
    m_graphs->level = scene.graph;
    for (auto& [block, graph] : m_graphs->recipes) {
        auto it = scene.recipes.find(block);
        graph = it != scene.recipes.end() ? it->second : kke::playBlockRecipe(block);
    }
    m_graphs->score = 0.0;
    m_graphs->scored = false;
    m_graphsDirty = true;
}

// ---------------------------------------------------------------- running

void SandboxModule::updateGraphs(float dt) {
    if (!m_graphs) return;
    PlayGraphs& g = *m_graphs;
    g.now += double(dt);

    // The editor: every change goes straight back into the game.
    if (g.editor.isOpen()) {
        const auto& mouse = m_app->window().mouseState();
        const GraphEditor::Result r = g.editor.update(dt, glm::vec2(mouse.x, mouse.y), mouse.leftButtonDown);
        if (r != GraphEditor::Result::None) {
            // Write the copy back to what it belongs to.
            if (g.target == PlayGraphs::Target::Level) g.level = g.editing;
            else if (g.target == PlayGraphs::Target::Recipe) g.recipes[g.targetRecipe] = g.editing;
            else if (Object* o = find(g.targetThing); o && g.target == PlayGraphs::Target::Thing) o->graph = g.editing;
            if (r == GraphEditor::Result::Changed) m_graphsDirty = true;
            if (r == GraphEditor::Result::Closed) closeGraphEditor();
        }
        // Two fingers on the editor move and zoom it, not the world.
        if (m_graphTouch) {
            const kke::TouchGestures::Frame t = m_touches.take();
            if (t.active) {
                g.editor.pan(t.pan);
                if (t.pinch > 1e-3f) g.editor.zoom(std::log(t.pinch) / std::log(1.15f), glm::vec2(mouse.x, mouse.y));
            }
        }
    }
    if (!g.editor.isOpen() || !m_graphTouch) (void)m_touches.take(); // only gestures on the editor are used here

#if KKE_ENABLE_LUA
    if (!g.vm) return;
    kke::ScriptVM& vm = *g.vm;

    // (Re)load what changed: a graph is reloaded only when its Lua does.
    if (m_graphsDirty) {
        m_graphsDirty = false;
        // Copies: loading a graph can bring things out, which moves m_objects.
        struct Want { std::string source; kke::NodeGraph graph; kke::CompileOptions options; };
        std::vector<Want> want;
        kke::CompileOptions level;
        level.source = kLevelSource;
        want.push_back({ kLevelSource, g.level, level });
        for (const auto& [block, graph] : g.recipes) {
            kke::CompileOptions o;
            o.source = recipeSource(block);
            o.block = block;
            for (const kke::PlayBlock& b : m_blocks)
                if (b.id == block) o.toolBlock = b.kind == kke::PlayBlockKind::Tool;
            want.push_back({ o.source, graph, o });
        }
        for (const Object& o : m_objects) {
            if (o.graph.empty() && !(g.target == PlayGraphs::Target::Thing && g.targetThing == o.id)) continue;
            kke::CompileOptions opt;
            opt.source = thingSource(o.id);
            opt.owner = o.id;
            want.push_back({ opt.source, o.graph, opt });
        }
        // Gone: their graphs stop (and take back what they brought out).
        std::vector<std::string> gone;
        for (const auto& [source, l] : g.loaded)
            if (std::none_of(want.begin(), want.end(), [&](const Want& w) { return w.source == source; })) gone.push_back(source);
        for (const std::string& s : gone) {
            vm.unload(s);
            g.loaded.erase(s);
        }
        for (Want& w : want) {
            w.options.trace = "graph.ran";
            kke::CompiledGraph c = kke::compileGraph(w.graph, g.library, w.options);
            const bool editing = w.source == g.editingSource;
            auto it = g.loaded.find(w.source);
            const bool same = it != g.loaded.end() && it->second.compiled.lua == c.lua;
            if (!same) {
                if (w.graph.empty()) vm.unload(w.source);
                else vm.reloadString(c.lua, w.source);
            }
            if (editing && (!same || it == g.loaded.end())) {
                kke::CompileOptions plain = w.options;
                plain.trace.clear();
                g.editor.setCompiled(c, kke::compileGraph(w.graph, g.library, plain).lua);
            }
            g.loaded[w.source].compiled = std::move(c);
        }
    }

    // Events since the last update, then timers.
    std::vector<PlayGraphs::Event> events;
    events.swap(g.events);
    for (const PlayGraphs::Event& e : events) {
        const Object* o = find(e.thing);
        const std::string block = o ? blockOf(o->asset) : std::string();
        switch (e.kind) {
        case PlayGraphs::Event::Kind::Hit: kke::firePlayHit(vm, { e.thing, "bat", e.point, e.push, block }); break;
        case PlayGraphs::Event::Kind::Clicked: kke::firePlayClicked(vm, e.thing, e.point, block); break;
        case PlayGraphs::Event::Kind::Placed: kke::firePlayPlaced(vm, e.thing, e.point, block); break;
        case PlayGraphs::Event::Kind::FellOver: kke::firePlayFellOver(vm, e.thing, block); break;
        case PlayGraphs::Event::Kind::StoodUp: kke::firePlayStoodUp(vm, e.thing, block); break;
        }
    }
    vm.updateTimers(g.now);
    updateAnimals(dt);

    // Things graphs took away.
    std::vector<uint32_t> removing;
    removing.swap(g.removing);
    for (uint32_t id : removing) removeObject(id);

    // Errors: logged, and shown on their block when that graph is open.
    for (const kke::ScriptVM::Error& e : vm.errors()) {
        kke::log::get(name())->warn("{}: {}", e.source, e.message);
        if (e.source == g.editingSource && g.editor.isOpen()) {
            auto it = g.loaded.find(e.source);
            g.editor.setRuntimeError(it != g.loaded.end() ? it->second.compiled.nodeForError(e.message, e.source) : 0, e.message);
        }
    }
    vm.clearErrors();
    for (const auto& [source, node] : g.ran)
        if (source == g.editingSource) g.editor.ran(node);
    g.ran.clear();
#endif
}

const kke::PlayBlock* SandboxModule::blockFor(const std::string& asset) const {
    const std::string id = blockOf(asset);
    for (const kke::PlayBlock& b : m_blocks)
        if (b.id == id) return &b;
    return nullptr;
}

void SandboxModule::animalNoise(const glm::vec3& at, float loudness) {
    if (m_graphs) m_graphs->ai.makeNoise({ at, loudness, 0, 0 });
}

void SandboxModule::updateAnimals(float dt) {
    if (!m_graphs) return;
    PlayGraphs& g = *m_graphs;
    kke::ai::AiWorld& ai = g.ai;
    const bool play = m_mode == Mode::Play;

    // Gone things leave the AI; people are what the animals see.
    std::vector<uint32_t> gone;
    for (const kke::ai::Agent& a : ai.agents())
        if (!find(a.id)) gone.push_back(a.id);
    for (uint32_t id : gone) {
        ai.remove(id);
        g.animPlaying.erase(id);
    }
    for (const Object& o : m_objects)
        if (o.character && !ai.has(o.id)) ai.addActor(o.id, "farmer", o.position);

    // In Build (or while carried) the thing leads: the AI takes it from where it is.
    for (const Object& o : m_objects) {
        const kke::ai::Agent* a = ai.agent(o.id);
        if (a && (a->actor || !play || o.id == m_movingId || g.animPlaying.count(o.id) == 0))
            ai.setTransform(o.id, o.position, glm::vec3(0.0f), o.yawDegrees);
    }
    if (!play) return;
    ai.update(dt);
    const std::vector<kke::ai::AiEvent> events = ai.takeEvents();
#if KKE_ENABLE_LUA
    if (g.vm) kke::ai::fireAiEvents(*g.vm, events); // "Spotted", "Scared", ... for the graphs
#endif

    // Then the animal follows its mind: position, facing and the clip for what it's doing.
    for (Object& o : m_objects) {
        const kke::ai::Agent* a = ai.agent(o.id);
        if (!a || a->actor || o.id == m_movingId) continue;
        o.position = a->position;
        o.yawDegrees = a->yaw;
        m_models->setTransform(o.instance, objectTransform(o));
        std::string& playing = g.animPlaying[o.id];
        if (a->anim == playing) continue;
        playing = a->anim;
        if (const kke::ModelData* d = m_models->model(o.model)) {
            const kke::ai::ClipChoice c = kke::ai::clipForAnim(*d, a->anim);
            m_models->playAnimation(o.instance, c.clip, a->anim != "attack", c.speed);
        }
    }
}

// What graphs say, and the score once there is one: big, at the top
// (the Play palette's document, RmlUi).
void SandboxModule::graphUi() {
    std::string said, score;
    if (m_graphs && m_mode == Mode::Play) {
        const PlayGraphs& g = *m_graphs;
        if (g.now < g.sayUntil) said = g.said;
        if (g.scored) {
            char text[48];
            std::snprintf(text, sizeof(text), "Score %g", g.score);
            score = text;
        }
    }
    m_palette.setWords(said, score);
}

} // namespace kke_sandbox
