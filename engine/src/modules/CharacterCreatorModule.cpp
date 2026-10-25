#include "kke/modules/CharacterCreatorModule.h"

#include "kke/Application.h"
#include "kke/AssetCatalog.h"
#include "kke/Cloth.h"
#include "kke/DevTools.h"
#include "kke/HairRenderer.h"
#include "kke/HumanClothes.h"
#include "kke/HumanEyes.h"
#include "kke/HumanHair.h"
#include "kke/Log.h"
#include "kke/Outfit.h"
#include "kke/RigidWorld.h"
#include "kke/SphereImpostors.h"
#include "kke/modules/OrbitCameraModule.h"
#include "kke/modules/UiModule.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

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
#include <set>
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
        { "hair", "Hair", {} },
        { "clothes", "Clothes", {} },
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

// Natural hair colours, root and tip (sRGB): the tips a little lighter,
// as sun and age leave them.
struct HairColour {
    const char* name;
    glm::vec3 root, tip;
};
const std::vector<HairColour>& hairColours() {
    static const std::vector<HairColour> c = {
        { "Black", { 0.03f, 0.025f, 0.02f }, { 0.07f, 0.055f, 0.045f } },
        { "Dark brown", { 0.10f, 0.06f, 0.04f }, { 0.20f, 0.13f, 0.08f } },
        { "Brown", { 0.22f, 0.13f, 0.07f }, { 0.36f, 0.24f, 0.13f } },
        { "Light brown", { 0.38f, 0.25f, 0.14f }, { 0.55f, 0.40f, 0.24f } },
        { "Auburn", { 0.32f, 0.10f, 0.05f }, { 0.48f, 0.18f, 0.08f } },
        { "Ginger", { 0.55f, 0.22f, 0.08f }, { 0.75f, 0.38f, 0.15f } },
        { "Dark blonde", { 0.45f, 0.33f, 0.18f }, { 0.66f, 0.52f, 0.32f } },
        { "Blonde", { 0.70f, 0.56f, 0.33f }, { 0.88f, 0.78f, 0.55f } },
        { "Platinum", { 0.85f, 0.80f, 0.70f }, { 0.95f, 0.93f, 0.86f } },
        { "Grey", { 0.45f, 0.43f, 0.41f }, { 0.68f, 0.67f, 0.65f } },
        { "White", { 0.82f, 0.81f, 0.79f }, { 0.94f, 0.94f, 0.93f } },
    };
    return c;
}

// The hairstyles on the Hair tab: kke::hairstyleOnHead names and types.
const std::vector<std::pair<const char*, const char*>>& hairChoices() {
    static const std::vector<std::pair<const char*, const char*>> c = {
        { "", "None" },
        { "short", "Short" },
        { "straight", "Straight" },
        { "long", "Long" },
        { "wavy", "Wavy" },
        { "curly", "Curly" },
        { "afro", "Afro" },
        { "puff", "Puff" },
        { "high-top fade", "High-top fade" },
        { "twist-out", "Twist-out" },
        { "bantu knots", "Bantu knots" },
        { "box braids", "Box braids" },
        { "cornrows", "Cornrows" },
        { "locs", "Locs" },
        { "two-strand twists", "Twists" },
    };
    return c;
}

constexpr int kHairGuides = 160;

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

