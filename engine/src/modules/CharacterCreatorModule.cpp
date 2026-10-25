#include "kke/modules/CharacterCreatorModule.h"

#include "kke/Application.h"
#include "kke/AssetCatalog.h"
#include "kke/DevTools.h"
#include "kke/Log.h"
#include "kke/Outfit.h"
#include "kke/modules/OrbitCameraModule.h"
#include "kke/modules/UiModule.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>
#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace kke {

namespace {

constexpr float kPi = 3.14159265f;

// Which MakeHuman modifier groups each tab shows, in order. Groups no tab
// lists (genitals) are left out of the screen.
struct TabDef {
    const char* id;
    const char* label;
    std::vector<const char*> groups;
};
const std::vector<TabDef>& tabDefs() {
    static const std::vector<TabDef> tabs = {
        { "body", "Body type", { "macrodetails", "macrodetails-universal", "macrodetails-height", "macrodetails-proportions" } },
        { "shape", "Shape", { "bodyshapes", "measure" } },
        { "torso", "Torso", { "torso", "breast", "stomach", "hip", "buttocks", "pelvis" } },
        { "limbs", "Arms & legs", { "armslegs" } },
        { "head", "Head & neck", { "head", "neck", "forehead", "eyebrows" } },
        { "eyes", "Eyes", { "eyes" } },
        { "nose", "Nose", { "nose" } },
        { "mouth", "Mouth & jaw", { "mouth", "chin", "cheek" } },
        { "ears", "Ears", { "ears" } },
        { "skin", "Skin & eyes", {} },
    };
    return tabs;
}

std::string capitalise(std::string s) {
    if (!s.empty()) s[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
    return s;
}

// "nose/nose-hump-decr|incr" -> "Hump"; "eyes/r-eye-height1-min|max" ->
// "Eye height 1" (side ", right" unless mirrored); macros by name. With
// groupWord the group leads ("Neck size depth" in the head tab), and
// two-sided sliders with telling ends say them ("Move (down / up)").
std::string labelOf(const HumanModifier& m, bool mirrored, bool groupWord) {
    if (!m.macro.empty()) {
        static const std::map<std::string, std::string> macro = {
            { "BodyProportions", "Proportions" }, { "BreastSize", "Breast size" }, { "BreastFirmness", "Breast firmness" },
        };
        auto it = macro.find(m.macro);
        return it != macro.end() ? it->second : m.macro;
    }
    std::string n = m.name.substr(m.name.find('/') + 1);
    std::string ends;
    if (auto bar = n.find('|'); bar != std::string::npos) {
        const size_t dash = n.rfind('-', bar);
        const std::string lo = n.substr(dash + 1, bar - dash - 1), hi = n.substr(bar + 1);
        static const char* plain[] = { "decr|incr", "min|max", "less|more", "small|big" };
        if (std::find(std::begin(plain), std::end(plain), lo + "|" + hi) == std::end(plain)) ends = " (" + lo + " / " + hi + ")";
        n = n.substr(0, dash);
    }
    std::string side;
    if (n.rfind("r-", 0) == 0 || n.rfind("l-", 0) == 0) {
        side = n[0] == 'r' ? "right" : "left";
        n = n.substr(2);
    }
    // Drop the group's own word in front ("nose-hump" in the nose tab).
    for (std::string group : { m.group + "-", m.group.back() == 's' ? m.group.substr(0, m.group.size() - 1) + "-" : std::string() })
        if (!group.empty() && n.rfind(group, 0) == 0 && n.size() > group.size()) n = n.substr(group.size());
    if (n.rfind("measure-", 0) == 0) n = n.substr(8);
    if (n.rfind("bodyshapes-elvs-", 0) == 0) n = n.substr(16);
    static const std::pair<const char*, const char*> words[] = {
        { "circ", "circumference" }, { "dist", "distance" }, { "trans", "move" }, { "horiz", "width" },
        { "vert", "height" }, { "incr", "" }, { "decr", "" }, { "fem", "" }, { "scale", "size" },
    };
    std::string out;
    std::stringstream in(n);
    for (std::string w; std::getline(in, w, '-');) {
        for (auto [from, to] : words)
            if (w == from) w = to;
        if (w.empty()) continue;
        // "height1" -> "height 1"
        if (w.size() > 1 && std::isdigit(static_cast<unsigned char>(w.back())) && !std::isdigit(static_cast<unsigned char>(w[w.size() - 2])))
            w.insert(w.size() - 1, " ");
        if (!out.empty()) out += ' ';
        out += w;
    }
    if (out.empty()) out = n;
    if (groupWord && out.rfind(m.group, 0) != 0) out = m.group + " " + out;
    // "Compression (compress / uncompress)": the ends add nothing.
    if (!ends.empty() && out.find(ends.substr(2, ends.find(' ', 2) - 2)) == std::string::npos) out += ends;
    if (!side.empty() && !mirrored) out += ", " + side;
    return capitalise(out);
}

// The other side's slider ("eyes/r-eye-..." <-> "eyes/l-eye-..."), "" if none.
std::string mirrorOf(const HumanPack& pack, const std::string& name) {
    const size_t slash = name.find('/');
    if (slash == std::string::npos || name.size() < slash + 3 || name[slash + 2] != '-') return {};
    const char s = name[slash + 1];
    if (s != 'l' && s != 'r') return {};
    std::string other = name;
    other[slash + 1] = s == 'l' ? 'r' : 'l';
    return pack.findModifier(other) >= 0 ? other : std::string();
}

std::string cssColour(const glm::vec3& c) {
    char buf[8];
    auto b = [](float v) { return static_cast<int>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
    std::snprintf(buf, sizeof buf, "#%02x%02x%02x", b(c.r), b(c.g), b(c.b));
    return buf;
}

std::string uiRoot() {
    if (const char* env = dev::env("KKE_CREATOR_UI")) {
        std::string root = env;
        if (!root.empty() && root.back() != '/') root += '/';
        return root;
    }
    const char* base = SDL_GetBasePath();
    const std::string dir = findAssetFolder("assets/ui/character_creator", {}, base ? base : "");
    return dir.empty() ? std::string() : dir + "/";
}

} // namespace

CharacterCreatorModule::CharacterCreatorModule(std::string saveName)
    : m_saveName(std::move(saveName)), m_rng(static_cast<uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count())) {}

CharacterCreatorModule::~CharacterCreatorModule() = default;

void CharacterCreatorModule::init(Application& app) {
    m_app = &app;
    m_ui = app.getModule<UiModule>();
    m_models = app.getModule<ModelModule>();
    m_orbit = app.getModule<OrbitCameraModule>();
    if (!m_ui || !m_ui->context() || !m_models)
        throw std::runtime_error("CharacterCreatorModule needs a UiModule and a ModelModule added before it");

    const char* base = SDL_GetBasePath();
    std::vector<std::string> searched;
    const std::string dir = findAssetFolder("assets/humans", { "KKE_HUMANS_DIR" }, base ? base : "", &searched);
    if (dir.empty()) throw std::runtime_error("CharacterCreatorModule: assets/humans/makehuman.kkhpack not found (docs/HUMANS.md)");
    m_pack = std::make_unique<HumanPack>(HumanPack::load(dir + "/makehuman.kkhpack"));
    m_human = std::make_unique<Human>(*m_pack);
    log::get(name())->info("human pack: {} targets, {} sliders, {}", m_pack->targets().size(), m_pack->modifiers().size(),
                           m_pack->licence());

    for (const NamedColour& c : skinTones())
        m_skinSwatches.push_back({ c.name, cssColour(c.rgb), c.rgb.r, c.rgb.g, c.rgb.b });
    static const NamedColour eyes[] = {
        { "Dark brown", { 0.20f, 0.11f, 0.05f } }, { "Brown", { 0.36f, 0.22f, 0.10f } }, { "Hazel", { 0.48f, 0.38f, 0.17f } },
        { "Amber", { 0.65f, 0.43f, 0.12f } },      { "Green", { 0.26f, 0.45f, 0.24f } }, { "Grey", { 0.50f, 0.55f, 0.58f } },
        { "Blue", { 0.24f, 0.42f, 0.66f } },       { "Light blue", { 0.48f, 0.66f, 0.84f } },
    };
    for (const NamedColour& c : eyes) m_eyeSwatches.push_back({ c.name, cssColour(c.rgb), c.rgb.r, c.rgb.g, c.rgb.b });
    for (const TabDef& t : tabDefs()) m_tabs.push_back({ t.id, t.label });

    buildModel();
    loadDocument();
    spawnBody();
    // A save from last time, if there is one.
    if (std::filesystem::exists(savePath())) load();
    fillSliders();
    setView("body");
    if (const char* tab = dev::env("KKE_CREATOR_TAB")) {
        m_tab = tab;
        fillSliders();
    }
    if (const char* view = dev::env("KKE_CREATOR_VIEW")) setView(view);
    open();
}

void CharacterCreatorModule::buildModel() {
    Rml::DataModelConstructor c = m_ui->context()->CreateDataModel("creator");
    if (!c) throw std::runtime_error("CharacterCreatorModule: the data model 'creator' exists already");
    if (auto t = c.RegisterStruct<TabView>()) {
        t.RegisterMember("id", &TabView::id);
        t.RegisterMember("label", &TabView::label);
    }
    c.RegisterArray<std::vector<TabView>>();
    if (auto s = c.RegisterStruct<SliderView>()) {
        s.RegisterMember("name", &SliderView::name);
        s.RegisterMember("label", &SliderView::label);
        s.RegisterMember("value", &SliderView::value);
        s.RegisterMember("min", &SliderView::min);
        s.RegisterMember("max", &SliderView::max);
    }
    c.RegisterArray<std::vector<SliderView>>();
    if (auto w = c.RegisterStruct<SwatchView>()) {
        w.RegisterMember("name", &SwatchView::name);
        w.RegisterMember("css", &SwatchView::css);
    }
    c.RegisterArray<std::vector<SwatchView>>();
    c.Bind("tabs", &m_tabs);
    c.Bind("tab", &m_tab);
    c.Bind("sliders", &m_sliders);
    c.Bind("skin_swatches", &m_skinSwatches);
    c.Bind("eye_swatches", &m_eyeSwatches);
    c.Bind("mirror", &m_mirror);
    c.Bind("turntable", &m_turntable);
    c.Bind("code", &m_codeShort);
    c.Bind("status", &m_status);
    c.Bind("view", &m_view);
    c.BindEventCallback("set_tab", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args) {
        if (!args.empty()) m_tab = args[0].Get<Rml::String>();
        fillSliders();
    });
    c.BindEventCallback("toggle_mirror", [this](Rml::DataModelHandle h, Rml::Event&, const Rml::VariantList&) {
        m_mirror = !m_mirror;
        h.DirtyVariable("mirror");
        fillSliders();
    });
    c.BindEventCallback("toggle_turntable", [this](Rml::DataModelHandle h, Rml::Event&, const Rml::VariantList&) {
        m_turntable = !m_turntable;
        if (m_orbit) m_orbit->setAutoOrbit(m_turntable, 25.0f);
        h.DirtyVariable("turntable");
    });
    c.BindEventCallback("view", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args) {
        if (!args.empty()) setView(args[0].Get<Rml::String>());
    });
    c.BindEventCallback("skin", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args) {
        const int i = args.empty() ? -1 : args[0].Get<int>();
        if (i < 0 || i >= static_cast<int>(m_skinSwatches.size())) return;
        m_look.skin = { m_skinSwatches[i].r, m_skinSwatches[i].g, m_skinSwatches[i].b };
        applyColours();
    });
    c.BindEventCallback("eyes", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args) {
        const int i = args.empty() ? -1 : args[0].Get<int>();
        if (i < 0 || i >= static_cast<int>(m_eyeSwatches.size())) return;
        m_look.eyes = { m_eyeSwatches[i].r, m_eyeSwatches[i].g, m_eyeSwatches[i].b };
        applyColours();
    });
    c.BindEventCallback("reset_slider", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args) {
        const int i = args.empty() ? -1 : args[0].Get<int>();
        if (i < 0 || i >= static_cast<int>(m_sliders.size())) return;
        pushUndo();
        const int m = m_pack->findModifier(m_sliders[i].name);
        const float def = m >= 0 ? m_pack->modifiers()[m].defaultValue : 0.0f;
        m_human->set(m_sliders[i].name, def);
        if (!m_sliders[i].pair.empty()) m_human->set(m_sliders[i].pair, def);
        m_shapeDirty = true;
        fillSliders();
    });
    c.BindEventCallback("reset_tab", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
        pushUndo();
        for (const SliderView& s : m_sliders) {
            const int m = m_pack->findModifier(s.name);
            const float def = m >= 0 ? m_pack->modifiers()[m].defaultValue : 0.0f;
            m_human->set(s.name, def);
            if (!s.pair.empty()) m_human->set(s.pair, def);
        }
        m_shapeDirty = true;
        fillSliders();
    });
    c.BindEventCallback("randomise", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args) {
        randomise(!args.empty() && args[0].Get<Rml::String>() == "all");
    });
    c.BindEventCallback("undo", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
        if (m_undo.empty()) return setStatus("Nothing to undo");
        m_redo.push_back(m_human->values());
        restore(m_undo.back());
        m_undo.pop_back();
    });
    c.BindEventCallback("redo", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
        if (m_redo.empty()) return setStatus("Nothing to redo");
        m_undo.push_back(m_human->values());
        restore(m_redo.back());
        m_redo.pop_back();
    });
    c.BindEventCallback("save", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) { save(); });
    c.BindEventCallback("load", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) { load(); });
    c.BindEventCallback("copy_code", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
        SDL_SetClipboardText(shareCode().c_str());
        setStatus("Share code copied");
    });
    c.BindEventCallback("paste_code", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
        char* text = SDL_GetClipboardText();
        const std::string code = text ? text : "";
        SDL_free(text);
        HumanLook look;
        std::string error;
        if (!fromShareCode(code, *m_pack, look, &error)) return setStatus("Can't use that code: " + error);
        pushUndo();
        setLook(look);
        setStatus("Character loaded from the code");
    });
    c.BindEventCallback("done", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
        save();
        if (m_onDone) m_onDone(look());
    });
    m_handle = c.GetModelHandle();
}

