#include "kke/modules/CharacterCreatorModule.h"

#include "kke/AnimeRenderer.h"
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
#include "kke/VrmSprings.h"
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
#include <cstdlib>
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

// Body types: the build sliders each sets (decr|incr sliders run -1..1,
// the rest 0..1). Every build slider a preset leaves out goes back to its
// default. MakeHuman's own body-shape targets (bodyshapes-elvs-*) carry
// most of a silhouette; the measurements and muscles sharpen it.
struct Preset {
    const char* name;
    std::vector<std::pair<const char*, float>> values, female, male; // female/male: added by gender
};
// MakeHuman's proportions slider runs 0 (squat) to 1 (the classical ideal:
// longer legs, a smaller head, a waist); its middle is the average person
// and looks heavy-headed on a screen, so a new body type starts at 0.7.
constexpr float kProportions = 0.7f;
const std::vector<Preset>& presets() {
    static const std::vector<Preset> list = {
        { "Average", {}, {}, {} },
        // Short and slight, or tall and long-legged: the two ends of height.
        { "Petite",
          { { "macrodetails-height/Height", 0.18f }, { "macrodetails-universal/Weight", 0.3f }, { "macrodetails-universal/Muscle", 0.4f },
            { "macrodetails-proportions/BodyProportions", 0.8f } },
          { { "bodyshapes/bodyshapes-elvs-fem-neat-hourglass", 0.4f } },
          { { "bodyshapes/bodyshapes-elvs-man-lean-column", 0.4f } } },
        { "Model",
          { { "macrodetails-height/Height", 0.64f }, { "macrodetails-universal/Weight", 0.18f }, { "macrodetails-universal/Muscle", 0.4f },
            { "macrodetails-proportions/BodyProportions", 1.0f }, { "measure/measure-waist-circ-decr|incr", -0.3f } },
          { { "bodyshapes/bodyshapes-elvs-fem-lean-column", 0.5f } },
          { { "bodyshapes/bodyshapes-elvs-man-lean-column", 0.4f } } },
        { "Slim",
          { { "macrodetails-universal/Weight", 0.25f }, { "macrodetails-universal/Muscle", 0.45f },
            { "measure/measure-waist-circ-decr|incr", -0.4f } },
          { { "bodyshapes/bodyshapes-elvs-fem-lean-column", 0.7f } },
          { { "bodyshapes/bodyshapes-elvs-man-lean-column", 0.7f } } },
        { "Athletic",
          { { "macrodetails-universal/Weight", 0.4f }, { "macrodetails-universal/Muscle", 0.85f },
            { "stomach/stomach-tone-decr|incr", 0.8f }, { "torso/torso-vshape-decr|incr", 0.45f },
            { "measure/measure-waist-circ-decr|incr", -0.45f }, { "armslegs/r-upperleg-muscle-decr|incr", 0.5f },
            { "armslegs/l-upperleg-muscle-decr|incr", 0.5f }, { "armslegs/r-lowerleg-muscle-decr|incr", 0.5f },
            { "armslegs/l-lowerleg-muscle-decr|incr", 0.5f }, { "buttocks/buttocks-volume-decr|incr", 0.3f } },
          { { "bodyshapes/bodyshapes-elvs-fem-neat-hourglass", 0.6f } },
          { { "bodyshapes/bodyshapes-elvs-man-invert-triangle", 0.6f } } },
        { "Strong",
          { { "macrodetails-universal/Weight", 0.62f }, { "macrodetails-universal/Muscle", 1.0f },
            { "torso/torso-vshape-decr|incr", 0.8f }, { "torso/torso-muscle-pectoral-decr|incr", 0.7f },
            { "torso/torso-muscle-dorsi-decr|incr", 0.7f }, { "armslegs/r-upperarm-muscle-decr|incr", 0.7f },
            { "armslegs/l-upperarm-muscle-decr|incr", 0.7f }, { "armslegs/r-upperarm-shoulder-muscle-decr|incr", 0.7f },
            { "armslegs/l-upperarm-shoulder-muscle-decr|incr", 0.7f }, { "armslegs/r-upperleg-muscle-decr|incr", 0.5f },
            { "armslegs/l-upperleg-muscle-decr|incr", 0.5f }, { "neck/neck-scale-horiz-decr|incr", 0.5f },
            { "measure/measure-shoulder-dist-decr|incr", 0.5f }, { "stomach/stomach-tone-decr|incr", 0.6f } },
          { { "bodyshapes/bodyshapes-elvs-fem-invert-triangle", 0.5f } },
          { { "bodyshapes/bodyshapes-elvs-man-trapezoid", 1.0f } } },
        { "Curvy",
          { { "macrodetails-universal/Weight", 0.6f }, { "macrodetails-universal/Muscle", 0.5f },
            { "measure/measure-waist-circ-decr|incr", -0.5f }, { "measure/measure-hips-circ-decr|incr", 0.7f },
            { "buttocks/buttocks-volume-decr|incr", 0.7f }, { "armslegs/r-upperleg-fat-decr|incr", 0.35f },
            { "armslegs/l-upperleg-fat-decr|incr", 0.35f } },
          { { "bodyshapes/bodyshapes-elvs-fem-full-hourglass", 1.0f }, { "breast/BreastSize", 0.9f },
            { "breast/BreastFirmness", 0.75f }, { "measure/measure-bust-circ-decr|incr", 0.5f } },
          { { "bodyshapes/bodyshapes-elvs-man-triangle", 0.6f } } },
        { "Heavy",
          { { "macrodetails-universal/Weight", 1.0f }, { "macrodetails-universal/Muscle", 0.5f },
            { "stomach/stomach-pregnant-decr|incr", 0.4f }, { "measure/measure-waist-circ-decr|incr", 0.7f },
            { "measure/measure-hips-circ-decr|incr", 0.5f }, { "neck/neck-double-decr|incr", 0.6f },
            { "armslegs/r-upperarm-fat-decr|incr", 0.7f }, { "armslegs/l-upperarm-fat-decr|incr", 0.7f },
            { "armslegs/r-upperleg-fat-decr|incr", 0.7f }, { "armslegs/l-upperleg-fat-decr|incr", 0.7f },
            { "buttocks/buttocks-volume-decr|incr", 0.4f } },
          { { "bodyshapes/bodyshapes-elvs-fem-apple", 0.6f }, { "breast/BreastSize", 0.8f } },
          { { "bodyshapes/bodyshapes-elvs-man-apple", 0.7f } } },
    };
    return list;
}
// The sliders a body type owns (put back to default before one is applied).
bool buildSlider(const HumanModifier& m) {
    if (m.macro == "Muscle" || m.macro == "Weight" || m.macro == "BreastSize" || m.macro == "BreastFirmness") return true;
    if (m.macro == "Height" || m.macro == "BodyProportions") return true; // a body type is a stature and a build
    for (const char* g : { "bodyshapes", "measure", "torso", "hip", "stomach", "buttocks" })
        if (m.group == g) return true;
    return m.group == "armslegs" && (m.name.find("muscle") != std::string::npos || m.name.find("fat") != std::string::npos);
}

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

