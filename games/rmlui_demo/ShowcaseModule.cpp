#include "ShowcaseModule.h"

#include "kke/Application.h"
#include "kke/Log.h"
#include "kke/modules/SettingsModule.h"
#include "kke/modules/UiModule.h"
#include "kke/modules/InputModule.h"
#include "InputScreen.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <typeindex>

namespace kke_demo {

namespace {

const char* kScreens[] = { "menu", "settings", "input", "inventory", "hud", "dialog", "loading" };
const char* kDocumentFiles[] = { "main_menu.rml", "settings.rml", "input.rml", "inventory.rml", "hud.rml", "dialog.rml", "loading.rml" };

int rarityRank(const std::string& r) {
    if (r == "legendary") return 5;
    if (r == "epic") return 4;
    if (r == "rare") return 3;
    if (r == "uncommon") return 2;
    if (r == "common") return 1;
    return 0;
}

std::string argString(const Rml::VariantList& args, size_t i) {
    return i < args.size() ? args[i].Get<Rml::String>() : Rml::String();
}

int argInt(const Rml::VariantList& args, size_t i) {
    return i < args.size() ? args[i].Get<int>() : -1;
}

// Finds the inventory slot id ("b3", "e1") an element belongs to, walking
// up from whatever child the pointer actually hit.
std::string slotIdOf(Rml::Element* element) {
    for (Rml::Element* e = element; e; e = e->GetParentNode()) {
        if (e->HasAttribute("slot")) return e->GetAttribute<Rml::String>("slot", "");
    }
    return {};
}

ShowcaseModule::Item makeItem(const char* icon, const char* name, const char* rarity, const char* category,
                              const char* equipType, int power, int armor, float weight, int value, int count,
                              const char* description) {
    ShowcaseModule::Item it;
    it.icon = icon; it.name = name; it.rarity = rarity; it.category = category; it.equipType = equipType;
    it.power = power; it.armor = armor; it.weight = weight; it.value = value; it.count = count;
    it.description = description;
    return it;
}

} // namespace

std::vector<kke::ModuleDependency> ShowcaseModule::dependencies() const {
    return {
        { std::type_index(typeid(kke::UiModule)), true, "loads and shows RmlUi documents" },
        { std::type_index(typeid(kke::SettingsModule)), true, "the settings screen edits and applies EngineSettings" },
        { std::type_index(typeid(InputScreen)), true, "creates the 'input' data model before input.rml loads" },
    };
}

float ShowcaseModule::random01() {
    m_rng = m_rng * 1664525u + 1013904223u;
    return static_cast<float>((m_rng >> 8) & 0xFFFFFF) / static_cast<float>(0xFFFFFF);
}

// ------------------------------------------------------------------ init

void ShowcaseModule::init(kke::Application& app) {
    m_app = &app;
    m_ui = app.getModule<kke::UiModule>();
    m_settings = app.getModule<kke::SettingsModule>();
    Rml::Context* context = m_ui->context();

    // Models must exist before the documents that reference them load.
    buildNavModel();
    buildMenuModel();
    buildSettingsModel();
    buildInventoryModel();
    buildHudModel();
    buildDialogModel();
    buildLoadingModel();

    // KKE_UI_ROOT lets you point at the source folder
    // (games/rmlui_demo/ui/) so .rcss edits + F5 show up without a rebuild.
    std::string root = "ui/";
    if (const char* env = std::getenv("KKE_UI_ROOT")) {
        root = env;
        if (!root.empty() && root.back() != '/') root += '/';
        kke::log::get(name())->info("loading UI documents from KKE_UI_ROOT='{}'", root);
    }
    for (size_t i = 0; i < std::size(kScreens); ++i) {
        Rml::ElementDocument* doc = context->LoadDocument(root + kDocumentFiles[i]);
        if (!doc) {
            kke::log::get(name())->error("could not load '{}{}' -- run from build/bin so ui/ is next to the executable", root, kDocumentFiles[i]);
            continue;
        }
        m_documents[kScreens[i]] = doc;
    }
    if (auto it = m_documents.find("inventory"); it != m_documents.end()) {
        it->second->AddEventListener(Rml::EventId::Dragdrop, &m_dragListener);
    }
    m_nav = context->LoadDocument(root + "nav.rml");
    // KKE_SHOWCASE_START=<screen> opens straight on one screen — handy
    // while iterating on a single .rml file, and for screenshots.
    const char* start = std::getenv("KKE_SHOWCASE_START");
    setScreen(start && m_documents.count(start) ? start : "menu");
}

void ShowcaseModule::onGo(Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args) {
    setScreen(argString(args, 0));
}

void ShowcaseModule::setScreen(const std::string& screen) {
    if (!m_documents.count(screen)) return;
    m_screen = screen;
    for (auto& [id, doc] : m_documents) {
        if (id == screen) doc->Show();
        else doc->Hide();
    }
    if (m_nav) {
        m_nav->Show();
        m_nav->PullToFront();
    }
    if (screen == "loading") {
        m_progress = 0.0f;
        m_stepIndex = 0;
        m_loadingModel.DirtyAllVariables();
    }
    if (screen == "dialog" && m_node < 0) startDialogNode(0);
    m_navModel.DirtyVariable("screen");
}

void ShowcaseModule::buildNavModel() {
    Rml::DataModelConstructor c = m_ui->context()->CreateDataModel("nav");
    c.Bind("screen", &m_screen);
    c.Bind("ui_scale", &m_uiScaleShown);
    c.Bind("width", &m_width);
    c.Bind("height", &m_height);
    c.BindEventCallback("go", &ShowcaseModule::onGo, this);
    m_navModel = c.GetModelHandle();
}

void ShowcaseModule::buildMenuModel() {
    Rml::DataModelConstructor c = m_ui->context()->CreateDataModel("menu");
    c.Bind("version", &m_version);
    c.Bind("fps", &m_fps);
    c.Bind("show_credits", &m_showCredits);
    c.Bind("show_quit", &m_showQuit);
    c.BindEventCallback("go", &ShowcaseModule::onGo, this);
    c.BindEventCallback("quit", [](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
        SDL_Event quit{};
        quit.type = SDL_EVENT_QUIT;
        SDL_PushEvent(&quit);
    });
    m_menuModel = c.GetModelHandle();
}

// ------------------------------------------------------------------ settings

void ShowcaseModule::syncBindingsFromSettings() {
    static const std::pair<const char*, const char*> kLabels[] = {
        {"move_forward", "Move forward"}, {"move_back", "Move back"}, {"move_left", "Strafe left"},
        {"move_right", "Strafe right"}, {"jump", "Jump"}, {"interact", "Interact"},
        {"inventory", "Open inventory"}, {"pause", "Pause menu"},
    };
    m_bindings.clear();
    auto& keys = m_settings->settings().controls.keyBindings;
    for (auto& [action, label] : kLabels) {
        auto it = keys.find(action);
        m_bindings.push_back({ action, label, it != keys.end() ? it->second : "—" });
    }
}

void ShowcaseModule::buildSettingsModel() {
    Rml::DataModelConstructor c = m_ui->context()->CreateDataModel("settings");
    kke::EngineSettings& s = m_settings->settings();
    m_settingsPath = m_settings->path();
    // Bound straight to the live EngineSettings — a slider writes the
    // engine's value directly; update() notices and calls apply().
    c.Bind("fullscreen", &s.graphics.fullscreen);
    c.Bind("vsync", &s.graphics.vsync);
    c.Bind("fps_limit", &s.graphics.frameRateLimit);
    c.Bind("fov", &s.graphics.fieldOfView);
    c.Bind("shadows", &s.graphics.shadows);
    c.Bind("brightness", &s.graphics.brightness);
    c.Bind("ui_scale", &s.graphics.uiScale);
    c.Bind("debug_overlay", &s.graphics.showDebugOverlay);
    c.Bind("master", &s.audio.master);
    c.Bind("music", &s.audio.music);
    c.Bind("effects", &s.audio.effects);
    c.Bind("mute_unfocused", &s.audio.muteWhenUnfocused);
    c.Bind("sensitivity", &s.controls.mouseSensitivity);
    c.Bind("invert_y", &s.controls.invertY);
    c.Bind("difficulty", &s.gameplay.difficulty);
    c.Bind("damage_numbers", &s.gameplay.showDamageNumbers);
    c.Bind("max_steps", &s.gameplay.maxPhysicsStepsPerFrame);
    c.Bind("path", &m_settingsPath);
    c.BindFunc("dirty", [this](Rml::Variant& v) { v = m_settings->hasUnsavedChanges(); });

    if (auto b = c.RegisterStruct<Binding>()) {
        b.RegisterMember("label", &Binding::label);
        b.RegisterMember("key", &Binding::key);
    }
    c.RegisterArray<std::vector<Binding>>();
    syncBindingsFromSettings();
    c.Bind("bindings", &m_bindings);
    c.Bind("capturing", &m_capturing);

    c.BindEventCallback("rebind", [this](Rml::DataModelHandle h, Rml::Event&, const Rml::VariantList& args) {
        m_capturing = argInt(args, 0);
        h.DirtyVariable("capturing");
    });
    c.BindEventCallback("save", [this](Rml::DataModelHandle h, Rml::Event&, const Rml::VariantList&) {
        m_settings->apply();
        m_settings->save();
        h.DirtyVariable("dirty");
    });
    c.BindEventCallback("revert", [this](Rml::DataModelHandle h, Rml::Event&, const Rml::VariantList&) {
        m_settings->revert();
        syncBindingsFromSettings();
        h.DirtyAllVariables();
    });
    c.BindEventCallback("reset_defaults", [this](Rml::DataModelHandle h, Rml::Event&, const Rml::VariantList&) {
        m_settings->resetToDefaults();
        syncBindingsFromSettings();
        h.DirtyAllVariables();
    });
    c.BindEventCallback("go", &ShowcaseModule::onGo, this);
    m_settingsModel = c.GetModelHandle();
}

// ------------------------------------------------------------------ inventory

void ShowcaseModule::buildInventoryModel() {
    Rml::DataModelConstructor c = m_ui->context()->CreateDataModel("inv");
    if (auto it = c.RegisterStruct<Item>()) {
        it.RegisterMember("icon", &Item::icon);
        it.RegisterMember("name", &Item::name);
        it.RegisterMember("rarity", &Item::rarity);
        it.RegisterMember("category", &Item::category);
        it.RegisterMember("description", &Item::description);
        it.RegisterMember("count", &Item::count);
        it.RegisterMember("power", &Item::power);
        it.RegisterMember("armor", &Item::armor);
        it.RegisterMember("weight", &Item::weight);
        it.RegisterMember("value", &Item::value);
        it.RegisterMember("slot_label", &Item::slotLabel);
    }
    c.RegisterArray<std::vector<Item>>();
    c.RegisterArray<std::vector<std::string>>();

    // Emoji icons (Noto Color Emoji is the UI's fallback font) — no image
    // assets needed, and they render in color at any UI scale.
    m_bag.assign(30, Item{});
    std::vector<Item> start = {
        makeItem("⚔", "Knight's Longsword", "rare", "Weapon", "weapon", 34, 0, 4.5f, 420, 1, "Balanced for both cutting and thrusting."),
        makeItem("🏹", "Elmwood Bow", "uncommon", "Weapon", "weapon", 22, 0, 1.8f, 160, 1, "Light, quick, and quietly deadly."),
        makeItem("🪓", "Woodsman's Axe", "common", "Weapon", "weapon", 18, 0, 3.2f, 45, 1, "Chops trees. Also other things."),
        makeItem("🔱", "Tide-Caller Trident", "legendary", "Weapon", "weapon", 61, 0, 5.0f, 5200, 1, "The sea remembers who carried it."),
        makeItem("🛡", "Oak Kite Shield", "uncommon", "Armor", "offhand", 0, 18, 5.5f, 140, 1, "Scorched on one side. Nobody asks why."),
        makeItem("⛑", "Iron Helm", "common", "Armor", "head", 0, 9, 2.4f, 60, 1, "Dented, but honest."),
        makeItem("🥋", "Padded Gambeson", "common", "Armor", "chest", 0, 12, 3.8f, 75, 1, "Warm in winter. Too warm in summer."),
        makeItem("👖", "Leather Greaves", "uncommon", "Armor", "legs", 0, 8, 2.1f, 90, 1, "Supple and quiet."),
        makeItem("💍", "Ring of the Magpie", "epic", "Armor", "ring", 5, 5, 0.1f, 1800, 1, "Shinies find you."),
        makeItem("🧪", "Health Potion", "common", "Consumable", "", 0, 0, 0.3f, 25, 5, "Restores 40 health. Tastes of cherries and regret."),
        makeItem("🫙", "Mana Draught", "uncommon", "Consumable", "", 0, 0, 0.3f, 40, 3, "Restores 30 mana."),
        makeItem("🍎", "Crisp Apple", "common", "Consumable", "", 0, 0, 0.2f, 2, 8, "An apple a day."),
        makeItem("🍖", "Roast Haunch", "common", "Consumable", "", 0, 0, 1.0f, 12, 2, "Restores stamina over time."),
        makeItem("💎", "Flawless Sapphire", "epic", "Material", "", 0, 0, 0.1f, 950, 2, "Cut by someone who knew what they were doing."),
        makeItem("🪵", "Oak Plank", "common", "Material", "", 0, 0, 1.5f, 3, 12, "Building material."),
        makeItem("🔩", "Iron Bolt", "common", "Material", "", 0, 0, 0.1f, 1, 40, "Holds things together."),
        makeItem("📜", "Scroll of Recall", "rare", "Consumable", "", 0, 0, 0.1f, 300, 1, "Returns you to the last shrine."),
        makeItem("🗝", "Rusty Key", "uncommon", "Material", "", 0, 0, 0.1f, 0, 1, "Opens something. Somewhere."),
    };
    for (size_t i = 0; i < start.size(); ++i) m_bag[i] = start[i];

    const std::pair<const char*, const char*> kEquipSlots[] = {
        {"head", "Head"}, {"chest", "Chest"}, {"legs", "Legs"}, {"weapon", "Main hand"}, {"offhand", "Off hand"}, {"ring", "Ring"},
    };
    for (auto& [type, label] : kEquipSlots) {
        Item slot;
        slot.slotType = type;
        slot.slotLabel = label;
        m_equipment.push_back(slot);
    }
    recomputeWeight();

    c.Bind("bag", &m_bag);
    c.Bind("equipment", &m_equipment);
    c.Bind("filters", &m_filters);
    c.Bind("filter", &m_filter);
    c.Bind("selected", &m_selected);
    c.Bind("detail", &m_detail);
    c.Bind("weight", &m_weight);
    c.Bind("max_weight", &m_maxWeight);
    c.Bind("gold", &m_gold);
    c.Bind("toast", &m_invToast);

    c.BindEventCallback("hover", [this](Rml::DataModelHandle h, Rml::Event&, const Rml::VariantList& args) {
        if (Item* item = slotById(argString(args, 0)); item && !item->empty()) {
            m_detail = *item;
            h.DirtyVariable("detail");
        }
    });
    c.BindEventCallback("select", [this](Rml::DataModelHandle h, Rml::Event&, const Rml::VariantList& args) {
        m_selected = argString(args, 0);
        h.DirtyVariable("selected");
    });
    c.BindEventCallback("use", [this](Rml::DataModelHandle h, Rml::Event&, const Rml::VariantList& args) {
        Item* item = slotById(argString(args, 0));
        if (!item || item->category != "Consumable") return;
        showInventoryToast("Used " + item->name + (item->name == "Health Potion" ? " — +40 health" : ""));
        if (item->name == "Health Potion") m_hp = std::min<float>(static_cast<float>(m_maxHp), m_hp + 40.0f);
        if (--item->count <= 0) *item = Item{};
        recomputeWeight();
        h.DirtyVariable("bag");
        h.DirtyVariable("weight");
    });
    c.BindEventCallback("sort", [this](Rml::DataModelHandle h, Rml::Event&, const Rml::VariantList&) {
        std::stable_sort(m_bag.begin(), m_bag.end(), [](const Item& a, const Item& b) {
            if (a.empty() != b.empty()) return !a.empty();
            return rarityRank(a.rarity) > rarityRank(b.rarity);
        });
        h.DirtyVariable("bag");
        showInventoryToast("Sorted by rarity");
    });
    c.BindEventCallback("go", &ShowcaseModule::onGo, this);
    m_invModel = c.GetModelHandle();
}

ShowcaseModule::Item* ShowcaseModule::slotById(const std::string& id) {
    if (id.size() < 2) return nullptr;
    int index = std::atoi(id.c_str() + 1);
    std::vector<Item>& list = (id[0] == 'e') ? m_equipment : m_bag;
    if (index < 0 || index >= static_cast<int>(list.size())) return nullptr;
    return &list[index];
}

void ShowcaseModule::moveItem(const std::string& from, const std::string& to) {
    Item* src = slotById(from);
    Item* dst = slotById(to);
    if (!src || !dst || src == dst || src->empty()) return;

    // Equipment slots only take the matching item type, in both directions
    // of a swap.
    auto fits = [](const Item& item, const Item& slot, bool slotIsEquipment) {
        return !slotIsEquipment || item.empty() || item.equipType == slot.slotType;
    };
    bool srcIsEquip = from[0] == 'e', dstIsEquip = to[0] == 'e';
    if (!fits(*src, *dst, dstIsEquip) || !fits(*dst, *src, srcIsEquip)) {
        showInventoryToast("✖ " + src->name + " doesn't go in " + (dstIsEquip ? dst->slotLabel : std::string("that slot")));
        return;
    }
    // Same item type stacks instead of swapping.
    if (!dstIsEquip && !dst->empty() && dst->name == src->name && src->count > 0 && src->category != "Weapon" && src->category != "Armor") {
        dst->count += src->count;
        *src = Item{};
        if (srcIsEquip) { /* unreachable: equipment never stacks */ }
    } else {
        std::swap(src->icon, dst->icon); std::swap(src->name, dst->name); std::swap(src->rarity, dst->rarity);
        std::swap(src->category, dst->category); std::swap(src->equipType, dst->equipType);
        std::swap(src->description, dst->description); std::swap(src->count, dst->count);
        std::swap(src->power, dst->power); std::swap(src->armor, dst->armor);
        std::swap(src->weight, dst->weight); std::swap(src->value, dst->value);
        // slotLabel/slotType stay with the slot, not the item.
    }
    if (dstIsEquip && !dst->empty()) showInventoryToast("Equipped " + dst->name);
    recomputeWeight();
    m_invModel.DirtyVariable("bag");
    m_invModel.DirtyVariable("equipment");
    m_invModel.DirtyVariable("weight");
}

void ShowcaseModule::DragDropListener::ProcessEvent(Rml::Event& event) {
    auto* dragged = static_cast<Rml::Element*>(event.GetParameter<void*>("drag_element", nullptr));
    std::string from = slotIdOf(dragged);
    std::string to = slotIdOf(event.GetTargetElement());
    if (!from.empty() && !to.empty()) m_owner->moveItem(from, to);
}

void ShowcaseModule::recomputeWeight() {
    m_weight = 0.0f;
    for (auto* list : { &m_bag, &m_equipment }) {
        for (const Item& it : *list) m_weight += it.weight * static_cast<float>(std::max(it.count, it.empty() ? 0 : 1));
    }
}

void ShowcaseModule::showInventoryToast(const std::string& text) {
    m_invToast = text;
    m_invToastTtl = 2.5f;
    m_invModel.DirtyVariable("toast");
}

void ShowcaseModule::updateInventoryToast(float dt) {
    if (m_invToastTtl > 0.0f && (m_invToastTtl -= dt) <= 0.0f) {
        m_invToast.clear();
        m_invModel.DirtyVariable("toast");
    }
}

// ------------------------------------------------------------------ HUD

void ShowcaseModule::buildHudModel() {
    Rml::DataModelConstructor c = m_ui->context()->CreateDataModel("hud");
    if (auto a = c.RegisterStruct<Ability>()) {
        a.RegisterMember("icon", &Ability::icon);
        a.RegisterMember("cooldown", &Ability::cooldown);
        a.RegisterMember("max_cooldown", &Ability::maxCooldown);
    }
    c.RegisterArray<std::vector<Ability>>();
    if (auto b = c.RegisterStruct<Blip>()) {
        b.RegisterMember("kind", &Blip::kind);
        b.RegisterMember("x", &Blip::x);
        b.RegisterMember("y", &Blip::y);
    }
    c.RegisterArray<std::vector<Blip>>();
    if (auto q = c.RegisterStruct<Quest>()) {
        q.RegisterMember("title", &Quest::title);
        q.RegisterMember("step", &Quest::step);
        q.RegisterMember("done", &Quest::done);
    }
    c.RegisterArray<std::vector<Quest>>();
    if (auto t = c.RegisterStruct<Toast>()) {
        t.RegisterMember("text", &Toast::text);
        t.RegisterMember("kind", &Toast::kind);
    }
    c.RegisterArray<std::vector<Toast>>();
    if (auto d = c.RegisterStruct<Damage>()) {
        d.RegisterMember("amount", &Damage::amount);
        d.RegisterMember("x", &Damage::x);
        d.RegisterMember("y", &Damage::y);
        d.RegisterMember("crit", &Damage::crit);
    }
    c.RegisterArray<std::vector<Damage>>();

    m_abilities = {
        { "🗡", "Slash", 0.0f, 1.2f, 0.0f }, { "🔥", "Fireball", 0.0f, 4.0f, 20.0f }, { "🛡", "Guard", 0.0f, 8.0f, 0.0f },
        { "❄", "Frost Nova", 0.0f, 12.0f, 30.0f }, { "💚", "Mend", 0.0f, 15.0f, 25.0f }, { "🌀", "Blink", 0.0f, 6.0f, 15.0f },
    };
    m_blips = { { "enemy", 30, 35, 4, 2 }, { "enemy", 70, 62, -3, 3 }, { "enemy", 58, 28, 2, -4 }, { "quest", 78, 22, 0, 0 } };
    m_quests = {
        { "The Blacksmith's Debt", "Talk to Brunhild at the forge", false },
        { "Wolves at the Gate", "Wolves slain: 3 / 8", false },
        { "A Key to Nowhere", "Found the rusty key", true },
    };

    c.Bind("hp", &m_hp);
    c.Bind("ghost_hp", &m_ghostHp);
    c.Bind("max_hp", &m_maxHp);
    c.Bind("mp", &m_mp);
    c.Bind("max_mp", &m_maxMp);
    c.Bind("sp", &m_sp);
    c.Bind("xp", &m_xp);
    c.Bind("xp_next", &m_xpNext);
    c.Bind("level", &m_level);
    c.Bind("player_name", &m_playerName);
    c.Bind("abilities", &m_abilities);
    c.Bind("blips", &m_blips);
    c.Bind("heading", &m_heading);
    c.Bind("quests", &m_quests);
    c.Bind("toasts", &m_toasts);
    c.Bind("damage", &m_damage);
    c.Bind("hit_flash", &m_hitFlash);
    c.BindEventCallback("cast", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args) { castAbility(argInt(args, 0)); });
    c.BindEventCallback("take_damage", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) { takeHit(); });
    c.BindEventCallback("add_loot", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
        static const char* kLoot[] = { "🪙 +35 gold", "💎 Flawless Sapphire", "🧪 Health Potion ×2", "🪵 Oak Plank ×4", "📜 Scroll of Recall" };
        addToast(std::string("Looted ") + kLoot[static_cast<int>(random01() * 4.99f)], "loot");
    });
    m_hudModel = c.GetModelHandle();
}