void CharacterCreatorModule::loadDocument() {
    const std::string root = uiRoot();
    m_doc = m_ui->context()->LoadDocument(root + "creator.rml");
    if (!m_doc) throw std::runtime_error("CharacterCreatorModule: can't load " + root + "creator.rml (kke_use_character_creator in CMake copies it)");
}

void CharacterCreatorModule::spawnBody() {
    Human::BakeOptions body;
    body.groups = { "body" };
    body.skeleton = false; // edited as plain vertices; bakeSkinned() makes the animated one
    m_bodyBake = m_human->bake(body);
    m_bodyBake.model.materials[0].baseColor = glm::vec3(1.0f); // the tint is the skin colour
    m_bodyBake.model.materials[0].roughness = 0.55f;
    Human::BakeOptions parts;
    parts.groups = { "helper-l-eye", "helper-r-eye", "helper-upper-teeth", "helper-lower-teeth", "helper-tongue" };
    parts.skeleton = false;
    m_partsBake = m_human->bake(parts);
    const ModelModule::ModelId bodyModel = m_models->add(m_bodyBake.model, "character_creator/body");
    const ModelModule::ModelId partsModel = m_models->add(m_partsBake.model, "character_creator/parts");
    m_body = m_models->spawn(bodyModel);
    m_parts = m_models->spawn(partsModel);
    applyColours();
    m_shapeDirty = true;
}