// A garment on the figure. Fitted (a T-shirt, shoes): triangles of the
// body suit's bake, drawn as a model and moved with it on every slider.
// Cloth (a skirt): a sheet hanging in m_world, drawn every frame from
// where the solver has it.
struct CharacterCreatorModule::WornPiece {
    std::string id;
    glm::vec3 colour{ 0.9f };
    float offset = 0.0f;
    // Fitted.
    ModelModule::ModelId model = 0;
    ModelModule::InstanceId instance = 0;
    std::vector<uint32_t> from; // piece vertex -> body suit bake vertex
    ModelData data;
    // Cloth.
    RigidWorld::ClothId cloth = 0;
    std::unique_ptr<DynamicMeshRenderer> drawn;
    Fabric fabric;
    std::vector<uint32_t> indices;
    std::vector<glm::vec3> positions, normals;
    std::vector<Vertex> vertices;
};

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
    for (const HairColour& c : hairColours())
        m_hairSwatches.push_back({ c.name, cssColour(0.5f * (c.root + c.tip)), c.root.r, c.root.g, c.root.b });
    for (const auto& [id, label] : hairChoices()) m_hairChoices.push_back({ id, label });
    for (const NamedColour& c : clothColours()) m_clothSwatches.push_back({ c.name, cssColour(c.rgb), c.rgb.r, c.rgb.g, c.rgb.b });
    m_look.hair = "short"; // a new person; a save replaces it
    m_look.clothes = { { "t-shirt", { 0.92f, 0.92f, 0.9f } }, { "shorts", { 0.16f, 0.18f, 0.24f } },
                       { "socks", { 0.92f, 0.92f, 0.9f } },   { "shoes", { 0.92f, 0.92f, 0.9f } } };

    buildModel();
    loadDocument();
    spawnBody();
    buildWorld();
    // A save from last time, if there is one.
    if (std::filesystem::exists(savePath())) load();
    m_hairDirty = true;
    m_clothesDirty = true;
    fillSliders();
    setView("body");
    if (const char* tab = dev::env("KKE_CREATOR_TAB")) {
        m_tab = tab;
        fillSliders();
    }
    if (const char* view = dev::env("KKE_CREATOR_VIEW")) setView(view);
    if (const char* hair = dev::env("KKE_CREATOR_HAIR")) { // screenshots and CI: "long", "afro", "" = none
        m_look.hair = hair;
        fillSliders();
    }
    if (const char* clothes = dev::env("KKE_CREATOR_CLOTHES")) { // "t-shirt,tennis skirt,shoes", "" = none
        m_look.clothes.clear();
        std::stringstream list(clothes);
        for (std::string id; std::getline(list, id, ',');)
            if (!id.empty()) wear(id);
    }
    if (dev::env("KKE_CREATOR_TURN")) m_turntable = true;
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
    if (auto h = c.RegisterStruct<HairChoice>()) {
        h.RegisterMember("id", &HairChoice::id);
        h.RegisterMember("label", &HairChoice::label);
    }
    c.RegisterArray<std::vector<HairChoice>>();
    c.Bind("hair_choices", &m_hairChoices);
    c.Bind("hair_swatches", &m_hairSwatches);
    c.Bind("hair_style", &m_hairStyle);
    c.Bind("hair_length", &m_hairLength);
    if (auto g = c.RegisterStruct<GarmentView>()) {
        g.RegisterMember("id", &GarmentView::id);
        g.RegisterMember("label", &GarmentView::label);
        g.RegisterMember("slot", &GarmentView::slot);
        g.RegisterMember("worn", &GarmentView::worn);
    }
    c.RegisterArray<std::vector<GarmentView>>();
    c.Bind("garments", &m_garments);
    c.Bind("cloth_swatches", &m_clothSwatches);
    c.Bind("cloth_slot", &m_clothSlot);
    c.Bind("cloth_slot_label", &m_clothSlotLabel);
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
        m_turntable = !m_turntable; // the figure turns (fixedUpdate), so its hair swings
        h.DirtyVariable("turntable");
    });
    c.BindEventCallback("view", [this](Rml::DataModelHandle h, Rml::Event&, const Rml::VariantList& args) {
        m_turntable = false;
        m_yaw = 0.0f; // face the camera again
        h.DirtyVariable("turntable");
        if (!args.empty()) setView(args[0].Get<Rml::String>());
    });
    c.BindEventCallback("hair_style", [this](Rml::DataModelHandle h, Rml::Event&, const Rml::VariantList& args) {
        m_look.hair = args.empty() ? std::string() : args[0].Get<Rml::String>();
        m_hairStyle = m_look.hair;
        h.DirtyVariable("hair_style");
        buildHair();
    });
    c.BindEventCallback("hair_colour", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args) {
        const int i = args.empty() ? -1 : args[0].Get<int>();
        if (i < 0 || i >= static_cast<int>(hairColours().size())) return;
        m_look.hairRoot = hairColours()[static_cast<size_t>(i)].root;
        m_look.hairTip = hairColours()[static_cast<size_t>(i)].tip;
        buildHair();
    });
    c.BindEventCallback("wear", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args) {
        if (!args.empty()) wear(args[0].Get<Rml::String>());
    });
    c.BindEventCallback("cloth_slot", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args) {
        if (!args.empty()) m_clothSlot = args[0].Get<Rml::String>();
        fillClothes();
    });
    c.BindEventCallback("cloth_colour", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args) {
        const int i = args.empty() ? -1 : args[0].Get<int>();
        if (i < 0 || i >= static_cast<int>(m_clothSwatches.size())) return;
        const glm::vec3 c{ m_clothSwatches[i].r, m_clothSwatches[i].g, m_clothSwatches[i].b };
        for (HumanLook::Worn& w : m_look.clothes)
            if (const GarmentInfo* g = findGarment(w.id); g && g->slot == m_clothSlot) w.colour = c;
        applyColours(); // no new fitting: only the dye
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
    Human::BakeOptions suit;
    suit.groups = { "helper-tights" };
    suit.skeleton = false;
    m_tightsBake = m_human->bake(suit);
    const ModelModule::ModelId bodyModel = m_models->add(m_bodyBake.model, "character_creator/body");
    const ModelModule::ModelId partsModel = m_models->add(m_partsBake.model, "character_creator/parts");
    m_body = m_models->spawn(bodyModel);
    m_parts = m_models->spawn(partsModel);
    moveIrises();
    if (!m_irisModel.meshes.empty()) {
        m_irisModel.boundsMin = m_bodyBake.model.boundsMin - glm::vec3(0.2f);
        m_irisModel.boundsMax = m_bodyBake.model.boundsMax + glm::vec3(0.2f);
        m_irises = m_models->spawn(m_models->add(m_irisModel, "character_creator/irises"));
    }

    // The scalp cap: the body's triangles where hair grows (on the body
    // as it starts; the triangles then follow every slider).
    const HumanScalp scalp = findScalp(*m_pack, m_human->shapedPositions());
    std::vector<bool> onScalp(m_pack->positions().size(), false);
    for (uint32_t v : scalp.verts) onScalp[v] = true;
    const ModelMesh& skin = m_bodyBake.model.meshes[0];
    const std::vector<uint32_t>& from = m_bodyBake.sourceVertex[0];
    ModelMesh cap;
    cap.name = "scalp";
    std::map<uint32_t, uint32_t> capIndex;
    for (size_t t = 0; t + 2 < skin.indices.size(); t += 3) {
        const uint32_t a = skin.indices[t], b = skin.indices[t + 1], c = skin.indices[t + 2];
        if (!onScalp[from[a]] || !onScalp[from[b]] || !onScalp[from[c]]) continue;
        for (uint32_t v : { a, b, c }) {
            auto [it, added] = capIndex.emplace(v, static_cast<uint32_t>(m_capFrom.size()));
            if (added) {
                m_capFrom.push_back(v);
                cap.vertices.push_back(skin.vertices[v]);
            }
            cap.indices.push_back(it->second);
        }
    }
    if (!cap.indices.empty()) {
        m_capModel.meshes.push_back(std::move(cap));
        ModelMaterial m;
        m.name = "scalp";
        m.roughness = 0.9f;
        m_capModel.materials.push_back(m);
        moveCap();
        // Bounds for culling: the whole body's, since the sliders move the head.
        m_capModel.boundsMin = m_bodyBake.model.boundsMin - glm::vec3(0.2f);
        m_capModel.boundsMax = m_bodyBake.model.boundsMax + glm::vec3(0.2f);
        m_cap = m_models->spawn(m_models->add(m_capModel, "character_creator/scalp"));
    }
    applyColours();
    m_shapeDirty = true;
}