void ShowcaseModule::addToast(const std::string& text, const std::string& kind) {
    m_toasts.push_back({ text, kind, 4.0f });
    if (m_toasts.size() > 5) m_toasts.erase(m_toasts.begin());
    m_hudModel.DirtyVariable("toasts");
}

void ShowcaseModule::castAbility(int index) {
    if (index < 0 || index >= static_cast<int>(m_abilities.size())) return;
    Ability& a = m_abilities[index];
    if (a.cooldown > 0.0f) {
        addToast(a.name + " is not ready", "warn");
        return;
    }
    if (m_mp < a.manaCost) {
        addToast("Not enough mana for " + a.name, "warn");
        return;
    }
    m_mp -= a.manaCost;
    a.cooldown = a.maxCooldown;
    if (a.name == "Mend") m_hp = std::min(static_cast<float>(m_maxHp), m_hp + 35.0f);
    if (a.name == "Slash" || a.name == "Fireball" || a.name == "Frost Nova") {
        bool crit = random01() < 0.25f;
        int amount = static_cast<int>((a.name == "Slash" ? 18 : 42) * (crit ? 2.0f : 1.0f) * (0.85f + random01() * 0.3f));
        bool show = m_settings->settings().gameplay.showDamageNumbers;
        if (show) m_damage.push_back({ amount, 44.0f + random01() * 12.0f, 38.0f + random01() * 10.0f, 1.1f, crit });
        m_xp += amount * 0.5f;
        if (m_xp >= m_xpNext) {
            m_xp -= m_xpNext;
            ++m_level;
            addToast("Level up! You are now level " + std::to_string(m_level), "xp");
        }
    }
    m_hudModel.DirtyAllVariables();
}

