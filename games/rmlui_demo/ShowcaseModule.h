#pragma once

#include "kke/Module.h"

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/EventListener.h>

#include <map>
#include <string>
#include <vector>

namespace Rml {
class ElementDocument;
}

namespace kke {
class SettingsModule;
class UiModule;
}

namespace kke_demo {

// The RmlUi showcase: six screens (main menu, settings, inventory, HUD,
// dialogue/chat, loading) plus a nav bar, each a plain .rml/.rcss file
// in games/rmlui_demo/ui/ bound to C++ state through RmlUi data models.
// The documents hold all the layout and styling — this class only owns
// data and reacts to events, which is the split a game built on the
// engine should copy: artists and modders edit RML/RCSS (F5 reloads
// styles live), programmers own the model.
//
// What each screen demonstrates is listed at the top of its .rml file's
// <style> block and in README "The demo suite".
class ShowcaseModule : public kke::Module {
public:
    const char* name() const override { return "UiShowcase"; }
    std::vector<kke::ModuleDependency> dependencies() const override;

    void init(kke::Application& app) override;
    void update(const kke::UpdateContext& ctx) override;
    void onEvent(const SDL_Event& event) override;
    void shutdown() override;

    // --- Data-model types (public so RmlUi's member pointers can reach them)
    struct Item {
        std::string icon, name, rarity, category, equipType, description;
        int count = 0, power = 0, armor = 0, value = 0;
        float weight = 0.0f;
        std::string slotLabel, slotType; // equipment slots only
        bool empty() const { return icon.empty(); }
    };
    struct Binding { std::string action, label, key; };
    struct Ability { std::string icon, name; float cooldown = 0.0f, maxCooldown = 1.0f; float manaCost = 0.0f; };
    struct Blip { std::string kind; float x = 50.0f, y = 50.0f, vx = 0.0f, vy = 0.0f; };
    struct Quest { std::string title, step; bool done = false; };
    struct Toast { std::string text, kind; float ttl = 4.0f; };
    struct Damage { int amount = 0; float x = 50.0f, y = 45.0f, ttl = 1.1f; bool crit = false; };
    struct Choice { std::string text; int next = -1; bool leave = false; };
    struct ChatMessage { std::string who, text, color; bool system = false; };

private:
    void setScreen(const std::string& screen);
    void onGo(Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args);

    void buildNavModel();
    void buildMenuModel();
    void buildSettingsModel();
    void buildInventoryModel();
    void buildHudModel();
    void buildDialogModel();
    void buildLoadingModel();

    void updateHud(float dt);
    void updateDialog(float dt);
    void updateLoading(float dt);
    void updateInventoryToast(float dt);

    // Inventory helpers. Slot ids are "b<N>" (bag) or "e<N>" (equipment).
    Item* slotById(const std::string& id);
    void moveItem(const std::string& from, const std::string& to);
    void recomputeWeight();
    void showInventoryToast(const std::string& text);

    // HUD helpers
    void castAbility(int index);
    void takeHit();
    void addToast(const std::string& text, const std::string& kind);

    // Dialogue helpers
    void startDialogNode(int node);
    void chooseDialog(int index);
    void finishTyping();
    void sendChat();

    // Settings helpers
    void syncBindingsFromSettings();

    kke::Application* m_app = nullptr;
    kke::UiModule* m_ui = nullptr;
    kke::SettingsModule* m_settings = nullptr;
    std::map<std::string, Rml::ElementDocument*> m_documents;
    Rml::ElementDocument* m_nav = nullptr;
    std::string m_screen = "menu";
    std::string m_lastUiScaleShown;

    // nav/menu
    Rml::DataModelHandle m_navModel, m_menuModel;
    float m_uiScaleShown = 1.0f;
    int m_width = 0, m_height = 0;
    std::string m_version = "0.3-showcase";
    int m_fps = 0;
    float m_fpsAccum = 0.0f;
    int m_fpsFrames = 0;
    bool m_showCredits = false, m_showQuit = false;

    // settings
    Rml::DataModelHandle m_settingsModel;
    std::vector<Binding> m_bindings;
    int m_capturing = -1;
    std::string m_settingsPath;

    // inventory
    Rml::DataModelHandle m_invModel;
    std::vector<Item> m_bag, m_equipment;
    std::vector<std::string> m_filters{ "All", "Weapon", "Armor", "Consumable", "Material" };
    std::string m_filter = "All", m_selected;
    Item m_detail;
    float m_weight = 0.0f, m_maxWeight = 80.0f;
    int m_gold = 1250;
    std::string m_invToast;
    float m_invToastTtl = 0.0f;
    class DragDropListener : public Rml::EventListener {
    public:
        explicit DragDropListener(ShowcaseModule* owner) : m_owner(owner) {}
        void ProcessEvent(Rml::Event& event) override;
    private:
        ShowcaseModule* m_owner;
    };
    DragDropListener m_dragListener{ this };

    // HUD
    Rml::DataModelHandle m_hudModel;
    float m_hp = 82.0f, m_ghostHp = 82.0f, m_mp = 60.0f, m_sp = 100.0f, m_xp = 340.0f;
    int m_maxHp = 120, m_maxMp = 80, m_xpNext = 500, m_level = 7;
    std::string m_playerName = "Aria";
    std::vector<Ability> m_abilities;
    std::vector<Blip> m_blips;
    std::vector<Quest> m_quests;
    std::vector<Toast> m_toasts;
    std::vector<Damage> m_damage;
    std::string m_heading = "N";
    bool m_hitFlash = false;
    float m_hitFlashTtl = 0.0f, m_nextAutoHit = 4.0f, m_ghostDelay = 0.0f;
    float m_hudTime = 0.0f;
    uint32_t m_rng = 12345;
    float random01();

    // dialogue / chat
    Rml::DataModelHandle m_dialogModel;
    struct DialogNode { std::string speaker, role, portrait, text; std::vector<Choice> choices; };
    std::vector<DialogNode> m_nodes;
    int m_node = 0;
    std::string m_speaker, m_role, m_portrait, m_fullText, m_shownText;
    std::vector<Choice> m_choices;
    bool m_typing = false;
    float m_typeClock = 0.0f;
    size_t m_typedBytes = 0;
    std::vector<ChatMessage> m_messages;
    std::string m_draft;
    float m_npcReplyTimer = -1.0f;

    // loading
    Rml::DataModelHandle m_loadingModel;
    std::string m_area = "The Hollow Keep", m_stage, m_tip;
    float m_progress = 0.0f, m_tipTimer = 0.0f;
    int m_stepIndex = 0, m_tipIndex = 0;
    std::vector<std::string> m_steps{ "Reading world manifest", "Streaming terrain chunks", "Loading Synty props",
                                      "Compiling shaders", "Baking physics islands", "Spawning NPCs" };
    std::vector<std::string> m_tips;
};

} // namespace kke_demo