void CharacterCreatorModule::applyColours() {
    if (m_body) m_models->setTint(m_body, m_look.skin);
    if (m_irises) m_models->setTint(m_irises, m_look.eyes);
    for (auto& w : m_worn) {
        for (const HumanLook::Worn& worn : m_look.clothes)
            if (worn.id == w->id) w->colour = w->fabric.color = worn.colour;
        if (w->instance) m_models->setTint(w->instance, w->colour);
    }
    if (m_cap) {
        m_models->setVisible(m_cap, !m_look.hair.empty());
        // The skin a shade darker in the hair's shadow: a scalp in the
        // hair's own colour showed as a patch with a hard edge (Kees).
        m_models->setTint(m_cap, m_look.skin * 0.95f);
    }
}

void CharacterCreatorModule::moveIrises() {
    ModelData eyes = m_partsBake.model; // the eyeballs as the sliders have them now
    const size_t first = eyes.meshes.size();
    if (!addIrises(eyes, glm::vec3(1.0f))) return;
    if (m_irisModel.meshes.empty()) {
        for (size_t m = first; m < eyes.meshes.size(); ++m) {
            m_irisModel.materials.push_back(eyes.materials[eyes.meshes[m].material]);
            eyes.meshes[m].material = static_cast<uint32_t>(m_irisModel.materials.size() - 1);
            m_irisModel.meshes.push_back(std::move(eyes.meshes[m]));
        }
        return;
    }
    std::vector<std::vector<glm::vec3>> pos, nrm;
    for (size_t m = first; m < eyes.meshes.size(); ++m) {
        pos.emplace_back();
        nrm.emplace_back();
        for (const ModelVertex& v : eyes.meshes[m].vertices) {
            pos.back().push_back(v.position);
            nrm.back().push_back(v.normal);
        }
    }
    if (m_irises) m_models->setDeformedVertices(m_irises, pos, nrm, /*modelSpace=*/true);
}