void ShowcaseModule::takeHit() {
    const std::string& difficulty = m_settings->settings().gameplay.difficulty;
    float scale = difficulty == "easy" ? 0.5f : difficulty == "hard" ? 1.6f : 1.0f;
    float amount = (8.0f + random01() * 14.0f) * scale;
    m_hp = std::max(0.0f, m_hp - amount);
    m_ghostDelay = 0.6f; // the pale "ghost" bar lingers, then catches up
    m_hitFlash = true;
    m_hitFlashTtl = 0.5f;
    if (m_hp <= 0.0f) {
        addToast("You were defeated — respawning", "warn");
        m_hp = static_cast<float>(m_maxHp);
        m_ghostHp = m_hp;
    }
    m_hudModel.DirtyAllVariables();
}

void ShowcaseModule::updateHud(float dt) {
    m_hudTime += dt;
    m_mp = std::min(static_cast<float>(m_maxMp), m_mp + 3.0f * dt);
    m_sp = 70.0f + 30.0f * std::sin(m_hudTime * 0.7f);
    m_hp = std::min(static_cast<float>(m_maxHp), m_hp + 1.5f * dt);
    if ((m_ghostDelay -= dt) <= 0.0f) m_ghostHp = m_hp;
    else m_ghostHp = std::max(m_ghostHp, m_hp);
    for (Ability& a : m_abilities) a.cooldown = std::max(0.0f, a.cooldown - dt);
    for (Blip& b : m_blips) {
        b.x += b.vx * dt;
        b.y += b.vy * dt;
        if (b.x < 12 || b.x > 88) b.vx = -b.vx;
        if (b.y < 12 || b.y > 88) b.vy = -b.vy;
    }
    static const char* kHeadings[] = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" };
    float yawDeg = std::fmod(m_hudTime * 12.0f, 360.0f);
    m_heading = std::string("‹ ") + kHeadings[static_cast<int>((yawDeg + 22.5f) / 45.0f) % 8] + "  " +
                std::to_string(static_cast<int>(yawDeg)) + "° ›";
    for (Toast& t : m_toasts) t.ttl -= dt;
    size_t before = m_toasts.size();
    m_toasts.erase(std::remove_if(m_toasts.begin(), m_toasts.end(), [](const Toast& t) { return t.ttl <= 0.0f; }), m_toasts.end());
    for (Damage& d : m_damage) d.ttl -= dt;
    m_damage.erase(std::remove_if(m_damage.begin(), m_damage.end(), [](const Damage& d) { return d.ttl <= 0.0f; }), m_damage.end());
    if (m_hitFlash && (m_hitFlashTtl -= dt) <= 0.0f) m_hitFlash = false;
    if ((m_nextAutoHit -= dt) <= 0.0f) {
        takeHit();
        m_nextAutoHit = 5.0f + random01() * 5.0f;
    }
    // Continuous values change every frame; lists only when they change,
    // so RmlUi doesn't rebuild the toast/damage elements (and restart
    // their entrance animations) every frame.
    for (const char* v : { "hp", "ghost_hp", "mp", "sp", "xp", "abilities", "blips", "heading", "hit_flash" }) {
        m_hudModel.DirtyVariable(v);
    }
    if (before != m_toasts.size()) m_hudModel.DirtyVariable("toasts");
    m_hudModel.DirtyVariable("damage");
}