void CharacterCreatorModule::applyColours() {
    if (m_body) m_models->setTint(m_body, m_look.skin);
}

void CharacterCreatorModule::reshape() {
    for (Human::Bake* bake : { &m_bodyBake, &m_partsBake }) m_human->reshape(*bake);
    for (auto [bake, inst] : { std::pair{ &m_bodyBake, m_body }, std::pair{ &m_partsBake, m_parts } }) {
        std::vector<std::vector<glm::vec3>> pos, nrm;
        for (const ModelMesh& mesh : bake->model.meshes) {
            pos.emplace_back();
            nrm.emplace_back();
            for (const ModelVertex& v : mesh.vertices) {
                pos.back().push_back(v.position);
                nrm.back().push_back(v.normal);
            }
        }
        m_models->setDeformedVertices(inst, pos, nrm, /*modelSpace=*/true);
    }
    m_code = shareCode();
    m_codeShort = m_code.size() > 40 ? m_code.substr(0, 36) + "... (" + std::to_string(m_code.size()) + " letters)" : m_code;
    m_handle.DirtyVariable("code");
}

void CharacterCreatorModule::fillSliders() {
    m_sliders.clear();
    const TabDef* tab = nullptr;
    for (const TabDef& t : tabDefs())
        if (m_tab == t.id) tab = &t;
    if (!tab) {
        m_tab = "body";
        tab = &tabDefs().front();
    }
    for (const char* group : tab->groups)
        for (const HumanModifier& m : m_pack->modifiers()) {
            if (m.group != group) continue;
            const std::string pair = m_mirror ? mirrorOf(*m_pack, m.name) : std::string();
            // Mirrored: one slider for both sides, the right one standing in.
            if (!pair.empty() && m.name[m.name.find('/') + 1] == 'l') continue;
            SliderView s;
            s.name = m.name;
            s.pair = pair;
            s.label = labelOf(m, !pair.empty(), group != tab->groups.front());
            s.min = m.min;
            s.max = m.max;
            s.value = s.shown = m_human->get(m.name);
            m_sliders.push_back(std::move(s));
        }
    m_handle.DirtyVariable("sliders");
    m_handle.DirtyVariable("tab");
}