void CharacterCreatorModule::moveCap() {
    if (m_capModel.meshes.empty()) return;
    const ModelMesh& skin = m_bodyBake.model.meshes[0];
    ModelMesh& cap = m_capModel.meshes[0];
    for (size_t i = 0; i < m_capFrom.size(); ++i) {
        const ModelVertex& v = skin.vertices[m_capFrom[i]];
        cap.vertices[i].position = v.position + v.normal * 0.002f; // over the skin, clear of depth fighting
        cap.vertices[i].normal = v.normal;
    }
    if (!m_cap) return;
    std::vector<std::vector<glm::vec3>> pos(1), nrm(1);
    for (const ModelVertex& v : cap.vertices) {
        pos[0].push_back(v.position);
        nrm[0].push_back(v.normal);
    }
    m_models->setDeformedVertices(m_cap, pos, nrm, /*modelSpace=*/true);
}

void CharacterCreatorModule::reshape() {
    for (Human::Bake* bake : { &m_bodyBake, &m_partsBake, &m_tightsBake }) m_human->reshape(*bake);
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
    moveCap();
    moveIrises();
    moveClothes();
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
    m_hairStyle = m_look.hair;
    m_hairLength = m_hairLengthShown = m_look.hairLength;
    m_handle.DirtyVariable("sliders");
    m_handle.DirtyVariable("tab");
    m_handle.DirtyVariable("hair_style");
    m_handle.DirtyVariable("hair_length");
    fillClothes();
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
            const auto& choices = hairChoices();
            m_look.hair = choices[std::uniform_int_distribution<size_t>(1, choices.size() - 1)(m_rng)].first;
            const auto& colours = hairColours();
            // Grey and white mostly on older people.
            const size_t last = m_human->get("macrodetails/Age") > 0.75f ? colours.size() - 1 : colours.size() - 3;
            const HairColour& c = colours[std::uniform_int_distribution<size_t>(0, last)(m_rng)];
            m_look.hairRoot = c.root;
            m_look.hairTip = c.tip;
            m_look.hairLength = std::clamp(1.0f + n(m_rng) * 0.15f, 0.7f, 1.3f);
            m_hairDirty = true;
            // An outfit: a top and a bottom nearly always, socks and shoes mostly.
            m_look.clothes.clear();
            const auto& cloth = clothColours();
            for (const char* slot : { "top", "bottom", "socks", "shoes" }) {
                std::vector<const GarmentInfo*> in;
                for (const GarmentInfo& g : garments())
                    if (g.slot == slot) in.push_back(&g);
                if (in.empty() || u(m_rng) > (std::string(slot) == "socks" ? 0.7f : 0.95f)) continue;
                const GarmentInfo* g = in[std::uniform_int_distribution<size_t>(0, in.size() - 1)(m_rng)];
                m_look.clothes.push_back({ g->id, cloth[std::uniform_int_distribution<size_t>(0, cloth.size() - 1)(m_rng)].rgb });
            }
            m_clothesDirty = true;
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
    if (std::abs(m_hairLength - m_hairLengthShown) > 0.004f) { // the length slider
        m_look.hairLength = m_hairLengthShown = m_hairLength;
        m_hairDirty = true;
        m_sinceEdit = 0.0f;
    }
    if (m_shapeDirty) {
        m_shapeDirty = false;
        m_hairDirty = true; // roots on the new scalp, once the sliders rest
        m_clothesDirty = true; // socks and skirts are cut by height
        reshape();
        // A new person (random, undo, load) may be a head taller: frame it
        // again. Not while a slider is dragged, so the camera holds still.
        if (!dragged) setView(m_view);
    }
    // CI (KKE_CREATOR_SELFTEST=1): a random person, through a share code
    // and back, must come out the same, stand on the floor and have its
    // hairstyle on its head.
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
        buildHair(); // the random hairstyle on the random head
        // Every guide asked for: a plaited style has only a row or braid each
        // (cornrows 6-14), so a fixed count failed whenever one came up.
        const bool hair = m_look.hair.empty() || (m_hair && m_hairRoots > 0 && m_world->hairStats(m_hair).guides >= m_hairRoots);
        dressUp(); // and every random garment on the random body
        bool dressed = m_worn.size() == m_look.clothes.size();
        std::string clothes;
        for (const auto& w : m_worn) {
            dressed = dressed && (w->cloth ? m_world->clothStats(w->cloth).triangles > 20 : w->instance != 0);
            clothes += (clothes.empty() ? "" : ", ") + w->id;
        }
        log::get(name())->info("creator self-test {} (code {}, floor {}, hair '{}' {}, clothes '{}' {})",
                               same && floor && hair && dressed ? "passed" : "FAILED", same ? "round-trips" : "differs",
                               floor ? "ok" : "off", m_look.hair, hair ? "ok" : "missing", clothes, dressed ? "ok" : "missing");
    }
    if (m_hairDirty && m_sinceEdit > 0.35f) buildHair();
    if (m_clothesDirty && m_sinceEdit > 0.35f) dressUp(); // after the hair: on the new body collider
    if (m_hair && m_hairDrawn && m_world->hairPositions(m_hair, m_guides)) m_hairDrawn->update(m_guides, m_figure);
    for (auto& w : m_worn) {
        if (!w->cloth || !w->drawn || !m_world->clothPositions(w->cloth, w->positions)) continue;
        clothNormals(w->positions, w->indices, w->normals);
        w->vertices.resize(w->positions.size());
        for (size_t i = 0; i < w->positions.size(); ++i) w->vertices[i] = Vertex{ w->positions[i], w->fabric.color, w->normals[i] };
        w->drawn->upload(w->vertices, w->indices);
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
    if (m_irises) m_models->setVisible(m_irises, true);
    for (auto& w : m_worn)
        if (w->instance) m_models->setVisible(w->instance, true);
}

