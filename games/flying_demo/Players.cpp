// Players (README.md "Controllers" and "The start menu"): the actions and
// their default bindings for a gamepad, the keyboard and mouse, and a
// flight stick; the lobby; reading each player's controls; and the pause
// menu, where a player moves to another controller, but never to one
// another player holds.

#include "FlyingModule.h"

#include "kke/DevTools.h"
#include "kke/Log.h"
#include "kke/modules/GameShellModule.h"
#include "kke/modules/InputModule.h"
#include "kke/modules/LobbyModule.h"
#include "kke/modules/NetModule.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iterator>

namespace flying {

namespace {

using IM = kke::InputModule;

const char* const kNames[] = { "Skye", "Juno", "Rocket", "Kestrel", "Nova", "Blaze", "Comet", "Swift", "Talon", "Echo", "Zephyr", "Orbit" };
struct Colour {
    const char* name;
    glm::vec3 rgb;
};
const Colour kColours[] = { { "Sky", { 0.25f, 0.66f, 1.0f } },  { "Ember", { 1.0f, 0.42f, 0.24f } }, { "Lime", { 0.56f, 0.88f, 0.29f } },
                            { "Plum", { 0.69f, 0.42f, 1.0f } },  { "Sun", { 1.0f, 0.82f, 0.25f } },   { "Rose", { 1.0f, 0.37f, 0.64f } },
                            { "Teal", { 0.18f, 0.83f, 0.75f } }, { "Snow", { 0.95f, 0.96f, 0.97f } } };
const char* const kPaints[] = { "Red Baron", "Sunburst", "Midnight", "Racer" };
// The islands: seeds the tests fly every CPU pilot round (tests/test_flight.cpp).
struct IslandPick {
    const char* name;
    uint32_t seed;
};
const IslandPick kIslands[] = { { "Palm Key", 1 },     { "Twin Bays", 2 },
                                { "Gull Rock", 3 },    { "Harbour Isle", 11 },
                                { "Cloud Cape", 42 },  { "Canyon", mapSeed(Map::Canyon, 7) },
                                { "Mega City", mapSeed(Map::City, 9) } };
constexpr int kIslandCount = static_cast<int>(sizeof(kIslands) / sizeof(kIslands[0]));
struct SkyPick {
    const char* name;
    const char* mood;
};
const SkyPick kSkies[] = { { "Day", "clear_day" }, { "Morning", "morning" }, { "Golden hour", "golden_hour" }, { "Sunset", "sunset" }, { "Stormy", "stormy" } };
const char* const kModeNames[] = { "Race", "Stunts", "Free flight", "Dogfight" };

void axisKeys(kke::InputMap& in, const char* action, SDL_Scancode plus, SDL_Scancode minus) {
    kke::Binding p = IM::bind(action, IM::key(plus), kke::Trigger::Continuous);
    kke::Binding m = IM::bind(action, IM::key(minus), kke::Trigger::Continuous);
    m.scale = -1.0f;
    in.addBinding(p);
    in.addBinding(m);
}
void padStick(kke::InputMap& in, const char* action, SDL_GamepadAxis axis, float scale, float deadzone = 0.12f) {
    kke::Binding b = IM::bind(action, IM::padAxis(axis), kke::Trigger::Continuous);
    b.scale = scale;
    b.deadzone = deadzone;
    in.addBinding(b);
}
kke::InputSource joyAxis(int axis, int8_t half = 0) { return { kke::SourceKind::JoyAxis, 0, axis, half }; }
kke::InputSource joyButton(int button) { return { kke::SourceKind::JoyButton, 0, button, 0 }; }
kke::InputSource joyHat(int direction) { return { kke::SourceKind::JoyHat, 0, direction, 0 }; }

} // namespace

// Every player's map gets the same actions; each listens only to its own
// devices (kke::LobbyModule::applyInput). Flight stick axes are separate
// actions ("stick.*"): SDL shows a gamepad as a joystick too, so they are
// only read for a player whose device really is a stick.
void FlyingModule::defineActions() {
    for (int p = 0; p < kke::Lobby::kMaxSeats; ++p) {
        m_input->setPlayers(p + 1);
        kke::InputMap& in = m_input->map(p);
        auto def = [&](const char* id, const char* label, const char* cat, kke::ActionType t = kke::ActionType::Button, bool clamp = true) {
            in.defineAction({ id, label, cat, "game", t, clamp });
        };
        def("fly.pitch", "Pitch (nose up +)", "Flying", kke::ActionType::Axis1D);
        def("fly.roll", "Roll (right +)", "Flying", kke::ActionType::Axis1D);
        def("fly.yaw", "Rudder (right +)", "Flying", kke::ActionType::Axis1D);
        def("fly.throttle.up", "Throttle up", "Flying", kke::ActionType::Axis1D);
        def("fly.throttle.down", "Throttle down", "Flying", kke::ActionType::Axis1D);
        def("fly.smoke", "Smoke on / off", "Flying");
        def("fly.fire", "Fire the guns (Dogfight)", "Flying");
        def("fly.brake", "Wheel brakes", "Flying");
        def("fly.camera", "Camera: chase, cockpit, far", "Camera");
        def("fly.look", "Look around (stick, hat)", "Camera", kke::ActionType::Axis2D);
        def("fly.mouse", "Mouse flying (virtual stick)", "Flying", kke::ActionType::Axis2D, false);
        def("fly.mouselook", "Hold: the mouse looks around", "Camera");
        def("fly.pause", "Pause (and change controller)", "Game");
        def("fly.again", "Fly again (results)", "Game");
        def("fly.menu", "Back to the start menu (results)", "Game");
        def("stick.pitch", "Flight stick: pitch", "Flight stick", kke::ActionType::Axis1D);
        def("stick.roll", "Flight stick: roll", "Flight stick", kke::ActionType::Axis1D);
        def("stick.yaw", "Flight stick: twist rudder", "Flight stick", kke::ActionType::Axis1D);
        def("stick.throttle", "Flight stick: throttle lever", "Flight stick", kke::ActionType::Axis1D);
        // A gamepad is a joystick to SDL too, so a stick's buttons and hat
        // are actions of their own, read only for a player on a stick
        // (pressedBy / heldBy): button 4 of a pad is not "pause".
        def("stick.smoke", "Flight stick: smoke", "Flight stick");
        def("stick.fire", "Flight stick: fire (Dogfight)", "Flight stick");
        def("stick.camera", "Flight stick: camera", "Flight stick");
        def("stick.brake", "Flight stick: wheel brakes", "Flight stick");
        def("stick.pause", "Flight stick: pause", "Flight stick");
        def("stick.again", "Flight stick: fly again", "Flight stick");
        def("stick.look", "Flight stick: look around (hat)", "Flight stick", kke::ActionType::Axis2D);
        for (const char* nav : { "up", "down", "left", "right" }) def((std::string("stick.") + nav).c_str(), "Flight stick: menu", "Flight stick");
        def("stick.accept", "Flight stick: menu choose", "Flight stick");
        def("stick.back", "Flight stick: menu back", "Flight stick");
        // The pause menu, from whichever device opened it.
        def("pause.up", "Pause menu: up", "Menus");
        def("pause.down", "Pause menu: down", "Menus");
        def("pause.left", "Pause menu: left", "Menus");
        def("pause.right", "Pause menu: right", "Menus");
        def("pause.accept", "Pause menu: choose", "Menus");
        def("pause.back", "Pause menu: back", "Menus");
        def("panels", "Developer panels", "Game");

        // Gamepad: left stick flies (pull back: nose up), bumpers the
        // rudder, triggers the throttle, A smoke (in a dogfight A or B
        // fire), X brakes, Y camera, right stick looks, Start pauses.
        padStick(in, "fly.pitch", SDL_GAMEPAD_AXIS_LEFTY, 1.0f);
        padStick(in, "fly.roll", SDL_GAMEPAD_AXIS_LEFTX, 1.0f);
        in.addBinding(IM::bind("fly.yaw", IM::pad(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER), kke::Trigger::Continuous));
        {
            kke::Binding b = IM::bind("fly.yaw", IM::pad(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER), kke::Trigger::Continuous);
            b.scale = -1.0f;
            in.addBinding(b);
        }
        {
            kke::Binding up = IM::bind("fly.throttle.up", IM::padAxis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 1), kke::Trigger::Continuous);
            kke::Binding down = IM::bind("fly.throttle.down", IM::padAxis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 1), kke::Trigger::Continuous);
            up.deadzone = down.deadzone = 0.05f;
            in.addBinding(up);
            in.addBinding(down);
        }
        in.addBinding(IM::bind("fly.smoke", IM::pad(SDL_GAMEPAD_BUTTON_SOUTH)));
        in.addBinding(IM::bind("fly.fire", IM::pad(SDL_GAMEPAD_BUTTON_SOUTH), kke::Trigger::Continuous));
        in.addBinding(IM::bind("fly.fire", IM::pad(SDL_GAMEPAD_BUTTON_EAST), kke::Trigger::Continuous));
        in.addBinding(IM::bind("fly.brake", IM::pad(SDL_GAMEPAD_BUTTON_WEST), kke::Trigger::Continuous));
        in.addBinding(IM::bind("fly.camera", IM::pad(SDL_GAMEPAD_BUTTON_NORTH)));
        {
            kke::Binding b = IM::bind("fly.look", IM::padAxis(SDL_GAMEPAD_AXIS_RIGHTX), kke::Trigger::Continuous);
            b.sourceY = IM::padAxis(SDL_GAMEPAD_AXIS_RIGHTY);
            b.deadzone = 0.15f;
            in.addBinding(b);
        }
        in.addBinding(IM::bind("fly.pause", IM::pad(SDL_GAMEPAD_BUTTON_START)));
        in.addBinding(IM::bind("fly.again", IM::pad(SDL_GAMEPAD_BUTTON_SOUTH)));
        in.addBinding(IM::bind("fly.pause", IM::pad(SDL_GAMEPAD_BUTTON_BACK)));
        in.addBinding(IM::bind("fly.menu", IM::pad(SDL_GAMEPAD_BUTTON_BACK)));