// ------------------------------------------------------------------ dialogue & chat

void ShowcaseModule::buildDialogModel() {
    Rml::DataModelConstructor c = m_ui->context()->CreateDataModel("dialog");
    if (auto ch = c.RegisterStruct<Choice>()) {
        ch.RegisterMember("text", &Choice::text);
        ch.RegisterMember("leave", &Choice::leave);
    }
    c.RegisterArray<std::vector<Choice>>();
    if (auto m = c.RegisterStruct<ChatMessage>()) {
        m.RegisterMember("who", &ChatMessage::who);
        m.RegisterMember("text", &ChatMessage::text);
        m.RegisterMember("color", &ChatMessage::color);
        m.RegisterMember("system", &ChatMessage::system);
    }
    c.RegisterArray<std::vector<ChatMessage>>();

    m_nodes = {
        { "Brunhild", "Blacksmith of Emberfall", "👩",
          "Ah, a new face. You've the look of someone who breaks swords faster than I can make them. What brings you to my forge?",
          { { "I need my longsword repaired.", 1 }, { "Your brother says you owe him money.", 2 }, { "What's that glowing ore?", 3 }, { "Just passing through.", -1, true } } },
        { "Brunhild", "Blacksmith of Emberfall", "👩",
          "Let me see it... Mm. Notched, bent, and is that troll blood? Forty gold, and I'll have it sharper than your wit by morning.",
          { { "Deal. (Pay 40 gold)", 4 }, { "Forty? That's robbery.", 5 }, { "Never mind.", 0 } } },
        { "Brunhild", "Blacksmith of Emberfall", "👩",
          "Owe HIM? That weasel still has my good tongs. You tell Gunnar that when the tongs come home, so does his coin.",
          { { "I'll pass that along.", 0 }, { "Goodbye.", -1, true } } },
        { "Brunhild", "Blacksmith of Emberfall", "👩",
          "Starsteel. Fell out of the sky last winter, over the Hollow Keep. Burns hotter than anything I've worked. Bring me more and I'll make you something worth dying for.",
          { { "Where exactly did it fall?", 6 }, { "Back.", 0 } } },
        { "Brunhild", "Blacksmith of Emberfall", "👩",
          "Coin first, compliments later. Come back at dawn.", { { "Goodbye.", -1, true } } },
        { "Brunhild", "Blacksmith of Emberfall", "👩",
          "Robbery is the bandits on the north road. This is craftsmanship. Thirty-five, and not a copper less.",
          { { "Fine, thirty-five.", 4 }, { "I'll find another smith.", -1, true } } },
        { "Brunhild", "Blacksmith of Emberfall", "👩",
          "The old watchtower east of the keep. The wolves have been thick there since. Bring a torch — and a better sword.",
          { { "Thanks. (Quest updated)", 0 } } },
    };
    m_node = -1;
    m_messages = {
        { "", "Welcome to the showcase chat. Messages are plain data-bound text — try typing <b>markup</b>.", "#8b93aa", true },
        { "Brunhild", "Forge is open till sundown.", "#ffcf5c", false },
        { "Gunnar", "Don't believe a word she says about the tongs.", "#6fe39a", false },
    };

    c.Bind("speaker", &m_speaker);
    c.Bind("role", &m_role);
    c.Bind("portrait", &m_portrait);
    c.Bind("shown_text", &m_shownText);
    c.Bind("typing", &m_typing);
    c.Bind("choices", &m_choices);
    c.Bind("messages", &m_messages);
    c.Bind("draft", &m_draft);
    c.BindEventCallback("skip", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) { finishTyping(); });
    c.BindEventCallback("choose", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args) { chooseDialog(argInt(args, 0)); });
    c.BindEventCallback("send", [this](Rml::DataModelHandle, Rml::Event& ev, const Rml::VariantList&) {
        sendChat();
        ev.StopPropagation();
    });
    m_dialogModel = c.GetModelHandle();
}