void CharacterCreatorModule::close() {
    m_open = false;
    if (m_doc) m_doc->Hide();
    if (m_body) m_models->setVisible(m_body, false);
    if (m_parts) m_models->setVisible(m_parts, false);
    if (m_irises) m_models->setVisible(m_irises, false);
    for (auto& w : m_worn)
        if (w->instance) m_models->setVisible(w->instance, false);
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
    m_hairDirty = true;
    m_clothesDirty = true;
    fillSliders();
}

ModelData CharacterCreatorModule::bakeSkinned() const {
    Human h(*m_pack);
    applyLook(h, look());
    Human::Bake b = h.bake();
    for (ModelMaterial& m : b.model.materials)
        if (m.name == "Human_body") m.baseColor = look().skin;
    addIrises(b.model, look().eyes);
    dress(b.model, h, look().clothes);
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
    if (m_hairDrawn) m_app->renderer().retire(std::move(m_hairDrawn));
    undress();
    m_world.reset();
    if (m_doc) {
        m_doc->Close();
        m_doc = nullptr;
    }
    if (m_ui && m_ui->context()) m_ui->context()->RemoveDataModel("creator");
}

void CharacterCreatorModule::buildWorld() {
    RigidWorld::Settings settings;
    settings.threads = 1; // a few hundred strands: a worker is plenty, the game keeps the rest
    m_world = std::make_unique<RigidWorld>(settings);
}

