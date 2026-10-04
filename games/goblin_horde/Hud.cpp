// The HUD (RmlUi, ui/horde_hud.rml): a panel per player at this screen in
// their part of the split screen (health, stamina, weapon, the bow's draw,
// a crosshair while aiming), the wave and the goblins left, a boss's
// health, a banner between waves, the controls; and the inventory, where a
// player swaps weapons without pausing anyone else.

#include "HordeModule.h"

#include "kke/Application.h"
#include "kke/Log.h"
#include "kke/Viewports.h"
#include "kke/modules/InputModule.h"
#include "kke/modules/UiModule.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/ElementDocument.h>
#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace horde {

namespace {

std::string percent(float fraction) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%.1f%%", static_cast<double>(std::clamp(fraction, 0.0f, 1.0f) * 100.0f));
    return buf;
}

std::string hexColor(const glm::vec3& c) {
    char buf[16];
    auto b = [](float v) { return static_cast<int>(std::round(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
    std::snprintf(buf, sizeof(buf), "#%02x%02x%02x", b(c.r), b(c.g), b(c.b));
    return buf;
}

const char* kindName(Weapon::Kind k) {
    switch (k) {
    case Weapon::Kind::OneHand: return "One-handed";
    case Weapon::Kind::TwoHand: return "Two-handed";
    case Weapon::Kind::Bow: return "Bow";
    case Weapon::Kind::Crossbow: return "Crossbow";
    }
    return "";
}

// What a player needs to know about each weapon's quirks.
const char* aboutWeapon(Weapon::Kind k) {
    switch (k) {
    case Weapon::Kind::OneHand: return "Quick three-hit combo. Heavy: a big swing. Hold heavy: spin and hit all round.";
    case Weapon::Kind::TwoHand: return "Slow, wide combo that hits hard. Heavy can't be blocked. Hold heavy: a wider spin.";
    case Weapon::Kind::Bow: return "Hold to draw: the longer, the harder and faster. Aim for the head. Heavy: a kick.";
    case Weapon::Kind::Crossbow: return "Every bolt hits the same, aimed or not, then a slow reload. Heavy: a kick.";
    }
    return "";
}

} // namespace

void HordeModule::buildHud() {
    auto* ui = m_app->getModule<kke::UiModule>();
    if (!ui || !ui->context()) return;
    Rml::Context* ctx = ui->context();
    Rml::DataModelConstructor c = ctx->CreateDataModel("horde");
    if (!c) return;
    if (auto p = c.RegisterStruct<PlayerHud>()) {
        p.RegisterMember("name", &PlayerHud::name);
        p.RegisterMember("health", &PlayerHud::health);
        p.RegisterMember("stamina", &PlayerHud::stamina);
        p.RegisterMember("weapon", &PlayerHud::weapon);
        p.RegisterMember("ammo", &PlayerHud::ammo);
        p.RegisterMember("accent", &PlayerHud::accent);
        p.RegisterMember("charge", &PlayerHud::charge);
        p.RegisterMember("status", &PlayerHud::status);
        p.RegisterMember("low", &PlayerHud::low);
        p.RegisterMember("aiming", &PlayerHud::aiming);
        p.RegisterMember("charged", &PlayerHud::charged);
        p.RegisterMember("down", &PlayerHud::down);
        p.RegisterMember("drawing", &PlayerHud::drawing);
        p.RegisterMember("x", &PlayerHud::x);
        p.RegisterMember("y", &PlayerHud::y);
        p.RegisterMember("w", &PlayerHud::w);
        p.RegisterMember("h", &PlayerHud::h);
    }
    c.RegisterArray<std::vector<PlayerHud>>();
    if (auto r = c.RegisterStruct<InventoryRow>()) {
        r.RegisterMember("label", &InventoryRow::label);
        r.RegisterMember("kind", &InventoryRow::kind);
        r.RegisterMember("line", &InventoryRow::line);
        r.RegisterMember("focused", &InventoryRow::focused);
        r.RegisterMember("equipped", &InventoryRow::equipped);
    }
    c.RegisterArray<std::vector<InventoryRow>>();
    c.Bind("players", &m_hud.players);
    c.Bind("wave", &m_hud.wave);
    c.Bind("left", &m_hud.left);
    c.Bind("kills", &m_hud.kills);
    c.Bind("banner", &m_hud.banner);
    c.Bind("sub", &m_hud.sub);
    c.Bind("hint", &m_hud.hint);
    c.Bind("boss", &m_hud.boss);
    c.Bind("boss_health", &m_hud.bossHealth);
    c.Bind("boss_shown", &m_hud.bossShown);
    c.Bind("inventory", &m_hud.inventory);
    c.Bind("rows", &m_hud.rows);
    c.Bind("inventory_title", &m_hud.inventoryTitle);
    c.Bind("inventory_hint", &m_hud.inventoryHint);
    c.Bind("inv_x", &m_hud.invX);
    c.Bind("inv_y", &m_hud.invY);
    c.Bind("inv_w", &m_hud.invW);
    c.Bind("inv_h", &m_hud.invH);
    m_hudModel = c.GetModelHandle();

    const char* base = SDL_GetBasePath();
    const std::string path = std::string(base ? base : "") + "ui/horde_hud.rml";
    m_hudDoc = ctx->LoadDocument(path);
    if (!m_hudDoc) {
        kke::log::get(name())->error("HUD: could not load {}", path);
        return;
    }
    m_hudDoc->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
}

void HordeModule::openInventory(int hero) {
    if (hero < 0 || hero >= static_cast<int>(m_heroes.size()) || m_heroes[static_cast<size_t>(hero)]->remote) return;
    if (m_inventoryHero == hero) {
        m_inventoryHero = -1;
        return;
    }
    m_inventoryHero = hero;
    m_inventoryRow = m_heroes[static_cast<size_t>(hero)]->weapon;
    m_inventoryFresh = true;
}

// The inventory reads its player's menu buttons; everyone else plays on.
void HordeModule::updateInventory() {
    if (m_inventoryHero >= static_cast<int>(m_heroes.size()) || m_phase == Phase::Lobby) m_inventoryHero = -1;
    if (m_inventoryHero < 0) return;
    Hero& h = *m_heroes[static_cast<size_t>(m_inventoryHero)];
    if (m_inventoryFresh) {
        m_inventoryFresh = false;
        return;
    }
    kke::InputMap& in = m_input->map(h.player);
    const int n = static_cast<int>(m_roster.weapons.size());
    if (in.pressed("horde.up")) m_inventoryRow = (m_inventoryRow + n - 1) % n;
    if (in.pressed("horde.down")) m_inventoryRow = (m_inventoryRow + 1) % n;
    if (in.pressed("horde.accept")) {
        equip(h, m_inventoryRow);
        m_inventoryHero = -1;
    } else if (in.pressed("horde.back") || in.pressed("horde.inventory")) {
        m_inventoryHero = -1;
    }
}

void HordeModule::updateHud() {
    if (!m_hudModel) return;
    auto set = [this](std::string& field, const std::string& value, const char* var) {
        if (field == value) return;
        field = value;
        m_hudModel.DirtyVariable(var);
    };
    auto setBool = [this](bool& field, bool value, const char* var) {
        if (field == value) return;
        field = value;
        m_hudModel.DirtyVariable(var);
    };
    const kke::InputModule* input = m_input;
    auto prompt = [input](const std::string& text, int player) { return input ? input->promptText(text, player) : text; };

    // A panel per player here, in their part of the screen.
    std::vector<Hero*> here;
    for (auto& h : m_heroes)
        if (!h->remote) here.push_back(h.get());
    std::sort(here.begin(), here.end(), [](const Hero* a, const Hero* b) { return a->slot < b->slot; });
    const std::vector<kke::ViewRect> rects = kke::splitScreen(std::max(1, static_cast<int>(here.size())), true);
    std::vector<PlayerHud> players;
    const bool playing = m_phase != Phase::Lobby;
    for (size_t i = 0; playing && i < here.size(); ++i) {
        const Hero& h = *here[i];
        const kke::Combatant& c = m_combat.get(h.id);
        const Weapon& w = weaponOf(h);
        PlayerHud p;
        p.name = h.name;
        p.health = percent(c.healthFraction());
        p.stamina = percent(c.staminaFraction());
        p.low = c.alive() && c.healthFraction() < 0.25f;
        p.down = !c.alive();
        p.accent = hexColor(h.color);
        p.weapon = w.label;
        p.aiming = h.aiming && c.alive();
        p.drawing = h.drawing;
        p.charged = h.charged;
        if (w.kind == Weapon::Kind::Bow) {
            const float k = h.drawing ? std::min(1.0f, h.draw / std::max(0.1f, w.draw)) : 0.0f;
            p.charge = percent(k);
            p.ammo = h.drawing ? (k >= 1.0f ? "Full draw" : "Drawing") : "";
        } else if (w.kind == Weapon::Kind::Crossbow) {
            p.charge = percent(h.reload > 0.0f ? 1.0f - h.reload / std::max(0.1f, w.reload) : 1.0f);
            p.ammo = h.reload > 0.0f ? "Reloading" : "Loaded";
        } else {
            const float k = h.heavyHeld >= 0.0f ? std::min(1.0f, h.heavyHeld / std::max(0.2f, w.spin.charge)) : 0.0f;
            p.charge = percent(k);
            p.ammo = h.charged ? "Spin ready: let go" : h.combo > 0 && h.sinceSwing < 0.9f ? "Combo " + std::to_string(h.combo) + "/3" : "";
        }
        p.status = p.down ? "Down: back next wave" : "";
        const kke::ViewRect r = rects[std::min(i, rects.size() - 1)];
        p.x = percent(r.x);
        p.y = percent(r.y);
        p.w = percent(r.w);
        p.h = percent(r.h);
        players.push_back(p);
    }
    bool changed = players.size() != m_hud.players.size();
    for (size_t i = 0; !changed && i < players.size(); ++i) {
        const PlayerHud &a = players[i], &b = m_hud.players[i];
        changed = a.name != b.name || a.health != b.health || a.stamina != b.stamina || a.weapon != b.weapon || a.ammo != b.ammo || a.accent != b.accent ||
                  a.charge != b.charge || a.status != b.status || a.low != b.low || a.aiming != b.aiming || a.charged != b.charged || a.down != b.down ||
                  a.drawing != b.drawing || a.x != b.x || a.y != b.y || a.w != b.w || a.h != b.h;
    }
    if (changed) {
        m_hud.players = std::move(players);
        m_hudModel.DirtyVariable("players");
    }

    const int alive = aliveFoes();
    set(m_hud.wave, playing ? "Wave " + std::to_string(m_wave) : "", "wave");
    set(m_hud.left, playing ? std::to_string(alive + m_toSpawn) + " goblins left" : "", "left");
    set(m_hud.kills, playing ? std::to_string(m_kills) + " slain" : "", "kills");

    // A boss up: its name and health across the top.
    const Foe* boss = nullptr;
    for (const auto& f : m_foes)
        if (!f->dead && f->type && f->type->boss) boss = f.get();
    setBool(m_hud.bossShown, boss != nullptr, "boss_shown");
    if (boss) {
        set(m_hud.boss, boss->type->label, "boss");
        set(m_hud.bossHealth, percent(boss->health), "boss_health");
    }

    std::string banner, sub;
    switch (m_phase) {
    case Phase::Lobby:
        break;
    case Phase::Intro:
        banner = "Wave " + std::to_string(m_wave);
        sub = std::to_string(m_waveSize) + " goblins are coming";
        if (m_bossToSpawn >= 0 && m_bossToSpawn < static_cast<int>(m_roster.bosses.size()))
            sub += ", and " + m_roster.bosses[static_cast<size_t>(m_bossToSpawn)].label;
        break;
    case Phase::Fighting:
        if (m_morale < 0.15f && alive > 0 && !boss) banner = "They're breaking!";
        break;
    case Phase::Cleared:
        banner = "Wave " + std::to_string(m_wave) + " beaten";
        sub = "Catch your breath";
        break;
    case Phase::Overrun:
        banner = "Overrun";
        sub = "Wave " + std::to_string(m_wave) + ", " + std::to_string(m_kills) + " goblins slain." + (netClient() ? " The host starts again." : " {horde.again} to try again");
        break;
    }
    set(m_hud.banner, banner, "banner");
    set(m_hud.sub, prompt(sub, here.empty() ? 0 : here.front()->player), "sub");

    // The controls (for the first player here, on their device).
    std::string hint;
    if (playing && !here.empty() && m_phase != Phase::Overrun) {
        const Hero& h = *here.front();
        const bool keyboard = !input || input->promptStyle(h.player) == kke::PromptStyle::Keyboard;
        const Weapon& w = weaponOf(h);
        std::string moves;
        if (w.kind == Weapon::Kind::Bow) moves = "{horde.aim} aim  ·  hold {horde.attack} draw, let go to shoot  ·  {horde.heavy} kick";
        else if (w.kind == Weapon::Kind::Crossbow) moves = "{horde.aim} aim  ·  {horde.attack} shoot  ·  {horde.heavy} kick";
        else moves = "{horde.attack} combo  ·  {horde.heavy} heavy (hold: spin)  ·  {horde.block} block";
        hint = std::string(keyboard && !m_captured && h.slot == 0 ? "{mouse:left} take the mouse  ·  " : "") + "{move} move  ·  " + moves +
               "  ·  {horde.roll} roll  ·  {horde.swap} next weapon  ·  {horde.inventory} inventory";
        hint = prompt(hint, h.player);
    }
    set(m_hud.hint, hint, "hint");

    // The inventory, over its player's part of the screen.
    const bool open = m_inventoryHero >= 0;
    setBool(m_hud.inventory, open, "inventory");
    if (open) {
        const Hero& h = *m_heroes[static_cast<size_t>(m_inventoryHero)];
        std::vector<InventoryRow> rows;
        for (size_t i = 0; i < m_roster.weapons.size(); ++i) {
            const Weapon& w = m_roster.weapons[i];
            InventoryRow r;
            r.label = w.label;
            r.kind = kindName(w.kind);
            r.line = aboutWeapon(w.kind);
            r.focused = static_cast<int>(i) == m_inventoryRow;
            r.equipped = static_cast<int>(i) == h.weapon;
            rows.push_back(r);
        }
        bool rowsChanged = rows.size() != m_hud.rows.size();
        for (size_t i = 0; !rowsChanged && i < rows.size(); ++i)
            rowsChanged = rows[i].label != m_hud.rows[i].label || rows[i].focused != m_hud.rows[i].focused || rows[i].equipped != m_hud.rows[i].equipped;
        if (rowsChanged) {
            m_hud.rows = std::move(rows);
            m_hudModel.DirtyVariable("rows");
        }
        set(m_hud.inventoryTitle, "Inventory: " + h.name, "inventory_title");
        set(m_hud.inventoryHint, prompt("{horde.up} {horde.down} choose  ·  {horde.accept} equip  ·  {horde.back} close", h.player), "inventory_hint");
        size_t i = 0;
        while (i < here.size() && here[i] != &h) ++i;
        const kke::ViewRect r = rects[std::min(i, rects.size() - 1)];
        set(m_hud.invX, percent(r.x), "inv_x");
        set(m_hud.invY, percent(r.y), "inv_y");
        set(m_hud.invW, percent(r.w), "inv_w");
        set(m_hud.invH, percent(r.h), "inv_h");
    }
}

} // namespace horde