void ShowcaseModule::startDialogNode(int node) {
    if (node < 0 || node >= static_cast<int>(m_nodes.size())) node = 0;
    m_node = node;
    const DialogNode& n = m_nodes[node];
    m_speaker = n.speaker;
    m_role = n.role;
    m_portrait = n.portrait;
    m_fullText = n.text;
    m_shownText.clear();
    m_typedBytes = 0;
    m_typeClock = 0.0f;
    m_typing = true;
    m_choices = n.choices;
    m_dialogModel.DirtyAllVariables();
}

void ShowcaseModule::finishTyping() {
    if (!m_typing) return;
    m_shownText = m_fullText;
    m_typedBytes = m_fullText.size();
    m_typing = false;
    m_dialogModel.DirtyVariable("shown_text");
    m_dialogModel.DirtyVariable("typing");
}

void ShowcaseModule::chooseDialog(int index) {
    if (m_typing || index < 0 || index >= static_cast<int>(m_choices.size())) return;
    const Choice& choice = m_choices[index];
    if (choice.leave || choice.next < 0) {
        m_messages.push_back({ "", "You leave the forge.", "#8b93aa", true });
        m_dialogModel.DirtyVariable("messages");
        startDialogNode(0);
        setScreen("hud");
        return;
    }
    if (choice.next == 4 && m_gold >= 40) m_gold -= 40;
    startDialogNode(choice.next);
}