void CharacterCreatorModule::buildHair() {
    m_hairDirty = false;
    if (!m_world) return;
    if (m_hair) m_world->removeHair(m_hair);
    m_hair = 0;
    if (m_hairDrawn) m_app->renderer().retire(std::move(m_hairDrawn));
    // The body as it is now is what the hair falls on: a mesh only cloth
    // and hair feel, turned with the figure.
    if (m_bodyCollider != RigidWorld::kNoBody) m_world->remove(m_bodyCollider);
    m_bodyCollider = RigidWorld::kNoBody;
    RigidWorld::BodyDesc body;
    body.shape = RigidWorld::Shape::Mesh;
    body.motion = RigidWorld::Motion::Static; // a mesh can't be kinematic in Jolt (no mass); turned with setTransform
    body.clothOnly = true;
    for (const ModelMesh& mesh : m_bodyBake.model.meshes) {
        const auto base = static_cast<uint32_t>(body.points.size());
        for (const ModelVertex& v : mesh.vertices) body.points.push_back(v.position);
        for (uint32_t i : mesh.indices) body.indices.push_back(base + i);
    }
    body.rotation = glm::angleAxis(m_yaw, glm::vec3(0, 1, 0));
    m_bodyCollider = m_world->add(body);

    applyColours(); // the scalp cap
    if (m_look.hair.empty()) return;
    HairDesc d;
    d.style.rootColor = m_look.hairRoot;
    d.style.tipColor = m_look.hairTip;
    if (!hairOnHuman(d, m_look.hair, *m_pack, m_human->shapedPositions(), kHairGuides)) {
        log::get(name())->warn("hair: no hairstyle '{}' (kke::hairstyleNames(), hairStyleNames())", m_look.hair);
        return;
    }
    d.style.length *= std::clamp(m_look.hairLength, 0.3f, 2.0f);
    m_hairRoots = d.roots.size();
    d.wind = 0.0f; // indoors
    d.bindPose = glm::mat4(1.0f); // the shaped body faces +Z at the origin
    m_hair = m_world->addHair(d);
    m_world->setHairJoint(m_hair, m_figure);
    m_hairDrawn = std::make_unique<HairRenderer>(*m_app);
    m_hairDrawn->build(d);
}

void CharacterCreatorModule::undress() {
    for (auto& w : m_worn) {
        if (w->instance) m_models->remove(w->instance);
        if (w->model) m_models->unload(w->model);
        if (w->cloth && m_world) m_world->removeCloth(w->cloth);
        if (w->drawn) m_app->renderer().retire(std::move(w->drawn));
    }
    m_worn.clear();
}