        // Keyboard and mouse: W/S pitch (S pulls up, like a stick), A/D
        // roll, Q/E rudder, Shift/Ctrl throttle, Space smoke (in a
        // dogfight Space, F or the left button fire), B brakes, C camera;
        // the mouse is a stick that centres itself, the right button held
        // looks around instead.
        axisKeys(in, "fly.pitch", SDL_SCANCODE_S, SDL_SCANCODE_W);
        axisKeys(in, "fly.pitch", SDL_SCANCODE_DOWN, SDL_SCANCODE_UP);
        axisKeys(in, "fly.roll", SDL_SCANCODE_D, SDL_SCANCODE_A);
        axisKeys(in, "fly.roll", SDL_SCANCODE_RIGHT, SDL_SCANCODE_LEFT);
        axisKeys(in, "fly.yaw", SDL_SCANCODE_E, SDL_SCANCODE_Q);
        in.addBinding(IM::bind("fly.throttle.up", IM::key(SDL_SCANCODE_LSHIFT), kke::Trigger::Continuous));
        in.addBinding(IM::bind("fly.throttle.down", IM::key(SDL_SCANCODE_LCTRL), kke::Trigger::Continuous));
        in.addBinding(IM::bind("fly.smoke", IM::key(SDL_SCANCODE_SPACE)));
        in.addBinding(IM::bind("fly.fire", IM::key(SDL_SCANCODE_SPACE), kke::Trigger::Continuous));
        in.addBinding(IM::bind("fly.fire", IM::key(SDL_SCANCODE_F), kke::Trigger::Continuous));
        in.addBinding(IM::bind("fly.fire", IM::mouse(SDL_BUTTON_LEFT), kke::Trigger::Continuous));
        in.addBinding(IM::bind("fly.brake", IM::key(SDL_SCANCODE_B), kke::Trigger::Continuous));
        in.addBinding(IM::bind("fly.camera", IM::key(SDL_SCANCODE_C)));
        for (int axis = 0; axis < 2; ++axis) {
            kke::Binding b = IM::bind("fly.mouse", { kke::SourceKind::MouseMotion, 0, axis, 0 }, kke::Trigger::Continuous);
            b.component = axis;
            in.addBinding(b);
        }
        in.addBinding(IM::bind("fly.mouselook", IM::mouse(SDL_BUTTON_RIGHT), kke::Trigger::Continuous));
        in.addBinding(IM::bind("fly.smoke", IM::mouse(SDL_BUTTON_LEFT)));
        in.addBinding(IM::bind("fly.pause", IM::key(SDL_SCANCODE_ESCAPE)));
        in.addBinding(IM::bind("fly.again", IM::key(SDL_SCANCODE_RETURN)));
        in.addBinding(IM::bind("fly.again", IM::key(SDL_SCANCODE_R)));
        in.addBinding(IM::bind("fly.menu", IM::key(SDL_SCANCODE_M)));
        in.addBinding(IM::bind("panels", IM::key(SDL_SCANCODE_F1)));