void CharacterCreatorModule::pushUndo() {
    m_undo.push_back(m_human->values());
    if (m_undo.size() > 100) m_undo.erase(m_undo.begin());
    m_redo.clear();
}

void CharacterCreatorModule::restore(const std::map<std::string, float>& values) {
    m_human->setAll(values);
    m_shapeDirty = true;
    fillSliders();
}

void CharacterCreatorModule::randomise(bool everything) {
    pushUndo();
    std::normal_distribution<float> n(0.0f, 1.0f);
    // Real people are near the middle of almost every slider: a third of
    // the detail sliders move, a little (sd 6% of the range, never past
    // 2.5 sd), and body type stays inside its usual band. Many sliders
    // moved far at once add up to a face no one has (first screenshots).
    std::uniform_real_distribution<float> u(0.0f, 1.0f);
    auto pick = [&](const HumanModifier& m) {
        if (!m.macro.empty()) return std::clamp(0.5f + n(m_rng) * 0.15f, 0.15f, 0.85f);
        if (u(m_rng) > 0.35f) return m.defaultValue;
        const float sd = (m.max - m.min) * 0.06f;
        return std::clamp(m.defaultValue + std::clamp(n(m_rng), -2.5f, 2.5f) * sd, m.min, m.max);
    };
    std::vector<std::string> groups;
    for (const TabDef& t : tabDefs())
        if (everything || m_tab == t.id)
            for (const char* g : t.groups) groups.emplace_back(g);
    for (const HumanModifier& m : m_pack->modifiers()) {
        if (std::find(groups.begin(), groups.end(), m.group) == groups.end()) continue;
        if (m.macro == "Age" || m.macro == "African" || m.macro == "Asian" || m.macro == "Caucasian") continue; // set below
        // Whole-body presets and measurements stay put: they fight the rest.
        if (m.group == "bodyshapes") continue;
        const float v = m.macro == "Gender" ? (u(m_rng) < 0.5f ? 0.1f : 0.9f) : pick(m);
        m_human->set(m.name, v);
        if (const std::string pair = mirrorOf(*m_pack, m.name); !pair.empty() && m.name[m.name.find('/') + 1] == 'r')
            m_human->set(pair, v == m.defaultValue ? v : v + n(m_rng) * 0.02f); // a face is never quite symmetric
    }
    if (everything || m_tab == "body") {
        m_human->set("macrodetails/Age", std::uniform_real_distribution<float>(0.42f, 0.8f)(m_rng)); // 18 to 70 or so
        std::gamma_distribution<float> g(0.6f, 1.0f);
        const float a = g(m_rng), b = g(m_rng), c = g(m_rng);
        m_human->setAll([&] {
            auto v = m_human->values();
            v["macrodetails/African"] = a / (a + b + c);
            v["macrodetails/Asian"] = b / (a + b + c);
            v["macrodetails/Caucasian"] = c / (a + b + c);
            return v;
        }());
        if (everything) {
            const auto& tones = skinTones();
            const NamedColour& t = tones[std::uniform_int_distribution<size_t>(0, tones.size() - 1)(m_rng)];
            m_look.skin = t.rgb;
            applyColours();
        }
    }
    m_shapeDirty = true;
    fillSliders();
}