constexpr int kHairGuides = 320;

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
    bool fromBody = false; // cut from the skin itself (gloves), not the body suit
    // Fitted.
    ModelModule::ModelId model = 0;
    ModelModule::InstanceId instance = 0;
    std::vector<uint32_t> from; // piece vertex -> body suit (or skin) bake vertex
    std::vector<float> lift;    // piece vertex -> m slid up onto an open neckline
    std::vector<uint32_t> under; // piece vertex -> the skin vertex it must stay over
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
    m_humansDir = dir;
    // CharMorph's Vitruvian body (docs/HUMANS.md) when the pack is there, MakeHuman's own base mesh when not
    // (or KKE_HUMAN_BODY=makehuman): the same sliders, rig and helper meshes on a finer, better-formed body.
    const bool makehuman = std::string(dev::env("KKE_HUMAN_BODY") ? dev::env("KKE_HUMAN_BODY") : "") == "makehuman";
    const bool vitruvian = !makehuman && std::filesystem::exists(dir + "/vitruvian.kkhpack");
    m_skinPrefix = vitruvian ? "vitruvian_" : "";
    m_pack = std::make_unique<HumanPack>(HumanPack::load(dir + (vitruvian ? "/vitruvian.kkhpack" : "/makehuman.kkhpack")));
    m_human = std::make_unique<Human>(*m_pack);
    m_human->set("macrodetails/Gender", 0.0f); // a new person is a woman or a man, not half way: Female / Male buttons
    setBodyType("Average");
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
    for (const Preset& p : presets()) m_presets.push_back(p.name);
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
    if (const char* style = dev::env("KKE_CREATOR_STYLE")) { // screenshots
        m_anime = std::string(style) == "anime";
        m_handle.DirtyVariable("anime");
        applyStyle();
    }
    if (const char* hair = dev::env("KKE_CREATOR_HAIR")) { // screenshots and CI: "long", "afro", "" = none
        m_look.hair = hair;
        fillSliders();
    }
    if (const char* gender = dev::env("KKE_CREATOR_GENDER")) applyArchetype(std::strtof(gender, nullptr) >= 0.5f); // 0 female, 1 male
    if (const char* preset = dev::env("KKE_CREATOR_PRESET")) applyPreset(preset); // "Curvy", "Strong"...
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
        g.RegisterMember("thumb", &GarmentView::thumb);
        g.RegisterMember("worn", &GarmentView::worn);
    }
    c.RegisterArray<std::vector<GarmentView>>();
    c.Bind("garments", &m_garments);
    c.RegisterArray<std::vector<Rml::String>>();
    c.Bind("presets", &m_presets);
    c.Bind("archetype", &m_archetype);
    c.Bind("body_type", &m_bodyType);
    c.BindEventCallback("archetype", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args) {
        if (!args.empty()) applyArchetype(args[0].Get<Rml::String>() == "male");
    });
    c.BindEventCallback("preset", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args) {
        if (!args.empty()) applyPreset(args[0].Get<Rml::String>());
    });
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
    c.Bind("anime", &m_anime);
    c.BindEventCallback("style", [this](Rml::DataModelHandle h, Rml::Event&, const Rml::VariantList& args) {
        m_anime = !args.empty() && args[0].Get<Rml::String>() == "anime";
        h.DirtyVariable("anime");
        applyStyle();
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
        applyColours(); // the brows
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
    skinMaterial(m_bodyBake.model);
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
    moveBrows();
    if (!m_browModel.meshes.empty()) {
        m_browModel.boundsMin = m_bodyBake.model.boundsMin - glm::vec3(0.2f);
        m_browModel.boundsMax = m_bodyBake.model.boundsMax + glm::vec3(0.2f);
        m_brows = m_models->spawn(m_models->add(m_browModel, "character_creator/brows"));
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
    applyStyle();
    m_shapeDirty = true;
}

void CharacterCreatorModule::applyColours() {
    if (m_body) m_models->setTint(m_body, m_look.skin);
    if (m_irises) m_models->setTint(m_irises, m_look.eyes);
    if (m_brows) m_models->setTint(m_brows, m_look.hairRoot);
    for (auto& w : m_worn) {
        for (const HumanLook::Worn& worn : m_look.clothes)
            if (worn.id == w->id) w->colour = w->fabric.color = worn.colour;
        if (w->instance) m_models->setTint(w->instance, w->colour);
    }
    if (m_cap) {
        m_models->setVisible(m_cap, m_open && !m_anime && !m_look.hair.empty());
        // The skin a shade darker in the hair's shadow: a scalp in the
        // hair's own colour showed as a patch with a hard edge (Kees).
        m_models->setTint(m_cap, m_look.skin * 0.95f);
    }
}

void CharacterCreatorModule::applyStyle() {
    // Realistic: the MakeHuman body. Anime: VRoid's avatar, a body of its
    // own (docs/ANIME.md). One shows at a time; the sliders and the share
    // code stay the realistic body's.
    const bool real = m_open && !m_anime;
    for (const ModelModule::InstanceId id : { m_body, m_parts, m_irises, m_brows })
        if (id) m_models->setVisible(id, real);
    if (m_cap) m_models->setVisible(m_cap, real && !m_look.hair.empty());
    for (auto& w : m_worn)
        if (w->instance) m_models->setVisible(w->instance, real);
    if (m_anime && m_open) {
        showAnimeBody();
        dressAnime(); // what was put on while the other body showed
    }
    if (!m_anime && m_open) m_clothesDirty = true; // and back: the realistic pieces for what was put on
    if (m_animeDrawn) m_animeDrawn->setVisible(m_open && m_anime);
}

// What is on the avatar, as a string: a garment and its dye each.
std::string CharacterCreatorModule::outfitKey() const {
    std::string key;
    char colour[48];
    for (const HumanLook::Worn& w : m_look.clothes) {
        std::snprintf(colour, sizeof colour, "|%.3f,%.3f,%.3f;", w.colour.r, w.colour.g, w.colour.b);
        key += w.id + colour;
    }
    return key;
}

void CharacterCreatorModule::dressAnime() {
    if (!m_vrm || !m_animeDrawn) return;
    const std::string key = outfitKey();
    if (key == m_animeOutfit) return;
    const size_t pieces = dressVrm(*m_vrm, m_vrmStock, m_look.clothes);
    m_animeOutfit = key;
    m_animeDrawn->build(*m_vrm); // the garments are meshes of the avatar now
    m_animeDrawn->setVisible(m_open && m_anime);
    poseAnime(0.0f);
    log::get(name())->info("anime clothes: {} garment mesh(es): {}", pieces, key.empty() ? "nothing (the avatar's own clothes)" : key);
}

void CharacterCreatorModule::showAnimeBody() {
    const bool male = m_human->get("macrodetails/Gender") >= 0.5f;
    // assets/anime sits next to assets/humans (kke_use_character_creator copies both).
    const std::string file = (std::filesystem::path(m_humansDir).parent_path() / "anime" / (male ? "HairSample_Male.vrm" : "HairSample_Female.vrm")).string();
    if (m_vrm && file == m_vrmFile) return;
    try {
        auto avatar = std::make_unique<VrmAvatar>(loadVrm(file));
        m_springs.reset();
        m_vrm = std::move(avatar);
        m_vrmStock = VrmStock{};
        const size_t pieces = dressVrm(*m_vrm, m_vrmStock, m_look.clothes); // before the renderer takes the meshes
        m_animeOutfit = outfitKey();
        log::get(name())->info("anime clothes: {} garment mesh(es) on {}: {}", pieces, std::filesystem::path(file).filename().string(),
                               m_animeOutfit.empty() ? "nothing (the avatar's own clothes)" : m_animeOutfit);
    } catch (const std::exception& e) {
        log::get(name())->warn("anime body: {}", e.what());
        return;
    }
    m_vrmFile = file;
    if (!m_animeDrawn) m_animeDrawn = std::make_unique<AnimeRenderer>(*m_app);
    m_animeDrawn->build(*m_vrm);
    m_springs = std::make_unique<VrmSprings>(*m_vrm);
    m_expressions.assign(m_vrm->expressions.size(), 0.0f);
    m_vrmLocals = m_vrm->restLocals();
    // VRoid stands in a T: the arms down to an A like the realistic body.
    m_vrmWorld = computeRestPose(m_vrm->model);
    for (const auto& [bone, sign] : { std::pair{ "leftUpperArm", -1.0f }, std::pair{ "rightUpperArm", 1.0f } }) {
        const int b = m_vrm->bone(bone);
        if (b < 0) continue;
        const glm::mat3 world(m_vrmWorld[static_cast<size_t>(b)]);
        const glm::mat3 inModel = glm::transpose(world) * glm::mat3(glm::rotate(glm::mat4(1.0f), sign * glm::radians(62.0f), glm::vec3(0, 0, 1))) * world;
        m_vrmLocals[static_cast<size_t>(b)] = m_vrmLocals[static_cast<size_t>(b)] * glm::mat4(inModel);
    }
    m_springs->reset(m_vrmLocals, m_figure);
    m_animeDrawn->setVisible(m_open && m_anime);
    poseAnime(0.0f);
}

void CharacterCreatorModule::poseAnime(float dt) {
    if (!m_vrm || !m_animeDrawn) return;
    m_animeTime += dt;
    // Blinks every few seconds, each a fifth of a second.
    const int blink = m_vrm->expression("blink");
    if (blink >= 0) {
        if (m_animeTime > m_nextBlink + 0.2f) m_nextBlink = m_animeTime + 2.0f + 3.0f * std::uniform_real_distribution<float>(0.0f, 1.0f)(m_rng);
        const float t = (m_animeTime - m_nextBlink) / 0.2f;
        m_expressions[static_cast<size_t>(blink)] = t > 0.0f && t < 1.0f ? std::sin(t * kPi) : 0.0f;
    }
    if (const char* face = dev::env("KKE_CREATOR_EXPRESSION")) { // screenshots: "joy", "angry", ...
        const int e = m_vrm->expression(face);
        if (e >= 0) m_expressions[static_cast<size_t>(e)] = 1.0f;
    }
    std::vector<glm::mat4> locals = m_vrmLocals;
    // Breathing: the chest rises a little.
    if (const int chest = m_vrm->bone("chest"); chest >= 0)
        locals[static_cast<size_t>(chest)] = locals[static_cast<size_t>(chest)] *
            glm::rotate(glm::mat4(1.0f), 0.015f * std::sin(m_animeTime * 1.6f), glm::vec3(1, 0, 0));
    m_springs->step(dt, locals, m_figure);
    m_vrm->pose(locals, m_vrm->weights(m_expressions), m_posed);
    // The head bone's model-space transform, for the face's light.
    const std::vector<ModelBone>& bones = m_vrm->model.bones;
    m_vrmWorld.resize(bones.size());
    for (size_t b = 0; b < bones.size(); ++b)
        m_vrmWorld[b] = (bones[b].parent >= 0 ? m_vrmWorld[static_cast<size_t>(bones[b].parent)] : glm::mat4(1.0f)) * locals[b];
    const int head = m_vrm->bone("head");
    m_animeDrawn->update(m_posed, m_figure, head >= 0 ? m_vrmWorld[static_cast<size_t>(head)] : glm::mat4(1.0f));
}

void CharacterCreatorModule::skinMaterial(ModelData& model) const {
    // tools/makehuman/bake_skin.py: lips, blush, lids, pores, gloss, in the
    // body's UV layout; the albedo is a near-white multiplier on the tint.
    namespace fs = std::filesystem;
    // A map is a .jpg (Vitruvian's 4096 atlas: small files) or a .png (the MakeHuman bake).
    auto map = [&](const char* name) {
        for (const char* ext : { ".jpg", ".png" })
            if (const std::string f = m_humansDir + "/" + m_skinPrefix + name + ext; fs::exists(f)) return f;
        return std::string();
    };
    const std::string albedo = map("skin_albedo");
    for (ModelMaterial& m : model.materials) {
        if (m.name != "Human_body") continue;
        m.roughness = 0.55f;
        if (albedo.empty()) continue; // a pack without the maps: the plain colour
        m.albedoTexture = albedo;
        m.normalTexture = map("skin_normal");
        m.roughnessTexture = map("skin_roughness");
        m.roughness = 1.0f; // the map holds it
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

void CharacterCreatorModule::moveBrows() {
    ModelData face = m_partsBake.model; // the eyes as the sliders have them now
    const size_t first = face.meshes.size();
    if (!addBrows(face, m_bodyBake.model.meshes[0], glm::vec3(1.0f))) return;
    if (m_browModel.meshes.empty()) {
        for (size_t m = first; m < face.meshes.size(); ++m) {
            m_browModel.materials.push_back(face.materials[face.meshes[m].material]);
            face.meshes[m].material = static_cast<uint32_t>(m_browModel.materials.size() - 1);
            m_browModel.meshes.push_back(std::move(face.meshes[m]));
        }
        return;
    }
    std::vector<std::vector<glm::vec3>> pos, nrm;
    for (size_t m = first; m < face.meshes.size(); ++m) {
        pos.emplace_back();
        nrm.emplace_back();
        for (const ModelVertex& v : face.meshes[m].vertices) {
            pos.back().push_back(v.position);
            nrm.back().push_back(v.normal);
        }
    }
    if (m_brows) m_models->setDeformedVertices(m_brows, pos, nrm, /*modelSpace=*/true);
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
    moveBrows();
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
            if (m.macro == "Gender") continue; // an archetype (Female / Male buttons), not a slider
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
    m_archetype = m_human->get("macrodetails/Gender") < 0.5f ? "female" : "male"; // after a load, an undo, a random person
    m_handle.DirtyVariable("archetype");
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

void CharacterCreatorModule::applyPreset(const std::string& name) {
    if (std::none_of(presets().begin(), presets().end(), [&](const Preset& p) { return name == p.name; })) return;
    pushUndo();
    setBodyType(name);
    m_shapeDirty = true;
    fillSliders();
}

void CharacterCreatorModule::setBodyType(const std::string& name) {
    const Preset* found = nullptr;
    for (const Preset& p : presets())
        if (name == p.name) found = &p;
    if (!found) return;
    const Preset& preset = *found;
    for (const HumanModifier& m : m_pack->modifiers())
        if (buildSlider(m)) m_human->set(m.name, m.macro == "BodyProportions" ? kProportions : m.defaultValue);
    const bool female = m_human->get("macrodetails/Gender") < 0.5f;
    for (const auto* list : { &preset.values, female ? &preset.female : &preset.male })
        for (const auto& [slider, value] : *list) m_human->set(slider, value);
    m_bodyType = preset.name;
    if (m_handle) m_handle.DirtyVariable("body_type");
}

void CharacterCreatorModule::applyArchetype(bool male) {
    pushUndo();
    m_human->set("macrodetails/Gender", male ? 1.0f : 0.0f);
    m_archetype = male ? "male" : "female";
    setBodyType(m_bodyType); // the same build, cut for this body
    m_shapeDirty = true;
    if (m_anime) applyStyle(); // the other VRoid avatar
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
        const float v = m.macro == "Gender" ? (u(m_rng) < 0.5f ? 0.0f : 1.0f) : pick(m);
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
        // Half the time a body type (scaled 50-100%): the sliders alone,
        // kept near the middle, make everyone look alike.
        if (u(m_rng) < 0.5f) {
            const Preset& p = presets()[std::uniform_int_distribution<size_t>(1, presets().size() - 1)(m_rng)];
            const float amount = 0.5f + 0.5f * u(m_rng);
            const bool female = m_human->get("macrodetails/Gender") < 0.5f;
            for (const auto* list : { &p.values, female ? &p.female : &p.male })
                for (const auto& [slider, value] : *list) {
                    const int m = m_pack->findModifier(slider);
                    const float def = m >= 0 ? m_pack->modifiers()[static_cast<size_t>(m)].defaultValue : 0.0f;
                    m_human->set(slider, def + (value - def) * amount);
                }
        }
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
            // An outfit: a dress now and then, else a top and a bottom nearly
            // always; socks and shoes mostly; a coat or gloves sometimes.
            m_look.clothes.clear();
            const auto& cloth = clothColours();
            const bool dress = u(m_rng) < 0.25f;
            for (const std::string& slot : garmentSlots()) {
                if (dress ? (slot == "top" || slot == "bottom") : slot == "dress") continue;
                std::vector<const GarmentInfo*> in;
                for (const GarmentInfo& g : garments())
                    if (g.slot == slot) in.push_back(&g);
                const float chance = slot == "socks" ? 0.7f : slot == "outer" ? 0.3f : slot == "gloves" ? 0.15f : 0.95f;
                if (in.empty() || u(m_rng) > chance) continue;
                const GarmentInfo* g = in[std::uniform_int_distribution<size_t>(0, in.size() - 1)(m_rng)];
                // Half the time its own colour (jeans blue, leather black), else any dye.
                const glm::vec3 colour = u(m_rng) < 0.5f ? g->colour : cloth[std::uniform_int_distribution<size_t>(0, cloth.size() - 1)(m_rng)].rgb;
                m_look.clothes.push_back({ g->id, colour });
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
        dressUp(); // and every random garment on the random body, every piece of it
        size_t pieces = 0;
        std::string clothes;
        for (const HumanLook::Worn& worn : m_look.clothes) {
            pieces += garmentPieces(*m_pack, m_human->shapedPositions(), worn.id).size();
            clothes += (clothes.empty() ? "" : ", ") + worn.id;
        }
        bool dressed = m_worn.size() == pieces;
        for (const auto& w : m_worn)
            dressed = dressed && (w->cloth ? m_world->clothStats(w->cloth).triangles > 20 : w->instance != 0);
        if (m_anime) { // the avatar wears them: a mesh each at least, and what it wears is what was asked
            dressed = m_vrm && m_vrmStock.ready && m_animeOutfit == outfitKey() &&
                      m_vrm->model.meshes.size() >= m_vrmStock.meshes + m_look.clothes.size();
        }
        log::get(name())->info("creator self-test {} (code {}, floor {}, hair '{}' {}, clothes '{}' {})",
                               same && floor && hair && dressed ? "passed" : "FAILED", same ? "round-trips" : "differs",
                               floor ? "ok" : "off", m_look.hair, hair ? "ok" : "missing", clothes, dressed ? "ok" : "missing");
    }
    if (m_hairDirty && m_sinceEdit > 0.35f) buildHair();
    if (m_clothesDirty && m_sinceEdit > 0.35f) dressUp(); // after the hair: on the new body collider
    if (m_hair && m_hairDrawn && m_world->hairPositions(m_hair, m_guides)) m_hairDrawn->update(m_guides, m_figure);
    if (m_anime) poseAnime(ctx.dt);
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
    if (m_brows) m_models->setVisible(m_brows, true);
    for (auto& w : m_worn)
        if (w->instance) m_models->setVisible(w->instance, true);
}

void CharacterCreatorModule::close() {
    m_open = false;
    if (m_doc) m_doc->Hide();
    if (m_body) m_models->setVisible(m_body, false);
    if (m_parts) m_models->setVisible(m_parts, false);
    if (m_irises) m_models->setVisible(m_irises, false);
    if (m_brows) m_models->setVisible(m_brows, false);
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
    skinMaterial(b.model);
    for (ModelMaterial& m : b.model.materials)
        if (m.name == "Human_body") m.baseColor = look().skin;
    addIrises(b.model, look().eyes);
    for (const ModelMesh& m : b.model.meshes)
        if (m.name == "body") {
            const ModelMesh skin = m; // a copy: addBrows adds meshes to the model
            addBrows(b.model, skin, look().hairRoot);
            break;
        }
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
    if (m_animeDrawn) m_app->renderer().retire(std::move(m_animeDrawn));
    m_springs.reset();
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
    d.style.shine = std::min(d.style.shine, 0.3f); // thin strands under a bright light read as white scratches
    // Fuller: a head has 100,000 hairs and the guides stand for clumps of them; with 320 guides, 24 drawn hairs
    // round each let the scalp show between strings of hair.
    d.style.hairsPerGuide = std::clamp(static_cast<int>(static_cast<float>(d.style.hairsPerGuide) * 2.5f), 24, 90);
    d.style.hairWidth *= 1.25f;
    d.style.spread = std::max(d.style.spread, 2.0f); // neighbouring clumps overlap: no scalp between bunches
    d.style.clump *= 0.5f;
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
    if (m_anime) { // the avatar wears them (VrmClothes.h); the realistic pieces are rebuilt when that body shows again
        applyStyle(); // loads the avatar if it is not there, then dresses it
        return;
    }
    undress();
    const std::vector<glm::vec3> shaped = m_human->shapedPositions();
    const ModelMesh* suit = m_tightsBake.model.meshes.empty() ? nullptr : &m_tightsBake.model.meshes[0];
    const ModelMesh* skin = m_bodyBake.model.meshes.empty() ? nullptr : &m_bodyBake.model.meshes[0];
    for (const HumanLook::Worn& worn : m_look.clothes) {
        const GarmentInfo* info = findGarment(worn.id);
        const std::vector<GarmentShape> pieces = garmentPieces(*m_pack, shaped, worn.id);
        if (!info || pieces.empty()) {
            log::get(name())->warn("clothes: no garment '{}' (kke::garments())", worn.id);
            continue;
        }
        for (const GarmentShape& shape : pieces) {
            auto w = std::make_unique<WornPiece>();
            w->id = worn.id;
            w->colour = worn.colour;
            w->offset = shape.offset;
            w->fromBody = shape.group == "body";
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
                d.fabric = clothFabric(info->fabric); // satin drapes, denim and leather hold their shape
                d.fabric.color = worn.colour;
                d.fabric.thickness = std::max(0.004f, shape.offset); // over the hips without a gap; a coat's tails over the clothes under it
                d.protection = ClothProtection::Basic; // one layer: the body is what it must not go through
                d.bindPose = { m_figure };
                d.wind = 0.0f;
                w->fabric = d.fabric;
                w->indices = d.mesh.indices;
                w->cloth = m_world->addCloth(d);
                m_world->setClothJoints(w->cloth, { m_figure });
                w->drawn = std::make_unique<DynamicMeshRenderer>(*m_app);
            } else if (w->fromBody ? skin != nullptr : suit != nullptr) {
                // The body suit's (or, for gloves, the skin's) triangles that
                // are all this piece.
                const Human::Bake& source = w->fromBody ? m_bodyBake : m_tightsBake;
                ModelMesh mesh = garmentMesh(source.model.meshes[0], source.sourceVertex[0], shape, &w->from, &w->lift);
                mesh.name = worn.id;
                if (mesh.indices.empty()) continue;
                w->data.meshes.push_back(std::move(mesh));
                ModelMaterial m = garmentMaterial(*info, worn.colour); // the fabric's roughness; the colour is the tint
                m.name = worn.id;
                m.baseColor = glm::vec3(1.0f);
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
        } // each piece
    }
    moveClothes();
    applyStyle();
    size_t cloth = 0;
    std::string worn;
    for (const auto& w : m_worn) {
        cloth += w->cloth ? 1 : 0;
        if (worn.find(w->id) == std::string::npos) worn += (worn.empty() ? "" : ", ") + w->id;
    }
    log::get(name())->info("clothes: {} piece(s), {} as cloth: {}", m_worn.size(), cloth, worn.empty() ? "nothing" : worn);
}

void CharacterCreatorModule::moveClothes() {
    if (m_tightsBake.model.meshes.empty()) return;
    const ModelMesh& suit = m_tightsBake.model.meshes[0];
    const ModelMesh& skin = m_bodyBake.model.meshes[0];
    for (auto& w : m_worn) {
        if (w->data.meshes.empty()) continue;
        ModelMesh& mesh = w->data.meshes[0];
        const ModelMesh& source = w->fromBody ? skin : suit;
        for (size_t i = 0; i < w->from.size(); ++i) {
            const ModelVertex& v = source.vertices[w->from[i]];
            mesh.vertices[i].position = v.position + v.normal * w->offset;
            if (i < w->lift.size()) mesh.vertices[i].position.y += w->lift[i];
            mesh.vertices[i].normal = v.normal;
        }
        if (!w->fromBody) { // gloves are the skin itself, moved out: nothing to stay over
            if (w->under.empty()) w->under = skinUnder(mesh, skin);
            keepOutside(mesh, skin, w->under, w->offset);
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
    glm::vec3 colour = g->colour; // its own (jeans blue, leather black)...
    for (const HumanLook::Worn& w : clothes) // ...unless a new skirt keeps the old shorts' colour
        if (const GarmentInfo* o = findGarment(w.id); o && o->slot == g->slot) colour = w.colour;
    // Off with what it replaces: the slot's garment, and under a dress the top and the bottom.
    clothes.erase(std::remove_if(clothes.begin(), clothes.end(),
                                 [&](const HumanLook::Worn& w) {
                                     const GarmentInfo* o = findGarment(w.id);
                                     return !o || garmentSlotsClash(o->slot, g->slot);
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
        if (g.slot != m_clothSlot) continue; // the browser shows one slot at a time
        GarmentView v{ g.id, g.label, g.slot, {}, false };
        // thumbs/<id>.png, spaces as underscores (tools/garment_thumbs).
        std::string file = g.id;
        std::replace(file.begin(), file.end(), ' ', '_');
        v.thumb = "thumbs/" + file + ".png";
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
    if (m_brows) m_models->setTransform(m_brows, m_figure);
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
    if (m_anime) {
        if (m_animeDrawn && m_open) m_animeDrawn->draw(ctx);
        return;
    }
    if (m_hairDrawn && m_hair && m_open) m_hairDrawn->draw(ctx);
    if (m_open)
        for (auto& w : m_worn)
            if (w->drawn && !w->positions.empty()) w->drawn->drawCloth(ctx, w->fabric);
}

void CharacterCreatorModule::renderShadow(const ShadowRenderContext& ctx) {
    if (m_anime) {
        if (m_animeDrawn && m_open) m_animeDrawn->drawShadow(ctx);
        return;
    }
    if (m_hairDrawn && m_hair && m_open) m_hairDrawn->drawShadow(ctx, -m_app->lighting().lights[0].direction);
    if (m_open)
        for (auto& w : m_worn)
            if (w->drawn && !w->positions.empty()) w->drawn->drawShadow(ctx);
}

} // namespace kke