        // Flight stick: X roll, Y pitch (pull back: nose up), twist the
        // rudder, the throttle lever the throttle; trigger smoke (in a
        // dogfight, fire), button 2 camera, 3 brakes, 4 pause; the hat
        // looks around.
        {
            kke::Binding roll = IM::bind("stick.roll", joyAxis(0), kke::Trigger::Continuous);
            kke::Binding pitch = IM::bind("stick.pitch", joyAxis(1), kke::Trigger::Continuous);
            kke::Binding yaw = IM::bind("stick.yaw", joyAxis(2), kke::Trigger::Continuous);
            roll.deadzone = pitch.deadzone = 0.04f;
            yaw.deadzone = 0.18f; // twist grips sit a little off centre
            in.addBinding(roll);
            in.addBinding(pitch);
            in.addBinding(yaw);
            in.addBinding(IM::bind("stick.throttle", joyAxis(3), kke::Trigger::Continuous));
        }
        in.addBinding(IM::bind("stick.smoke", joyButton(0)));
        in.addBinding(IM::bind("stick.fire", joyButton(0), kke::Trigger::Continuous));
        in.addBinding(IM::bind("stick.camera", joyButton(1)));
        in.addBinding(IM::bind("stick.brake", joyButton(2), kke::Trigger::Continuous));
        in.addBinding(IM::bind("stick.pause", joyButton(3)));
        in.addBinding(IM::bind("stick.again", joyButton(0)));
        in.addBinding(IM::bind("stick.accept", joyButton(0)));
        in.addBinding(IM::bind("stick.back", joyButton(1)));
        {
            kke::Binding right = IM::bind("stick.look", joyHat(1), kke::Trigger::Continuous);
            kke::Binding left = IM::bind("stick.look", joyHat(3), kke::Trigger::Continuous);
            kke::Binding up = IM::bind("stick.look", joyHat(0), kke::Trigger::Continuous);
            kke::Binding down = IM::bind("stick.look", joyHat(2), kke::Trigger::Continuous);
            right.component = left.component = 0;
            up.component = down.component = 1;
            left.scale = -1.0f;
            up.scale = -1.0f;
            for (const kke::Binding& b : { right, left, up, down }) in.addBinding(b);
        }

        // The pause menu: D-pad or stick, arrows or WASD, a stick's hat.
        const struct {
            const char *action, *stick;
            SDL_GamepadButton button;
            SDL_Scancode key, key2;
            int hat;
        } nav[] = { { "pause.up", "stick.up", SDL_GAMEPAD_BUTTON_DPAD_UP, SDL_SCANCODE_UP, SDL_SCANCODE_W, 0 },
                    { "pause.down", "stick.down", SDL_GAMEPAD_BUTTON_DPAD_DOWN, SDL_SCANCODE_DOWN, SDL_SCANCODE_S, 2 },
                    { "pause.left", "stick.left", SDL_GAMEPAD_BUTTON_DPAD_LEFT, SDL_SCANCODE_LEFT, SDL_SCANCODE_A, 3 },
                    { "pause.right", "stick.right", SDL_GAMEPAD_BUTTON_DPAD_RIGHT, SDL_SCANCODE_RIGHT, SDL_SCANCODE_D, 1 } };
        for (const auto& n : nav) {
            in.addBinding(IM::bind(n.action, IM::pad(n.button)));
            in.addBinding(IM::bind(n.action, IM::key(n.key)));
            in.addBinding(IM::bind(n.action, IM::key(n.key2)));
            in.addBinding(IM::bind(n.stick, joyHat(n.hat)));
        }
        in.addBinding(IM::bind("pause.up", IM::padAxis(SDL_GAMEPAD_AXIS_LEFTY, -1)));
        in.addBinding(IM::bind("pause.down", IM::padAxis(SDL_GAMEPAD_AXIS_LEFTY, 1)));
        in.addBinding(IM::bind("pause.left", IM::padAxis(SDL_GAMEPAD_AXIS_LEFTX, -1)));
        in.addBinding(IM::bind("pause.right", IM::padAxis(SDL_GAMEPAD_AXIS_LEFTX, 1)));
        in.addBinding(IM::bind("pause.accept", IM::pad(SDL_GAMEPAD_BUTTON_SOUTH)));
        in.addBinding(IM::bind("pause.accept", IM::key(SDL_SCANCODE_RETURN)));
        in.addBinding(IM::bind("pause.accept", IM::key(SDL_SCANCODE_SPACE)));
        in.addBinding(IM::bind("pause.back", IM::pad(SDL_GAMEPAD_BUTTON_EAST)));
        in.addBinding(IM::bind("pause.back", IM::key(SDL_SCANCODE_BACKSPACE)));
    }
    m_input->setPlayers(1);
    m_input->commitDefaults();
}

