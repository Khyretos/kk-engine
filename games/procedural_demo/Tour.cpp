// The tour: the demo's answer to "what would I use this for?". Each
// station points the camera at one creature, says what you are looking
// at, which games need it, and what to try. N / B (d-pad right / left)
// go to the next and previous station; H hides the card.

#include "ProceduralDemoModule.h"

#include "kke/Application.h"
#include "kke/ButtonPrompts.h"
#include "kke/DevTools.h"
#include "kke/Log.h"
#include "kke/modules/InputModule.h"
#include "kke/modules/OrbitCameraModule.h"
#include "kke/modules/UiModule.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>
#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cstdlib>
#include <string>

namespace procedural_demo {

namespace {

struct Station {
    const char* title;
    const char* what;    // what you are looking at
    const char* useFor;  // the games that need it
    const char* tryMouse; // what to try with the keyboard and mouse (RML: may hold <prompt> elements)
    const char* tryPad;   // the same on a controller
    int focus;           // 0 spider, 1 beetle, 2 dog, 3 person, -1 the whole meadow
    float yaw, pitch, distance; // degrees, degrees, metres
};

// The creatures are made in this order in init(): spider, beetle, dog, person.
const Station kStations[] = {
    { "Creatures with no animations",
      "Four creatures and not one animation clip between them. Everything you see them do is worked out "
      "while they move: where each foot goes, how the body sways, where the head looks, how they fall.",
      "Any game with creatures: it saves animating every case by hand, and the motion fits the ground "
      "and the hit it gets.",
      "<prompt action=\"proc.next\" label=\"start the tour\"/>",
      "<prompt action=\"proc.next\" label=\"start the tour\"/>",
      -1, -35.0f, -25.0f, 7.5f },
    { "Feet that find the ground",
      "The spider has 8 legs and the beetle 6. A foot lifts when it falls too far behind and lands where "
      "a ray finds the ground: on the hills, and up the steps they are walking to now.",
      "Spiders, insects, crabs, robots and mechs: anything with many legs on slopes, stairs and rubble, "
      "with no clip for each case.",
      "click the ground: a flag, and they walk to it",
      "<prompt action=\"proc.call\" label=\"a flag at the crosshair\"/>: they walk to it",
      0, -60.0f, -18.0f, 2.4f },
    { "Walk, trot, gallop",
      "One dog skeleton picks its gait from its speed, the way real animals do (the Froude number): it "
      "walks, trots and gallops by itself. The tail wags, faster when you come close.",
      "Dogs, horses, mounts and monsters of any size: one rig instead of a clip for every speed.",
      "<prompt action=\"proc.gait\" label=\"pick the dog's gait\"/> (or the keys 0 to 3)",
      "<prompt action=\"proc.gait\" label=\"pick the dog's gait\"/>",
      2, -20.0f, -15.0f, 3.2f },
    { "Looking at you",
      "Heads turn to follow the camera, as far as a neck can turn, and let go when you move away.",
      "People in a town who notice the player, guards, shopkeepers, a pet watching you.",
      "right-drag the view to walk the camera round them",
      "<prompt action=\"camera.orbit\" label=\"walk the camera round them\"/>",
      3, 25.0f, -8.0f, 2.2f },
    { "Hit, stagger, fall, get up",
      "A hit hands the body to physics with muscles (joint motors): a light hit makes it stagger and catch "
      "itself, a hard one knocks it down, then it gets up by itself. The tour hits it every few seconds.",
      "Brawlers, shooters, sports and slapstick: the reaction comes from the real hit, not a canned flinch.",
      "click the person to hit, Shift+click to hit hard",
      "<prompt action=\"proc.hit\" label=\"hit\"/>   <prompt action=\"proc.hard\" label=\"hit hard\"/> at the crosshair",
      3, -70.0f, -12.0f, 4.0f },
    { "Your turn",
      "Everything at once. docs/PROCEDURAL_ANIMATION.md shows the code for each part, and the same parts "
      "move characters in the other demos.",
      "Call them, hit them, change the dog's gait, fly around.",
      "click the ground to call them, click one to hit it, <prompt action=\"proc.gait\" label=\"the dog's gait\"/>",
      "<prompt action=\"proc.call\" label=\"call\"/>   <prompt action=\"proc.hit\" label=\"hit\"/>   <prompt action=\"proc.gait\" label=\"gait\"/>",
      -1, -35.0f, -25.0f, 7.5f },
};
constexpr int kStationCount = static_cast<int>(sizeof(kStations) / sizeof(kStations[0]));

const char* kCardRml = R"(
<rml>
<head>
  <style>
    body { font-family: Noto Sans; font-size: 17dp; color: #f2f4f8; width: 100%; height: 100%; }
    div { display: block; }
    #card { position: absolute; left: 24dp; bottom: 24dp; width: 520dp; padding: 14dp 20dp;
            background-color: #10141ed9; border-radius: 12dp; }
    #step { color: #9aa3b8; font-size: 14dp; }
    #title { font-size: 23dp; font-weight: bold; color: #ffd27a; margin-bottom: 6dp; }
    .label { color: #9fe2ff; }
    .row { margin: 5dp 0; }
    .dim { color: #9aa3b8; font-size: 14dp; margin-top: 8dp; }
  </style>
</head>
<body>
  <div id="card">
    <div id="step"></div>
    <div id="title"></div>
    <div class="row" id="what"></div>
    <div class="row"><span class="label">Use it for: </span><span id="use"></span></div>
    <div class="row"><span class="label">Try: </span><span id="try"></span></div>
    <div class="dim"><prompt action="proc.prev" label="Back"/>   <prompt action="proc.next" label="Next"/>   <prompt action="proc.guide" label="Hide this card"/></div>
  </div>
</body>
</rml>
)";

void setText(Rml::ElementDocument* doc, const char* id, const std::string& rml) {
    if (Rml::Element* e = doc->GetElementById(id)) e->SetInnerRML(rml);
}

} // namespace

int ProceduralDemoModule::creatureIndex(Kind kind) const {
    for (size_t i = 0; i < m_creatures.size(); ++i)
        if (m_creatures[i].kind == kind) return static_cast<int>(i);
    return -1;
}

void ProceduralDemoModule::buildTour() {
    if (auto* in = m_app->getModule<kke::InputModule>()) {
        using IM = kke::InputModule;
        kke::InputMap& m = in->map(0);
        m.defineAction({ "proc.next", "Tour: next", "Tour" });
        m.addBinding(IM::bind("proc.next", IM::key(SDL_SCANCODE_N)));
        m.addBinding(IM::bind("proc.next", IM::pad(SDL_GAMEPAD_BUTTON_DPAD_RIGHT)));
        m.defineAction({ "proc.prev", "Tour: back", "Tour" });
        m.addBinding(IM::bind("proc.prev", IM::key(SDL_SCANCODE_B)));
        m.addBinding(IM::bind("proc.prev", IM::pad(SDL_GAMEPAD_BUTTON_DPAD_LEFT)));
        m.defineAction({ "proc.guide", "Show or hide the tour card", "Tour" });
        m.addBinding(IM::bind("proc.guide", IM::key(SDL_SCANCODE_H)));
        m.addBinding(IM::bind("proc.guide", IM::pad(SDL_GAMEPAD_BUTTON_LEFT_STICK)));
        in->commitDefaults();
    }
    auto* ui = m_app->getModule<kke::UiModule>();
    if (ui && ui->context()) {
        m_card = ui->context()->LoadDocumentFromMemory(kCardRml, "procedural-tour");
        if (m_card) m_card->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
    }
    // KKE_PROC_STATION=<n>: start at that station (screenshots). Without
    // it, and with a KKE_PROC_FOCUS or KKE_PROC_VIEW, the camera is left
    // as those set it.
    int start = 0;
    if (const char* s = kke::dev::env("KKE_PROC_STATION")) start = std::atoi(s);
    if (start > 0 || (m_focus < 0 && !kke::dev::env("KKE_PROC_VIEW"))) {
        goToStation(start);
    } else if (m_card) {
        // Keep the camera those switches set; the card still says hello.
        goToStation(0, false);
    }
}

void ProceduralDemoModule::goToStation(int index, bool moveCamera) {
    m_station = std::clamp(index, 0, kStationCount - 1);
    m_stationTime = 0.0f;
    m_autoHits = 0;
    const Station& s = kStations[m_station];
    if (moveCamera) m_focus = s.focus >= 0 && s.focus < static_cast<int>(m_creatures.size()) ? s.focus : -1;
    if (m_orbit && moveCamera) {
        const glm::vec3 target = m_focus >= 0 ? m_creatures[static_cast<size_t>(m_focus)].position + glm::vec3(0.0f, 0.3f, 0.0f)
                                              : glm::vec3(0.5f, 0.3f, 0.0f);
        m_orbit->setView(target, s.distance, glm::radians(s.pitch), glm::radians(s.yaw));
    }
    if (m_station == 1) {
        // The bugs walk to the top of the steps: feet finding each step.
        m_flag = glm::vec3(5.6f, 0.48f, 0.4f);
        m_flagTime = m_time;
    } else if (m_station == 2) {
        m_gaitIndex = 0; // auto: walk, trot and gallop in turn
        m_dogGait = gaitOf(0);
    }
    if (m_card) {
        setText(m_card, "step", std::to_string(m_station + 1) + " / " + std::to_string(kStationCount));
        setText(m_card, "title", s.title);
        setText(m_card, "what", s.what);
        setText(m_card, "use", s.useFor);
        m_cardPad = -1; // updateTour() fills in "try" for the device in use
    }
    kke::log::get(name())->info("tour: {} ({}/{})", s.title, m_station + 1, kStationCount);
}

void ProceduralDemoModule::updateTour(float dt) {
    m_stationTime += dt;
    if (auto* in = m_app->getModule<kke::InputModule>()) {
        const kke::InputMap& m = in->map(0);
        if (m.pressed("proc.next")) goToStation((m_station + 1) % kStationCount);
        if (m.pressed("proc.prev")) goToStation((m_station + kStationCount - 1) % kStationCount);
        if (m.pressed("proc.guide") && m_card) {
            m_cardShown = !m_cardShown;
            if (m_cardShown) m_card->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
            else m_card->Hide();
        }
    }
    // "Try:" for the keyboard and mouse or for a controller, whichever is in use.
    if (m_card) {
        const auto* in = m_app->getModule<kke::InputModule>();
        const kke::PromptStyle style = in ? in->promptStyle(0) : kke::PromptStyle::Keyboard;
        const int pad = style != kke::PromptStyle::Keyboard && style != kke::PromptStyle::Touch ? 1 : 0;
        if (pad != m_cardPad) {
            m_cardPad = pad;
            const Station& s = kStations[m_station];
            setText(m_card, "try", pad ? s.tryPad : s.tryMouse);
        }
    }
    // Station 5: hit the person every few seconds while it's on its feet,
    // light then hard, so the whole cycle plays without a click.
    if (m_station == 4 && m_stationTime > 2.0f + 6.0f * static_cast<float>(m_autoHits)) {
        const int p = creatureIndex(Kind::Person);
        if (p >= 0 && !m_creatures[static_cast<size_t>(p)].handle && !m_creatures[static_cast<size_t>(p)].world.empty()) {
            Creature& c = m_creatures[static_cast<size_t>(p)];
            const glm::vec3 center = glm::vec3(c.world[static_cast<size_t>(c.body)][3]) + glm::vec3(0.0f, 0.1f, 0.0f);
            const glm::vec3 side(std::cos(glm::radians(c.yaw)), 0.0f, -std::sin(glm::radians(c.yaw)));
            hit(c, center, side * (m_autoHits % 2 == 0 ? 3.0f : 7.0f) + glm::vec3(0.0f, 0.5f, 0.0f));
        }
        ++m_autoHits;
    }
}

} // namespace procedural_demo