void CharacterCreatorModule::dressUp() {
    m_clothesDirty = false;
    undress();
    const std::vector<glm::vec3> shaped = m_human->shapedPositions();
    const ModelMesh* suit = m_tightsBake.model.meshes.empty() ? nullptr : &m_tightsBake.model.meshes[0];
    for (const HumanLook::Worn& worn : m_look.clothes) {
        const GarmentShape shape = garmentShape(*m_pack, shaped, worn.id);
        if (shape.verts.empty()) {
            log::get(name())->warn("clothes: no garment '{}' (kke::garments())", worn.id);
            continue;
        }
        auto w = std::make_unique<WornPiece>();
        w->id = worn.id;
        w->colour = worn.colour;
        w->offset = shape.offset;
        const std::set<uint32_t> in(shape.verts.begin(), shape.verts.end());

        if (shape.cloth) {
            if (!m_world) continue;
            // The skirt's faces whose four corners are in it, as triangles,
            // where the figure stands now; the waistband pinned to the figure.
            const int g = m_pack->findGroup(shape.group);
            if (g < 0) continue;
            std::map<uint32_t, uint32_t> index;
            ClothDesc d;
            auto vertex = [&](uint32_t v) {
                auto [it, added] = index.emplace(v, static_cast<uint32_t>(d.mesh.positions.size()));
                if (added) d.mesh.positions.push_back(glm::vec3(m_figure * glm::vec4(shaped[v], 1.0f)));
                return it->second;
            };
            const HumanGroup& group = m_pack->groups()[static_cast<size_t>(g)];
            for (uint32_t f = group.firstFace; f < group.firstFace + group.faceCount; ++f) {
                const auto& q = m_pack->quads()[f].verts;
                if (!in.count(q[0]) || !in.count(q[1]) || !in.count(q[2]) || !in.count(q[3])) continue;
                const uint32_t a = vertex(q[0]), b = vertex(q[1]), c = vertex(q[2]), e = vertex(q[3]);
                d.mesh.indices.insert(d.mesh.indices.end(), { a, b, c });
                if (q[3] != q[2]) d.mesh.indices.insert(d.mesh.indices.end(), { a, c, e });
            }
            for (uint32_t v : shape.pinned)
                if (auto it = index.find(v); it != index.end()) d.pinned.push_back(it->second);
            if (d.mesh.indices.empty() || d.pinned.empty()) continue;
            // Cloth coordinates for the weave: round the skirt and down it.
            for (const glm::vec3& p : d.mesh.positions) {
                const glm::vec3 local = glm::vec3(glm::inverse(m_figure) * glm::vec4(p, 1.0f));
                d.mesh.uvs.emplace_back(std::atan2(local.x, local.z) * 0.15f, -local.y);
            }
            d.fabric = clothFabric("cotton");
            d.fabric.color = worn.colour;
            d.fabric.thickness = 0.004f; // over the hips without a gap
            d.protection = ClothProtection::Basic; // one layer: the body is what it must not go through
            d.bindPose = { m_figure };
            d.wind = 0.0f;
            w->fabric = d.fabric;
            w->indices = d.mesh.indices;
            w->cloth = m_world->addCloth(d);
            m_world->setClothJoints(w->cloth, { m_figure });
            w->drawn = std::make_unique<DynamicMeshRenderer>(*m_app);
        } else if (suit) {
            // The body suit's triangles that are all this garment.
            ModelMesh mesh = garmentMesh(*suit, m_tightsBake.sourceVertex[0], shape, &w->from);
            mesh.name = worn.id;
            if (mesh.indices.empty()) continue;
            w->data.meshes.push_back(std::move(mesh));
            ModelMaterial m;
            m.name = worn.id;
            m.roughness = worn.id == "shoes" ? 0.5f : 0.85f;
            w->data.materials.push_back(m);
            // Bounds for culling: the whole body's, since the sliders move it.
            w->data.boundsMin = m_bodyBake.model.boundsMin - glm::vec3(0.2f);
            w->data.boundsMax = m_bodyBake.model.boundsMax + glm::vec3(0.2f);
            m_worn.push_back(std::move(w));
            WornPiece& piece = *m_worn.back();
            moveClothes(); // onto the body, offset over the skin
            piece.model = m_models->add(piece.data, "character_creator/garment/" + std::to_string(++m_garmentSerial), /*transient=*/true);
            piece.instance = m_models->spawn(piece.model, m_figure);
            m_models->setTint(piece.instance, piece.colour);
            m_models->setVisible(piece.instance, m_open);
            continue;
        }
        m_worn.push_back(std::move(w));
    }
    moveClothes();
}

void CharacterCreatorModule::moveClothes() {
    if (m_tightsBake.model.meshes.empty()) return;
    const ModelMesh& suit = m_tightsBake.model.meshes[0];
    for (auto& w : m_worn) {
        if (w->data.meshes.empty()) continue;
        ModelMesh& mesh = w->data.meshes[0];
        for (size_t i = 0; i < w->from.size(); ++i) {
            const ModelVertex& v = suit.vertices[w->from[i]];
            mesh.vertices[i].position = v.position + v.normal * w->offset;
            mesh.vertices[i].normal = v.normal;
        }
        if (!w->instance) continue;
        std::vector<std::vector<glm::vec3>> pos(1), nrm(1);
        for (const ModelVertex& v : mesh.vertices) {
            pos[0].push_back(v.position);
            nrm[0].push_back(v.normal);
        }
        m_models->setDeformedVertices(w->instance, pos, nrm, /*modelSpace=*/true);
    }
}