void FlyingModule::setupLobby() {
    const char* lobbyVar = kke::dev::env("KKE_FLY_LOBBY");
    if (!m_lobby) return;
    kke::Lobby& l = m_lobby->lobby();
    kke::Lobby::LookField names{ "name", "Name", {}, {} };
    for (const char* n : kNames) names.choices.push_back(n);
    l.addLookField(std::move(names));
    kke::Lobby::LookField colours{ "colour", "Colour", {}, {} };
    for (const Colour& c : kColours) {
        colours.choices.push_back(c.name);
        colours.swatches.push_back(c.rgb);
    }
    l.addLookField(std::move(colours));
    kke::Lobby::LookField paint{ "paint", "Paint", {}, {} };
    for (const char* p : kPaints) paint.choices.push_back(p);
    l.addLookField(std::move(paint));
    l.setDifficulties({ "Rookie", "Pilot", "Ace", "Legend" });
    l.setCpuCount(m_defaultCpus);

    kke::Lobby::Option modeRow{ "mode", "Mode", {}, 0, true, {}, {} };
    for (const char* m : kModeNames) modeRow.choices.push_back(m);
    l.addOption(std::move(modeRow));
    kke::Lobby::Option island{ "island", "Map", {}, 0, true, {}, {} };
    for (const IslandPick& i : kIslands) island.choices.push_back(i.name);
    island.choices.push_back("Random");
    l.addOption(std::move(island));
    l.addOption({ "laps", "Laps", { "1", "2", "3" }, 1, true, {}, {} });
    l.addOption({ "rings", "Rings", { "Big", "Normal", "Tight" }, 1, true, {}, {} });
    l.addOption({ "kills", "First to", { "5 kills", "10 kills", "20 kills" }, 1, true, {}, {} });
    kke::Lobby::Option sky{ "sky", "Sky", {}, 0, true, {}, {} };
    for (const SkyPick& s : kSkies) sky.choices.push_back(s.name);
    sky.onChange = [this](int) { m_app->setMood(moodName()); };
    l.addOption(std::move(sky));
    // Laps and rings mean nothing outside a race, kills outside a dogfight.
    auto showRaceRows = [this]() {
        kke::Lobby& lb = m_lobby->lobby();
        const bool race = mode() == Mode::Race;
        if (kke::Lobby::Option* o = lb.option("laps")) o->visible = race;
        if (kke::Lobby::Option* o = lb.option("rings")) o->visible = race;
        if (kke::Lobby::Option* o = lb.option("kills")) o->visible = mode() == Mode::Dogfight;
    };
    l.option("mode")->onChange = [showRaceRows](int) { showRaceRows(); };
    m_lobby->load();
    showRaceRows();
    m_lobby->setTitle("STUNT PLANES", "Pick your pilot and plane. A controller, a flight stick or the keyboard: press {a}, the trigger or Enter to join.");
    l.onJoin = [this](int seat) {
        if (m_phase == Phase::Lobby) return;
        m_lobby->lobby().toast(m_lobby->lobby().seatName(seat) + " joins the next flight", 5.0f);
    };
    if (const char* v = kke::dev::env("KKE_FLY_MODE")) {
        const int pick = std::strcmp(v, "stunts") == 0 ? 1 : std::strcmp(v, "free") == 0 ? 2 : std::strcmp(v, "dogfight") == 0 ? 3 : 0;
        l.option("mode")->value = pick;
        showRaceRows();
    }
    if (const char* v = kke::dev::env("KKE_FLY_ISLAND")) {
        for (int i = 0; i < kIslandCount; ++i)
            if (std::to_string(kIslands[i].seed) == v || kIslands[i].name == std::string(v)) l.option("island")->value = i;
        if (std::strcmp(v, "canyon") == 0) l.option("island")->value = kIslandCount - 2;
        if (std::strcmp(v, "city") == 0) l.option("island")->value = kIslandCount - 1;
        if (std::strcmp(v, "random") == 0) l.option("island")->value = kIslandCount;
    }
    if (const char* v = kke::dev::env("KKE_FLY_CPUS")) l.setCpuCount(std::clamp(std::atoi(v), 0, kke::Lobby::kMaxCpus));
    // Straight into a flight (demos, tests), unless waiting for players
    // to join online first (KKE_FLY_WAIT).
    if (m_netWait == 0 && (m_autopilot || m_quitAfter > 0.0f || (lobbyVar && std::strcmp(lobbyVar, "0") == 0))) m_lobby->close();
}

FlyingModule::Mode FlyingModule::mode() const {
    if (!m_lobby) return Mode::Race;
    const kke::Lobby::Option* o = m_lobby->lobby().option("mode");
    return o ? static_cast<Mode>(std::clamp(o->value, 0, kModes - 1)) : Mode::Race;
}

uint32_t FlyingModule::islandSeed() const {
    if (!m_lobby) return kIslands[0].seed;
    const kke::Lobby::Option* o = m_lobby->lobby().option("island");
    const int pick = o ? o->value : 0;
    if (pick >= 0 && pick < kIslandCount) return kIslands[pick].seed;
    // Random: rolled once and kept (the menu reads this every frame, so a
    // new one each call rebuilt the island every frame), and rolled again
    // back in the start menu after a flight.
    if (m_randomSeed == 0) m_randomSeed = 1000u + static_cast<uint32_t>(SDL_GetTicks() % 100000u);
    return m_randomSeed;
}

std::string FlyingModule::moodName() const {
    if (!m_lobby) return kSkies[0].mood;
    const kke::Lobby::Option* o = m_lobby->lobby().option("sky");
    const int pick = o ? std::clamp(o->value, 0, static_cast<int>(sizeof(kSkies) / sizeof(kSkies[0])) - 1) : 0;
    return kSkies[pick].mood;
}

std::vector<FlyingModule::Entry> FlyingModule::wantedRoster() const {
    std::vector<Entry> out;
    if (!m_lobby) {
        Entry e;
        e.seat = 0;
        e.name = m_netName.empty() ? kNames[0] : m_netName;
        e.tint = kColours[0].rgb;
        out.push_back(e);
        for (int i = 0; i < m_defaultCpus; ++i) {
            Entry c;
            c.cpu = true;
            c.skill = 2;
            c.name = kNames[(i + 1) % 12];
            c.tint = kColours[(i + 1) % 8].rgb;
            c.livery = (i + 1) % 4;
            out.push_back(c);
        }
        return out;
    }
    const kke::Lobby& l = m_lobby->lobby();
    std::vector<std::string> taken;
    std::vector<int> colours;
    for (int seat : l.joinedSeats()) {
        Entry e;
        e.seat = seat;
        e.name = out.empty() && !m_netName.empty() ? m_netName : l.seatName(seat); // KKE_NET_NAME: player 1's name online
        const std::vector<int>& look = l.seat(seat).look;
        e.tint = kColours[static_cast<size_t>(look.size() > 1 ? look[1] : 0) % 8].rgb;
        e.livery = look.size() > 2 ? look[2] : 0;
        e.look = look;
        taken.push_back(e.name);
        colours.push_back(look.size() > 1 ? look[1] : 0);
        out.push_back(e);
    }
    for (int i = 0; i < l.cpuCount(); ++i) {
        Entry c;
        c.cpu = true;
        c.skill = l.cpuDifficulty(i);
        for (const char* n : kNames)
            if (std::find(taken.begin(), taken.end(), n) == taken.end()) {
                c.name = n;
                break;
            }
        taken.push_back(c.name);
        int colour = 0;
        while (std::find(colours.begin(), colours.end(), colour) != colours.end() && colour < 7) ++colour;
        colours.push_back(colour);
        c.tint = kColours[colour].rgb;
        c.livery = (i + 1) % 4;
        int nameIndex = 0;
        for (int n = 0; n < static_cast<int>(std::size(kNames)); ++n)
            if (c.name == kNames[n]) nameIndex = n;
        c.look = { nameIndex, colour, c.livery };
        out.push_back(c);
    }
    return out;
}