void ShowcaseModule::sendChat() {
    // Data-bound {{ text }} is inserted as a text node by RmlUi — typed
    // "<b>" shows up literally, never as markup. This is the safe path
    // for any player-written text (see also kke/RmlTextSafety.h).
    std::string text = m_draft;
    text.erase(0, text.find_first_not_of(" \t"));
    if (text.empty()) return;
    m_messages.push_back({ "You", text, "#7a9bff", false });
    if (m_messages.size() > 60) m_messages.erase(m_messages.begin());
    m_draft.clear();
    m_npcReplyTimer = 1.2f;
    m_dialogModel.DirtyVariable("messages");
    m_dialogModel.DirtyVariable("draft");
}

void ShowcaseModule::updateDialog(float dt) {
    if (m_typing) {
        m_typeClock += dt;
        // ~45 characters/second; advance whole UTF-8 code points so
        // emoji and accents never get cut in half.
        while (m_typeClock > 1.0f / 45.0f && m_typedBytes < m_fullText.size()) {
            m_typeClock -= 1.0f / 45.0f;
            do { ++m_typedBytes; } while (m_typedBytes < m_fullText.size() && (static_cast<unsigned char>(m_fullText[m_typedBytes]) & 0xC0) == 0x80);
        }
        m_shownText = m_fullText.substr(0, m_typedBytes);
        m_dialogModel.DirtyVariable("shown_text");
        if (m_typedBytes >= m_fullText.size()) {
            m_typing = false;
            m_dialogModel.DirtyVariable("typing");
        }
    }
    if (m_npcReplyTimer > 0.0f && (m_npcReplyTimer -= dt) <= 0.0f) {
        static const char* kReplies[] = { "Aye.", "Speak up, the bellows are loud!", "Hah! Good one.", "Bring me starsteel and we'll talk." };
        m_messages.push_back({ "Brunhild", kReplies[static_cast<int>(random01() * 3.99f)], "#ffcf5c", false });
        m_dialogModel.DirtyVariable("messages");
    }
}