void CharacterCreatorModule::update(const UpdateContext& ctx) {
    if (!m_open) return;
    m_sinceEdit += ctx.dt;
    // A dragged slider writes its value into m_sliders; hand it on.
    bool dragged = false;
    for (SliderView& s : m_sliders) {
        // The range input snaps a value it is given to its 0.01 step and
        // writes that back: not a drag (it would round a random person).
        if (std::abs(s.value - s.shown) < 0.006f) {
            s.value = s.shown;
            continue;
        }
        dragged = true;
        if (m_sinceEdit > 0.6f) pushUndo(); // one undo step per drag
        m_sinceEdit = 0.0f;
        m_human->set(s.name, s.value);
        if (!s.pair.empty()) m_human->set(s.pair, s.value);
        s.shown = s.value;
        m_shapeDirty = true;
        // The ancestry three move together.
        if (s.name == "macrodetails/African" || s.name == "macrodetails/Asian" || s.name == "macrodetails/Caucasian") {
            for (SliderView& o : m_sliders) o.value = o.shown = m_human->get(o.name);
            m_handle.DirtyVariable("sliders");
        }
    }
    if (m_shapeDirty) {
        m_shapeDirty = false;
        reshape();
        // A new person (random, undo, load) may be a head taller: frame it
        // again. Not while a slider is dragged, so the camera holds still.
        if (!dragged) setView(m_view);
    }
    // CI (KKE_CREATOR_SELFTEST=1): a random person, through a share code
    // and back, must come out the same and keep standing on the floor.
    if (!m_selfTested && dev::env("KKE_CREATOR_SELFTEST")) {
        m_selfTested = true;
        randomise(true);
        reshape();
        const std::string code = shareCode();
        HumanLook back;
        const bool parsed = fromShareCode(code, *m_pack, back);
        if (parsed) setLook(back);
        reshape();
        const bool same = parsed && shareCode() == code;
        const bool floor = std::abs(m_bodyBake.model.boundsMin.y) < 1e-3f;
        log::get(name())->info("creator self-test {} (code {}, floor {})", same && floor ? "passed" : "FAILED",
                               same ? "round-trips" : "differs", floor ? "ok" : "off");
    }
    if (!m_status.empty() && (m_statusTime -= ctx.dt) <= 0.0f) {
        m_status.clear();
        m_handle.DirtyVariable("status");
    }
}