bool FlyingModule::onFlightStick(const Pilot& p) const {
    if (!m_lobby || p.seat < 0) return false;
    const kke::Lobby::Seat& s = m_lobby->lobby().seat(p.seat);
    if (s.device == kke::Lobby::Device::Any) {
        // Player 1 before picking a device: a flight stick counts when
        // it's the only kind of controller here (a gamepad's axes read
        // as joystick axes too, and would fly the plane twice).
        bool stick = false, pad = false;
        for (const kke::InputDevices::Device& d : m_input->devices().devices()) {
            if (!d.connected) continue;
            stick = stick || d.kind == kke::InputDevices::Kind::Joystick;
            pad = pad || d.kind == kke::InputDevices::Kind::Gamepad;
        }
        return stick && !pad;
    }
    if (s.device != kke::Lobby::Device::Pad) return false;
    const kke::InputDevices::Device* d = m_input->devices().find(s.pad);
    return d && d->kind == kke::InputDevices::Kind::Joystick;
}

bool FlyingModule::onKeyboard(const Pilot& p) const {
    if (!m_lobby) return p.seat == 0;
    if (p.seat < 0) return false;
    const kke::Lobby::Device d = m_lobby->lobby().seat(p.seat).device;
    return d == kke::Lobby::Device::KeyboardMouse || d == kke::Lobby::Device::Any;
}

std::string FlyingModule::deviceName(int seat) const {
    if (!m_lobby || seat < 0) return {};
    const kke::Lobby::Seat& s = m_lobby->lobby().seat(seat);
    switch (s.device) {
    case kke::Lobby::Device::KeyboardMouse: return "Keyboard and mouse";
    case kke::Lobby::Device::Any: return "Any controller or the keyboard";
    case kke::Lobby::Device::Pad:
        if (const kke::InputDevices::Device* d = m_input->devices().find(s.pad))
            return (d->kind == kke::InputDevices::Kind::Joystick ? "Flight stick: " : "") + d->label() + (s.padPresent ? "" : " (unplugged)");
        return "Controller (unplugged)";
    }
    return {};
}

// A player on a flight stick: the lobby seat that InputModule player
// belongs to holds a joystick.
bool FlyingModule::stickPlayer(int player) const {
    for (const Pilot& p : m_pilots)
        if (!p.cpu && !p.remote && p.player == player) return onFlightStick(p);
    return false;
}

// "fly.smoke" pressed on this player's devices, or "stick.smoke" when
// they fly a stick (see defineActions).
bool FlyingModule::pressedBy(int player, const std::string& action) const {
    if (player < 0 || player >= m_input->players()) return false;
    const kke::InputMap& in = m_input->map(player);
    if (in.pressed(action)) return true;
    return stickPlayer(player) && in.pressed("stick." + action.substr(action.find('.') + 1));
}

bool FlyingModule::heldBy(int player, const std::string& action) const {
    if (player < 0 || player >= m_input->players()) return false;
    const kke::InputMap& in = m_input->map(player);
    if (in.held(action)) return true;
    return stickPlayer(player) && in.held("stick." + action.substr(action.find('.') + 1));
}

Controls FlyingModule::readPlayer(Pilot& p, float dt) {
    Controls c = p.controls;
    kke::InputMap& in = m_input->map(std::clamp(p.player, 0, m_input->players() - 1));
    float pitch = in.axis("fly.pitch"), roll = in.axis("fly.roll"), yaw = in.axis("fly.yaw");
    c.throttle = std::clamp(c.throttle + (in.axis("fly.throttle.up") - in.axis("fly.throttle.down")) * 0.6f * dt, 0.0f, 1.0f);
    if (onFlightStick(p)) {
        pitch += in.axis("stick.pitch");
        roll += in.axis("stick.roll");
        yaw += in.axis("stick.yaw");
        // The lever sets the throttle when it moves (so it doesn't fight
        // the buttons while it rests): forward (-1) is full.
        const float lever = in.axis("stick.throttle");
        if (p.stickThrottle < -1.5f || std::abs(lever - p.stickThrottle) > 0.02f) {
            if (p.stickThrottle > -1.5f) c.throttle = std::clamp((1.0f - lever) * 0.5f, 0.0f, 1.0f);
            p.stickThrottle = lever;
        }
    }
    const glm::vec2 mouse = in.axis2("fly.mouse");
    const bool mouseLook = in.held("fly.mouselook");
    if (onKeyboard(p)) {
        // The mouse is a stick that centres itself: move it forward (up)
        // to push the nose down, like a real stick.
        if (!mouseLook) p.mouseStick += glm::vec2(mouse.x, mouse.y) * 0.0035f;
        p.mouseStick = glm::clamp(p.mouseStick, glm::vec2(-1.0f), glm::vec2(1.0f)) * std::exp(-1.6f * dt);
        pitch += p.mouseStick.y;
        roll += p.mouseStick.x;
    }
    pitch = std::clamp(pitch, -1.0f, 1.0f);
    roll = std::clamp(roll, -1.0f, 1.0f);
    yaw = std::clamp(yaw, -1.0f, 1.0f);
    if (onKeyboard(p) && !onFlightStick(p)) {
        // Keys are all or nothing: ease the surfaces over (full in about a
        // third of a second), so a tap is a small correction.
        const float step = dt * 3.5f;
        auto ease = [step](float from, float to) { return from + std::clamp(to - from, -step, step); };
        c.pitch = ease(c.pitch, pitch);
        c.roll = ease(c.roll, roll);
        c.yaw = ease(c.yaw, yaw);
    } else {
        c.pitch = pitch;
        c.roll = roll;
        c.yaw = yaw;
    }
    c.brake = heldBy(p.player, "fly.brake");
    // A dogfight has guns instead of smoke, on the same buttons.
    if (m_mode == Mode::Dogfight) p.firing = heldBy(p.player, "fly.fire");
    else if (pressedBy(p.player, "fly.smoke")) p.smoke = !p.smoke;
    if (pressedBy(p.player, "fly.camera")) p.cameraMode = (p.cameraMode + 1) % 3;
    // Looking around: the right stick or hat while held (back to the nose
    // when let go), or the mouse with its right button held.
    glm::vec2 look = in.axis2("fly.look");
    if (onFlightStick(p)) look += in.axis2("stick.look");
    const glm::vec2 want = glm::clamp(look, glm::vec2(-1.0f), glm::vec2(1.0f)) * glm::vec2(150.0f, 70.0f);
    if (mouseLook && onKeyboard(p)) {
        p.look += glm::vec2(mouse.x, -mouse.y) * 0.25f;
        p.look = glm::clamp(p.look, glm::vec2(-170.0f, -80.0f), glm::vec2(170.0f, 80.0f));
    } else {
        p.look += (want - p.look) * std::min(1.0f, dt * 6.0f);
    }
    return c;
}