// ------------------------------------------------------------------ loading

void ShowcaseModule::buildLoadingModel() {
    Rml::DataModelConstructor c = m_ui->context()->CreateDataModel("loading");
    m_tips = {
        "Everything in this UI is RML + RCSS. Edit games/rmlui_demo/ui/*.rcss and press F5 to see it change live.",
        "Sizes are in dp: the whole UI scales with window height, times the UI-scale setting.",
        "Drag items onto the equipment slots in the inventory — the wrong type is refused.",
        "The settings screen writes settings.json next to the executable. Delete it to reset.",
        "Physics debris goes to sleep once it settles — watch the Physics panel's awake count.",
    };
    m_tip = m_tips[0];
    c.Bind("area", &m_area);
    c.Bind("stage", &m_stage);
    c.Bind("progress", &m_progress);
    c.Bind("steps", &m_steps);
    c.Bind("step_index", &m_stepIndex);
    c.Bind("tip", &m_tip);
    c.BindEventCallback("continue_if_ready", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
        if (m_progress >= 100.0f) setScreen("hud");
    });
    m_loadingModel = c.GetModelHandle();
}

void ShowcaseModule::updateLoading(float dt) {
    if (m_progress < 100.0f) {
        // Uneven speed per stage, like real loading.
        float speed = 14.0f + 10.0f * std::sin(static_cast<float>(m_stepIndex) * 2.1f);
        m_progress = std::min(100.0f, m_progress + speed * dt);
        int step = std::min(static_cast<int>(m_progress / 100.0f * m_steps.size()), static_cast<int>(m_steps.size()));
        if (step != m_stepIndex) {
            m_stepIndex = step;
            m_loadingModel.DirtyVariable("step_index");
        }
        m_stage = m_progress >= 100.0f ? "Ready" : (m_steps[std::min<size_t>(m_stepIndex, m_steps.size() - 1)] + "…");
        m_loadingModel.DirtyVariable("progress");
        m_loadingModel.DirtyVariable("stage");
    }
    if ((m_tipTimer += dt) > 5.0f) {
        m_tipTimer = 0.0f;
        m_tipIndex = (m_tipIndex + 1) % static_cast<int>(m_tips.size());
        m_tip = m_tips[m_tipIndex];
        m_loadingModel.DirtyVariable("tip");
    }
}