void CharacterCreatorModule::setStatus(const std::string& text) {
    m_status = text;
    m_statusTime = 4.0f;
    m_handle.DirtyVariable("status");
    log::get(name())->info("{}", text);
}

void CharacterCreatorModule::setView(const std::string& view) {
    m_view = view;
    m_handle.DirtyVariable("view");
    if (!m_orbit) return;
    // The figure faces +Z; the camera sits at target - forward * distance.
    const std::vector<glm::vec3> shaped = m_human->shapedPositions();
    const glm::vec3 head = m_human->jointPosition("head____head", shaped);
    const float h = m_bodyBake.model.boundsMax.y - m_bodyBake.model.boundsMin.y;
    // The panel covers the right of the screen: aim a little right of the
    // figure so it stands in the free part.
    if (view == "face") {
        m_orbit->setView(head + glm::vec3(0.07f, 0.03f, 0.0f), 0.75f, -0.05f, kPi);
    } else if (view == "profile") {
        m_orbit->setView(head + glm::vec3(0.0f, 0.03f, -0.07f), 0.75f, -0.05f, -kPi / 2.0f);
    } else if (view == "back") {
        m_orbit->setView(glm::vec3(-0.25f * h, 0.52f * h, 0.0f), 2.0f * h, -0.08f, 0.0f);
    } else {
        m_view = "body";
        m_orbit->setView(glm::vec3(0.25f * h, 0.52f * h, 0.0f), 2.0f * h, -0.08f, kPi);
    }
}