Controls FlyingModule::readCpu(Pilot& p) {
    const float skill = static_cast<float>(std::clamp(p.skill, 0, 3)) / 3.0f;
    // The ground as a CPU pilot sees it: the land, and the roofs (with room
    // to spare: it doesn't shave the walls).
    Ground g = m_island.ground();
    g.height = [this](float x, float z) { return std::max(m_island.surface(x, z), m_town.roof(x, z, 25.0f)); };
    p.firing = false;
    // Off the runway: full throttle straight down it, and climb straight
    // out until clear of everything.
    if (p.plane.onGround || p.climbOut) {
        const glm::vec3 ahead = p.plane.forward();
        const glm::vec3 out = p.plane.position + glm::normalize(glm::vec3(ahead.x, 0.0f, ahead.z) + glm::vec3(0.0f, 0.0f, 1e-4f)) * 500.0f + glm::vec3(0.0f, 160.0f, 0.0f);
        Controls c = steerToward(p.plane, out, g, 0.0f, skill);
        c.throttle = 1.0f;
        if (p.plane.onGround) c.roll = c.yaw = 0.0f;
        p.smoke = false;
        return c;
    }
    // Don't fly into each other: a plane close by pushes the aim away
    // from it (a pilot keeps its distance in a formation).
    glm::vec3 apart(0.0f);
    for (const Pilot& o : m_pilots) {
        if (&o == &p || down(o) || !present(o)) continue;
        const glm::vec3 d = p.plane.position - (o.remote ? o.net.position : o.plane.position);
        const float dist = glm::length(d);
        if (dist < 30.0f && dist > 0.01f) apart += d / dist * (30.0f - dist) * 3.0f;
    }
    if (m_mode == Mode::Race && !m_rings.empty() && m_island.map() == Map::Canyon) {
        // Down the gorge: a straight line to a ring cuts through the rock on
        // the bends, so follow the gorge's middle line a little ahead, at
        // the next ring's height; on the last stretch, line up on the ring.
        const Ring& next = m_rings[static_cast<size_t>(p.nextRing) % m_rings.size()];
        const glm::vec3 at = p.plane.position;
        const float here = std::atan2(at.z, at.x);
        const float dir = glm::dot(next.normal, m_island.gorgeTangent(std::atan2(next.center.z, next.center.x))) >= 0.0f ? 1.0f : -1.0f;
        // Down in the gorge (until it climbs out of it or is well out to
        // the side): the walls don't count as ground ahead, or it would
        // climb out at every bend, and nor do the pillars: following the
        // middle line keeps it off the walls, and it steers round the
        // pillars (below). A bridge it is about to fly into does count.
        const float off = m_island.gorgeDistance(at.x, at.z), hw = m_island.gorgeHalfWidth(here);
        if (at.y < Island::kPlateau - 20.0f && off < hw) p.inLane = true;
        else if (at.y > Island::kPlateau + 10.0f || off > hw + 150.0f) p.inLane = false;
        if (p.inLane)
            g.height = [this, under = at.y - 8.0f](float x, float z) {
                float top = Island::kFloor;
                for (const Building& b : m_town.buildings())
                    if (b.lo.y > Island::kFloor + 20.0f && b.lo.y < under && x > b.lo.x - 8.0f && x < b.hi.x + 8.0f && z > b.lo.z - 8.0f && z < b.hi.z + 8.0f)
                        top = std::max(top, b.hi.y);
                return top;
            };
        // Up on the plateau it first lines up over the gorge, high above
        // the rim, and only goes down once it is over the middle and flying
        // along it: coming in from the side, it would overshoot into a wall.
        const glm::vec3 way = m_island.gorgeTangent(here) * dir;
        const glm::vec2 flat(p.plane.velocity.x, p.plane.velocity.z);
        const bool along = glm::length(flat) > 1.0f && glm::dot(glm::normalize(flat), glm::normalize(glm::vec2(way.x, way.z))) > 0.9f;
        const bool descend = p.inLane || (along && off < hw * 0.6f);
        glm::vec3 target;
        const float ahead = glm::dot(at - next.center, next.normal);
        const float aside = glm::length((at - next.center) - next.normal * ahead);
        if (ahead < -5.0f && ahead > -320.0f && aside < 80.0f) {
            // The last stretch: onto the ring's line, gently (a sharp turn
            // onto it overshoots it).
            target = next.center + next.normal * (ahead + 100.0f);
        } else {
            target = m_island.gorgePoint(here + dir * 180.0f / m_island.gorgeRadius(here));
            target.y = descend ? next.center.y : Island::kPlateau + 40.0f;
        }
        // A pillar near the way there: aim to pass it with room to spare.
        for (const Building& b : m_town.buildings()) {
            if (b.lo.y > Island::kFloor + 20.0f || b.hi.y < at.y - 10.0f) continue;
            const glm::vec2 c((b.lo.x + b.hi.x) * 0.5f, (b.lo.z + b.hi.z) * 0.5f), from(at.x, at.z), to(target.x, target.z);
            const glm::vec2 seg = to - from;
            const float k = std::clamp(glm::dot(c - from, seg) / std::max(glm::dot(seg, seg), 1.0f), 0.0f, 1.0f);
            if (k <= 0.0f || k >= 1.0f) continue;
            const glm::vec2 closest = from + seg * k, away = closest - c;
            const float d = glm::length(away), room = (b.hi.x - b.lo.x) * 0.5f + 22.0f;
            if (d >= room) continue;
            const glm::vec2 mid(m_island.gorgePoint(here).x, m_island.gorgePoint(here).z);
            const glm::vec2 side = d > 0.5f ? away / d : glm::normalize(glm::vec2(-seg.y, seg.x)) * (glm::dot(mid - c, glm::vec2(-seg.y, seg.x)) >= 0.0f ? 1.0f : -1.0f);
            const glm::vec2 shift = side * (room - d) / k;
            target.x += shift.x;
            target.z += shift.y;
        }
        return steerToward(p.plane, target + apart * 0.5f, g, 25.0f, skill);
    }
    if (m_mode == Mode::Race && !m_rings.empty() && m_island.map() == Map::City) {
        // Down the avenue: Town leaves the straight line from ring to ring
        // clear, and each ring faces straight down it, so the ring pilot's
        // way in (and its turn back after a miss) runs along the avenue.
        // Over the avenue only what is right under the plane counts as
        // ground, or it would climb over every tower beside it; anywhere
        // else the roofs count and it comes back over the towers.
        const size_t n = m_rings.size();
        const Ring& next = m_rings[static_cast<size_t>(p.nextRing) % n];
        const glm::vec3 at = p.plane.position;
        float off = 1e9f;
        for (size_t back = 0; back < 2; ++back) { // this avenue and the one before (a turn back)
            const Ring& a = m_rings[(static_cast<size_t>(p.nextRing) + n - back) % n];
            const Ring& b = m_rings[(static_cast<size_t>(p.nextRing) + n - back - 1) % n];
            const glm::vec2 from(b.center.x, b.center.z), ab = glm::vec2(a.center.x, a.center.z) - from, here(at.x, at.z);
            const float k = std::clamp(glm::dot(here - from, ab) / std::max(glm::dot(ab, ab), 1.0f), 0.0f, 1.0f);
            off = std::min(off, glm::length(here - (from + ab * k)));
        }
        p.inLane = off < 45.0f;
        if (p.inLane)
            g.height = [this, under = at.y - 8.0f](float x, float z) { return std::max(m_island.surface(x, z), m_town.roof(x, z, 8.0f, under)); };
        glm::vec3 target = p.autopilot.aim(next, at);
        if (glm::length(next.center - at) > 120.0f) target += apart * 0.5f;
        return steerToward(p.plane, target, g, p.inLane ? 25.0f : p.autopilot.floor(40.0f), skill);
    }
    if (m_mode == Mode::Race && !m_rings.empty()) {
        const Ring& next = m_rings[static_cast<size_t>(p.nextRing) % m_rings.size()];
        glm::vec3 target = p.autopilot.aim(next, p.plane.position);
        // Near the ring, the ring wins: the others will be through it or past.
        if (glm::length(next.center - p.plane.position) > 120.0f) target += apart;
        return steerToward(p.plane, target, g, p.autopilot.floor(55.0f), skill);
    }
    if (m_mode == Mode::Dogfight && m_phase != Phase::Lobby) {
        const glm::vec3 c = m_town.centre();
        return readCpuDogfight(p, steerToward(p.plane, glm::vec3(c.x, c.y + 60.0f, c.z) + apart, g, 60.0f, skill));
    }
    // Stunts and free flight: laps of the island at a sightseeing height,
    // with a loop now and then for the show.
    const float a = m_clock * 0.05f + static_cast<float>(p.slot);
    const glm::vec3 target = glm::vec3(std::cos(a) * 900.0f, 220.0f, std::sin(a) * 900.0f) + apart;
    Controls c = steerToward(p.plane, target, g, 60.0f, skill);
    // Stunts: a loop (about 7 s round), then a roll, every 30 s, when
    // there's room under it.
    const float cycle = std::fmod(m_clock + static_cast<float>(p.slot) * 7.0f, 30.0f);
    const bool room = p.plane.position.y - m_island.surface(p.plane.position.x, p.plane.position.z) > 150.0f;
    if (m_mode == Mode::Stunts && m_phase == Phase::Flying && room && cycle < 8.0f) {
        c.pitch = 1.0f;
        c.roll = 0.0f;
        c.throttle = 1.0f;
    } else if (m_mode == Mode::Stunts && m_phase == Phase::Flying && room && cycle > 12.0f && cycle < 13.6f) {
        c.roll = 1.0f;
        c.pitch = 0.1f;
    }
    p.smoke = m_mode == Mode::Stunts;
    return c;
}