// ------------------------------------------------------------------ frame

void ShowcaseModule::update(const kke::UpdateContext& ctx) {
    float dt = std::min(ctx.dt, 0.1f);

    // Controller: B = back (like Esc), LB/RB = previous/next screen.
    if (auto* input = m_app->getModule<kke::InputModule>()) {
        const kke::InputMap& m = input->map(0);
        auto* is = m_app->getModule<InputScreen>();
        if (!(is && is->capturing())) {
            if (m.pressed("ui.back")) {
                if (m_showCredits || m_showQuit) {
                    m_showCredits = m_showQuit = false;
                    m_menuModel.DirtyAllVariables();
                } else {
                    setScreen("menu");
                }
            }
            const int n = static_cast<int>(std::size(kScreens));
            int cur = 0;
            while (cur < n && m_screen != kScreens[cur]) ++cur;
            if (m.pressed("ui.next")) setScreen(kScreens[(cur + 1) % n]);
            if (m.pressed("ui.prev")) setScreen(kScreens[(cur + n - 1) % n]);
        }
    }

    // Live preview: anything a settings control changed gets applied now.
    static kke::EngineSettings lastApplied = m_settings->settings();
    if (m_settings->settings() != lastApplied) {
        m_settings->apply();
        lastApplied = m_settings->settings();
        m_settingsModel.DirtyVariable("dirty");
    }

    // nav: scale readout
    VkExtent2D extent = m_app->renderer().extent();
    if (static_cast<int>(extent.width) != m_width || static_cast<int>(extent.height) != m_height ||
        std::abs(m_uiScaleShown - m_ui->dpRatio()) > 1e-3f) {
        m_width = static_cast<int>(extent.width);
        m_height = static_cast<int>(extent.height);
        m_uiScaleShown = m_ui->dpRatio();
        m_navModel.DirtyVariable("width");
        m_navModel.DirtyVariable("height");
        m_navModel.DirtyVariable("ui_scale");
    }
    m_fpsAccum += ctx.dt;
    ++m_fpsFrames;
    if (m_fpsAccum >= 0.5f) {
        m_fps = static_cast<int>(m_fpsFrames / m_fpsAccum + 0.5f);
        m_fpsAccum = 0.0f;
        m_fpsFrames = 0;
        m_menuModel.DirtyVariable("fps");
    }

    if (m_screen == "hud") updateHud(dt);
    if (m_screen == "dialog") updateDialog(dt);
    if (m_screen == "loading") updateLoading(dt);
    updateInventoryToast(dt);
}

void ShowcaseModule::onEvent(const SDL_Event& event) {
    if (event.type != SDL_EVENT_KEY_DOWN || event.key.repeat) return;
    SDL_Keycode key = event.key.key;

    // Rebinding a key: the next key press (anything but Esc) becomes the binding.
    if (m_capturing >= 0 && m_screen == "settings") {
        if (key != SDLK_ESCAPE && m_capturing < static_cast<int>(m_bindings.size())) {
            std::string keyName = SDL_GetKeyName(key);
            m_bindings[m_capturing].key = keyName;
            m_settings->settings().controls.keyBindings[m_bindings[m_capturing].action] = keyName;
        }
        m_capturing = -1;
        m_settingsModel.DirtyVariable("bindings");
        m_settingsModel.DirtyVariable("capturing");
        return;
    }

    if (key == SDLK_F1) {
        auto& s = m_settings->settings();
        s.graphics.showDebugOverlay = !s.graphics.showDebugOverlay;
        m_settingsModel.DirtyVariable("debug_overlay");
        return;
    }
    if (key == SDLK_ESCAPE) {
        // Esc while rebinding cancels the capture, not the screen.
        if (auto* is = m_app->getModule<InputScreen>(); is && is->capturing()) return;
        if (m_showCredits || m_showQuit) {
            m_showCredits = m_showQuit = false;
            m_menuModel.DirtyAllVariables();
        } else {
            setScreen("menu");
        }
        return;
    }
    if (m_screen == "hud" && key >= SDLK_1 && key <= SDLK_6) castAbility(static_cast<int>(key - SDLK_1));
    if (m_screen == "dialog") {
        bool typingInChat = SDL_TextInputActive(m_app->window().handle());
        // RmlUi doesn't submit a <form> on Enter in a text field; do it here.
        if (typingInChat && (key == SDLK_RETURN || key == SDLK_KP_ENTER)) {
            sendChat();
            return;
        }
        if (!typingInChat && key == SDLK_SPACE) finishTyping();
        if (!typingInChat && key >= SDLK_1 && key <= SDLK_9) chooseDialog(static_cast<int>(key - SDLK_1));
    }
    if (m_screen == "loading" && m_progress >= 100.0f) setScreen("hud");
}

void ShowcaseModule::shutdown() {
    if (auto it = m_documents.find("inventory"); it != m_documents.end()) {
        it->second->RemoveEventListener(Rml::EventId::Dragdrop, &m_dragListener); // see BUGS.md BUG-025
    }
    for (auto& [id, doc] : m_documents) doc->Close();
    m_documents.clear();
    if (m_nav) m_nav->Close();
    m_nav = nullptr;
}

} // namespace kke_demo