void CharacterCreatorModule::open() {
    m_open = true;
    if (m_doc) m_doc->Show();
    if (m_body) m_models->setVisible(m_body, true);
    if (m_parts) m_models->setVisible(m_parts, true);
}

void CharacterCreatorModule::close() {
    m_open = false;
    if (m_doc) m_doc->Hide();
    if (m_body) m_models->setVisible(m_body, false);
    if (m_parts) m_models->setVisible(m_parts, false);
}

HumanLook CharacterCreatorModule::look() const {
    HumanLook l = m_look;
    takeShape(*m_human, l);
    return quantised(l, *m_pack);
}

std::string CharacterCreatorModule::shareCode() const {
    HumanLook l = m_look;
    takeShape(*m_human, l);
    return kke::shareCode(l, *m_pack);
}

void CharacterCreatorModule::setLook(const HumanLook& look) {
    m_look = look;
    applyLook(*m_human, look);
    applyColours();
    m_shapeDirty = true;
    fillSliders();
}

ModelData CharacterCreatorModule::bakeSkinned() const {
    Human h(*m_pack);
    applyLook(h, look());
    Human::Bake b = h.bake();
    for (ModelMaterial& m : b.model.materials)
        if (m.name == "Human_body") m.baseColor = look().skin;
    return std::move(b.model);
}

std::string CharacterCreatorModule::savePath() const {
    char* pref = SDL_GetPrefPath("KreativeKompas", "characters");
    if (!pref) return {};
    std::string path = std::string(pref) + m_saveName + ".kkhuman";
    SDL_free(pref);
    return path;
}

void CharacterCreatorModule::save() {
    const std::string path = savePath();
    if (path.empty()) return setStatus("This device has no save folder");
    std::ofstream out(path, std::ios::binary);
    out << lookToJson(look());
    setStatus(out ? "Saved" : "Couldn't save to " + path);
}

void CharacterCreatorModule::load() {
    const std::string path = savePath();
    std::ifstream in(path, std::ios::binary);
    if (!in) return setStatus("No saved character yet");
    std::stringstream text;
    text << in.rdbuf();
    HumanLook l;
    std::string error;
    if (!lookFromJson(text.str(), l, &error)) return setStatus("The save can't be read: " + error);
    setLook(l);
    setStatus("Loaded");
}

void CharacterCreatorModule::shutdown() {
    if (m_doc) {
        m_doc->Close();
        m_doc = nullptr;
    }
    if (m_ui && m_ui->context()) m_ui->context()->RemoveDataModel("creator");
}

} // namespace kke