// A CPU dogfighter: picks someone (whoever last hurt it, else the nearest),
// flies at where they'll be when its bullets get there, and fires when
// they're in its sights, the way is clear and they're in range. Better
// pilots turn harder and aim truer (Damage.cpp's spread). Nobody in reach:
// `cruise` (a circle over the town).
Controls FlyingModule::readCpuDogfight(Pilot& p, const Controls& cruise) {
    const int self = static_cast<int>(&p - m_pilots.data());
    const float skill = static_cast<float>(std::clamp(p.skill, 0, 3)) / 3.0f;
    const float dt = 1.0f / 60.0f;
    p.targetFor += dt;
    auto valid = [&](int i) {
        return i >= 0 && i < static_cast<int>(m_pilots.size()) && i != self && !down(m_pilots[static_cast<size_t>(i)]) && present(m_pilots[static_cast<size_t>(i)]);
    };
    if (p.lastBy >= 0 && m_clock - p.lastByAt < 3.0f && valid(p.lastBy)) p.target = p.lastBy; // revenge
    if (!valid(p.target) || p.targetFor > 20.0f) {
        p.targetFor = 0.0f;
        p.target = -1;
        float best = 1e9f;
        for (size_t i = 0; i < m_pilots.size(); ++i) {
            if (!valid(static_cast<int>(i))) continue;
            const Pilot& o = m_pilots[i];
            if (safe(o)) continue; // still on the runway: it can't be hit
            const float d = glm::length((o.remote ? o.net.position : o.plane.position) - p.plane.position);
            if (d < best) {
                best = d;
                p.target = static_cast<int>(i);
            }
        }
    }
    if (!valid(p.target)) return cruise;
    const Pilot& t = m_pilots[static_cast<size_t>(p.target)];
    const glm::vec3 at = t.remote ? t.net.position : t.plane.position, vel = t.remote ? t.net.velocity : t.plane.velocity;
    const glm::vec3 lead = leadPoint(p.plane.position, at, vel - p.plane.velocity, 480.0f);
    Ground g = m_island.ground();
    g.height = [this](float x, float z) { return std::max(m_island.surface(x, z), m_town.roof(x, z, 25.0f)); };
    Controls c = steerToward(p.plane, lead, g, 35.0f + 25.0f * (1.0f - skill), 0.4f + 0.6f * skill);
    const glm::vec3 to = lead - p.plane.position;
    const float range = glm::length(to);
    const float off = glm::degrees(std::acos(std::clamp(glm::dot(glm::normalize(to), p.plane.forward()), -1.0f, 1.0f)));
    float wall = 0.0f;
    p.firing = range < 650.0f && off < 4.0f + (1.0f - skill) * 4.0f && !m_town.blocks(p.plane.position, at, wall);
    // Close behind: ease off, don't fly into it.
    if (range < 90.0f && glm::dot(to, p.plane.forward()) > 0.0f) c.throttle = std::min(c.throttle, 0.3f);
    return c;
}