void CharacterCreatorModule::wear(const std::string& id) {
    const GarmentInfo* g = findGarment(id);
    if (!g) return;
    auto& clothes = m_look.clothes;
    const bool wasOn = std::any_of(clothes.begin(), clothes.end(), [&](const HumanLook::Worn& w) { return w.id == id; });
    glm::vec3 colour = g->slot == "top"    ? glm::vec3(0.35f, 0.65f, 1.0f)  // Sky
                       : g->slot == "bottom" ? glm::vec3(0.16f, 0.18f, 0.24f) // Night
                                             : glm::vec3(0.92f, 0.92f, 0.9f); // Snow
    for (const HumanLook::Worn& w : clothes) // a new skirt keeps the old shorts' colour
        if (const GarmentInfo* o = findGarment(w.id); o && o->slot == g->slot) colour = w.colour;
    clothes.erase(std::remove_if(clothes.begin(), clothes.end(),
                                 [&](const HumanLook::Worn& w) {
                                     const GarmentInfo* o = findGarment(w.id);
                                     return !o || o->slot == g->slot;
                                 }),
                  clothes.end());
    if (!wasOn) clothes.push_back({ id, colour });
    m_clothSlot = g->slot;
    m_clothesDirty = true;
    m_sinceEdit = 1e9f; // a click: dress now
    fillClothes();
}

void CharacterCreatorModule::fillClothes() {
    m_garments.clear();
    for (const GarmentInfo& g : garments()) {
        GarmentView v{ g.id, g.label, g.slot };
        v.worn = std::any_of(m_look.clothes.begin(), m_look.clothes.end(), [&](const HumanLook::Worn& w) { return w.id == g.id; });
        m_garments.push_back(std::move(v));
    }
    m_clothSlotLabel = "Colour: nothing worn on this slot";
    for (const HumanLook::Worn& w : m_look.clothes)
        if (const GarmentInfo* g = findGarment(w.id); g && g->slot == m_clothSlot) m_clothSlotLabel = "Colour: " + g->label;
    m_handle.DirtyVariable("garments");
    m_handle.DirtyVariable("cloth_slot");
    m_handle.DirtyVariable("cloth_slot_label");
}

void CharacterCreatorModule::placeFigure() {
    m_figure = glm::rotate(glm::mat4(1.0f), m_yaw, glm::vec3(0, 1, 0));
    if (m_body) m_models->setTransform(m_body, m_figure);
    if (m_parts) m_models->setTransform(m_parts, m_figure);
    if (m_irises) m_models->setTransform(m_irises, m_figure);
    if (m_cap) m_models->setTransform(m_cap, m_figure);
    for (auto& w : m_worn)
        if (w->instance) m_models->setTransform(w->instance, m_figure);
}

void CharacterCreatorModule::fixedUpdate(const FixedUpdateContext& ctx) {
    if (!m_world) return;
    if (m_turntable) m_yaw = std::fmod(m_yaw + ctx.fixedDt * 0.7f, 2.0f * kPi);
    const glm::mat4 before = m_figure;
    placeFigure();
    if (m_bodyCollider != RigidWorld::kNoBody && before != m_figure)
        m_world->setTransform(m_bodyCollider, glm::vec3(0.0f), glm::angleAxis(m_yaw, glm::vec3(0, 1, 0)));
    if (m_hair) m_world->setHairJoint(m_hair, m_figure);
    for (auto& w : m_worn)
        if (w->cloth) m_world->setClothJoints(w->cloth, { m_figure });
    m_world->step(ctx.fixedDt);
}

void CharacterCreatorModule::compute(VkCommandBuffer cmd) {
    if (m_hairDrawn && m_hair) m_hairDrawn->compute(cmd); // the hairs' points, before the passes draw them
}

void CharacterCreatorModule::render(const RenderContext& ctx) {
    if (m_hairDrawn && m_hair && m_open) m_hairDrawn->draw(ctx);
    if (m_open)
        for (auto& w : m_worn)
            if (w->drawn && !w->positions.empty()) w->drawn->drawCloth(ctx, w->fabric);
}

void CharacterCreatorModule::renderShadow(const ShadowRenderContext& ctx) {
    if (m_hairDrawn && m_hair && m_open) m_hairDrawn->drawShadow(ctx, -m_app->lighting().lights[0].direction);
    if (m_open)
        for (auto& w : m_worn)
            if (w->drawn && !w->positions.empty()) w->drawn->drawShadow(ctx);
}

} // namespace kke