// ---- the pause menu

std::vector<FlyingModule::DeviceChoice> FlyingModule::deviceChoices() const {
    std::vector<DeviceChoice> out;
    if (!m_lobby) return out;
    const kke::Lobby& l = m_lobby->lobby();
    bool keyboard = false;
    for (const kke::InputDevices::Device& d : m_input->devices().devices()) {
        if (!d.connected) continue;
        if (d.kind == kke::InputDevices::Kind::Keyboard || d.kind == kke::InputDevices::Kind::Mouse) {
            keyboard = true;
            continue;
        }
        if (!m_lobby->seatable(d)) continue;
        DeviceChoice c;
        c.kind = kke::Lobby::Device::Pad;
        c.ref = d.ref;
        c.label = (d.kind == kke::InputDevices::Kind::Joystick ? "Flight stick: " : "") + d.label();
        c.holder = l.seatOfPad(d.ref);
        out.push_back(c);
    }
    if (keyboard) {
        DeviceChoice c;
        c.label = "Keyboard and mouse";
        c.holder = l.seatOfKeyboard();
        out.push_back(c);
    }
    return out;
}

void FlyingModule::openPause(int seat) {
    m_pauseSeat = seat;
    m_pauseRow = 0;
    m_pauseAge = 0.0f;
    // The Controls row starts on the device the player has now.
    m_pauseDevice = 0;
    const std::vector<DeviceChoice> choices = deviceChoices();
    for (size_t i = 0; i < choices.size(); ++i)
        if (choices[i].holder == seat) m_pauseDevice = static_cast<int>(i);
}

void FlyingModule::closePause() { m_pauseSeat = -1; }

// The rows: Resume, Controls (left / right through the devices; the ones
// another player holds are skipped), Restart, Settings (the shared game
// shell: settings, button remapping), Start menu, Quit.
void FlyingModule::updatePause(float dt) {
    if (m_pauseSeat < 0) return;
    m_pauseAge += dt;
    int player = 0;
    for (const Pilot& p : m_pilots)
        if (p.seat == m_pauseSeat) player = p.player;
    constexpr int kRows = 6;
    if (pressedBy(player, "pause.up")) m_pauseRow = (m_pauseRow + kRows - 1) % kRows;
    if (pressedBy(player, "pause.down")) m_pauseRow = (m_pauseRow + 1) % kRows;
    const int step = (pressedBy(player, "pause.right") ? 1 : 0) - (pressedBy(player, "pause.left") ? 1 : 0);
    const std::vector<DeviceChoice> choices = deviceChoices();
    if (step != 0 && m_pauseRow == 1 && !choices.empty()) {
        // Only to a free device (or back to your own).
        const int n = static_cast<int>(choices.size());
        for (int i = 1; i <= n; ++i) {
            const int k = ((m_pauseDevice + step * i) % n + n) % n;
            if (choices[static_cast<size_t>(k)].holder < 0 || choices[static_cast<size_t>(k)].holder == m_pauseSeat) {
                m_pauseDevice = k;
                break;
            }
        }
    }
    if (m_pauseAge > 0.15f && (pressedBy(player, "pause.back") || pressedBy(player, "fly.pause"))) {
        closePause();
        return;
    }
    if (pressedBy(player, "pause.accept") && m_pauseAge > 0.15f) pauseAction(m_pauseRow);
}

void FlyingModule::pauseAction(int row) {
    switch (row) {
    case 0: closePause(); break;
    case 1: {
        const std::vector<DeviceChoice> choices = deviceChoices();
        if (m_pauseDevice < 0 || m_pauseDevice >= static_cast<int>(choices.size())) break;
        const DeviceChoice& c = choices[static_cast<size_t>(m_pauseDevice)];
        const std::string who = m_lobby->lobby().seatName(m_pauseSeat);
        if (c.holder >= 0 && c.holder != m_pauseSeat) {
            m_lobby->lobby().toast(c.label + " is " + m_lobby->lobby().seatName(c.holder) + "'s", 4.0f);
            break;
        }
        if (m_lobby->lobby().setSeatDevice(m_pauseSeat, c.kind, c.ref)) {
            m_lobby->lobby().toast(who + " now flies with " + c.label, 4.0f);
            kke::log::get(name())->info("{} moved to {}", who, c.label);
            for (Pilot& p : m_pilots)
                if (p.seat == m_pauseSeat) {
                    p.mouseStick = glm::vec2(0.0f);
                    p.stickThrottle = -2.0f;
                }
            closePause();
        }
        break;
    }
    case 2:
        closePause();
        if (!netClient()) {
            newFlight();
            if (netHost()) sendSetup();
        }
        break;
    case 3:
        closePause();
        if (auto* shell = m_app->getModule<kke::GameShellModule>()) shell->openPause();
        break;
    case 4:
        closePause();
        backToLobby();
        break;
    case 5: {
        SDL_Event quit{};
        quit.type = SDL_EVENT_QUIT;
        SDL_PushEvent(&quit);
        break;
    }
    default: break;
    }
}

} // namespace flying
