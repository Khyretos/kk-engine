#include "kke/modules/MaterialGridModule.h"

#if KKE_ENABLE_FEMFX

#include "kke/Application.h"
#include "kke/Log.h"
#include "kke/modules/UiModule.h"
#include "kke/modules/PhysicsModule.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/EventListener.h>

#include <stdexcept>
#include <vector>
#include <string>

namespace kke {

namespace {

struct MaterialPreset {
    const char* id;
    const char* label;
    const char* swatchColor;
    Material material;
};

// Real, differentiated values, not five copies of the same numbers with
// different names -- each chosen to match Material.h's own documented
// intent (wood bends, glass is brittle and snaps near its yield point,
// rubber barely breaks at all).
const MaterialPreset kPresets[] = {
    { "wood",   "Wood",   "#8a5a2b", []{ Material m; m.density=600.0f;  m.stiffness=1.0e7f; m.poissonsRatio=0.30f; m.fractureStressThreshold=3.0e5f; m.plasticYieldThreshold=1.0e5f; m.plasticCreep=0.3f; m.metallic=0.0f; m.roughness=0.75f; return m; }() },
    { "stone",  "Stone",  "#7a7a7a", []{ Material m; m.density=2500.0f; m.stiffness=3.0e7f; m.poissonsRatio=0.25f; m.fractureStressThreshold=1.0e5f; m.plasticYieldThreshold=9.0e4f; m.plasticCreep=0.1f; m.metallic=0.0f; m.roughness=0.9f; return m; }() },
    { "iron",   "Iron",   "#b0b8c0", []{ Material m; m.density=7870.0f; m.stiffness=2.0e8f; m.poissonsRatio=0.30f; m.fractureStressThreshold=5.0e8f; m.plasticYieldThreshold=2.0e8f; m.plasticCreep=0.2f; m.metallic=0.9f; m.roughness=0.35f; return m; }() },
    { "rubber", "Rubber", "#2b2b2b", []{ Material m; m.density=1200.0f; m.stiffness=1.0e5f; m.poissonsRatio=0.45f; m.fractureStressThreshold=1.0e7f; m.plasticYieldThreshold=5.0e6f; m.plasticCreep=0.05f; m.metallic=0.0f; m.roughness=0.95f; return m; }() },
    { "glass",  "Glass",  "#bfe3f0", []{ Material m; m.density=2500.0f; m.stiffness=7.0e7f; m.poissonsRatio=0.22f; m.fractureStressThreshold=5.0e4f; m.plasticYieldThreshold=4.0e4f; m.plasticCreep=0.02f; m.metallic=0.0f; m.roughness=0.05f; return m; }() },
};
constexpr int kNumPresets = sizeof(kPresets) / sizeof(kPresets[0]);

const char* kMaterialGridRml = R"(
<rml>
<head>
    <title>Material Grid</title>
    <style>
        body { color: #ffffff; font-family: Noto Sans; }
        /* Narrower than this module's first version (740px -> 440px,
           cards 110px -> 78px) — the original width assumed this panel
           had the full bottom row to itself, which was true in
           physics_demo but not once kke_demo (see README "Complete
           kke_demo showcase") needed to fit this beside
           LightingControlsModule's own panel on the same screen. */
        .gridpanel { position: absolute; left: 40px; top: 500px; width: 460px; background-color: #1a1d2e; padding: 14px; }
        .gridpanel h1 { display: block; font-size: 15px; color: #a0c8ff; margin-bottom: 10px; }
        .card { display: inline-block; width: 70px; height: 118px; margin-right: 6px; background-color: #262636; border: 2px #333344; padding: 4px; text-align: center; }
        .card:hover { border: 2px #6a9aee; background-color: #2d2d42; }
        .card.selected { border: 2px #4a7ac9; background-color: #2a3a5a; }
        .swatch { display: block; width: 44px; height: 44px; margin-left: auto; margin-right: auto; margin-bottom: 5px; border: 1px #00000080; }
        .cardlabel { display: block; font-size: 12px; color: #ffffff; }
        .cardstat { display: block; font-size: 10px; color: #9aa0c0; }
        .hint { display: block; margin-top: 10px; font-size: 12px; color: #7fd8a0; }
    </style>
</head>
<body style="width:1280px; height:720px;">
    <div id="material-grid-panel" class="gridpanel">
        <h1>Material Grid -- select what "Spawn tetrahedron" spawns next</h1>
        <div id="card-wood" class="card selected">
            <div class="swatch" style="background-color:#8a5a2b;"/>
            <p class="cardlabel">Wood</p>
            <p class="cardstat">density 6.0e+02</p>
            <p class="cardstat">stiff 1.0e+07</p>
        </div>
        <div id="card-stone" class="card">
            <div class="swatch" style="background-color:#7a7a7a;"/>
            <p class="cardlabel">Stone</p>
            <p class="cardstat">density 2.5e+03</p>
            <p class="cardstat">stiff 3.0e+07</p>
        </div>
        <div id="card-iron" class="card">
            <div class="swatch" style="background-color:#b0b8c0;"/>
            <p class="cardlabel">Iron</p>
            <p class="cardstat">density 7.9e+03</p>
            <p class="cardstat">stiff 2.0e+08</p>
        </div>
        <div id="card-rubber" class="card">
            <div class="swatch" style="background-color:#2b2b2b;"/>
            <p class="cardlabel">Rubber</p>
            <p class="cardstat">density 1.2e+03</p>
            <p class="cardstat">stiff 1.0e+05</p>
        </div>
        <div id="card-glass" class="card">
            <div class="swatch" style="background-color:#bfe3f0;"/>
            <p class="cardlabel">Glass</p>
            <p class="cardstat">density 2.5e+03</p>
            <p class="cardstat">stiff 7.0e+07</p>
        </div>
        <p class="hint" id="selection-hint">Selected: Wood</p>
    </div>
</body>
</rml>
)";

} // namespace

// A real Rml::EventListener, same pattern as
// LightingControlsModule::PresetButtonListener -- button/div clicks
// already dispatch correctly through RmlUi's normal event path (unlike
// input.range, see UiModule.cpp's own comment on that real, separately
// diagnosed quirk), so no click-position workaround is needed here.
class MaterialGridModule::CardClickListener : public Rml::EventListener {
public:
    CardClickListener(Application* app, Rml::ElementDocument* document) : m_app(app), m_document(document) {}

    void ProcessEvent(Rml::Event& event) override {
        // GetCurrentElement(), not GetTargetElement() -- a real bug,
        // found by actually clicking a card and watching nothing
        // happen: each card has child elements (the swatch div, the
        // label/stat <p> tags), and RmlUi's click event targets
        // whichever of those the cursor is actually over, not
        // necessarily the card div this listener is attached to.
        // GetCurrentElement() returns the element the listener itself
        // is registered on, which is always the card -- correct
        // regardless of which specific child inside it was clicked.
        Rml::Element* target = event.GetCurrentElement();
        if (!target || !m_app || !m_document) return;
        const Rml::String& id = target->GetId();

        for (int i = 0; i < kNumPresets; ++i) {
            std::string cardId = std::string("card-") + kPresets[i].id;
            if (id != cardId) continue;

            PhysicsModule* physics = m_app->getModule<PhysicsModule>();
            if (physics) {
                physics->selectedMaterial() = kPresets[i].material;
            }

            // Real visual selection state, not just internal data --
            // toggle the "selected" class off every card, on for the
            // one actually clicked, matching a real inventory grid's
            // own feedback rather than a silent state change.
            for (int j = 0; j < kNumPresets; ++j) {
                std::string otherCardId = std::string("card-") + kPresets[j].id;
                if (Rml::Element* card = m_document->GetElementById(otherCardId)) {
                    card->SetClass("selected", j == i);
                }
            }
            if (Rml::Element* hint = m_document->GetElementById("selection-hint")) {
                hint->SetInnerRML(Rml::String("Selected: ") + kPresets[i].label);
            }
            break;
        }
    }

private:
    Application* m_app;
    Rml::ElementDocument* m_document;
};

MaterialGridModule::~MaterialGridModule() = default;

MaterialGridModule::MaterialGridModule(float left, float top) : m_left(left), m_top(top) {}

void MaterialGridModule::init(Application& app) {
    m_app = &app;
    UiModule* ui = app.getModule<UiModule>();
    if (!ui || !ui->context()) {
        throw std::runtime_error("MaterialGridModule requires kke::UiModule to be added and initialised first");
    }
    if (!app.getModule<PhysicsModule>()) {
        throw std::runtime_error("MaterialGridModule requires kke::PhysicsModule to be added and initialised first");
    }

    m_document = ui->context()->LoadDocumentFromMemory(kMaterialGridRml);
    if (!m_document) {
        throw std::runtime_error("MaterialGridModule: LoadDocumentFromMemory failed");
    }
    m_document->Show();

    // See LightingControlsModule.cpp's own identical comment on this
    // same pattern — real repositioning via SetProperty, not baked
    // into the RML/RCSS string.
    if (Rml::Element* panel = m_document->GetElementById("material-grid-panel")) {
        panel->SetProperty("left", std::to_string(m_left) + "px");
        panel->SetProperty("top", std::to_string(m_top) + "px");
        kke::log::get(name())->info("MaterialGrid panel repositioned to left={} top={}", m_left, m_top);
    } else {
        kke::log::get(name())->warn("MaterialGrid: could not find element 'material-grid-panel' to reposition");
    }

    // Wood is the grid's own default-selected card -- set explicitly
    // here too, not left to coincidence, so this module's own default
    // and PhysicsModule's actual state are guaranteed to agree.
    if (PhysicsModule* physics = app.getModule<PhysicsModule>()) {
        physics->selectedMaterial() = kPresets[0].material;
    }

    m_listener = new CardClickListener(m_app, m_document);
    for (int i = 0; i < kNumPresets; ++i) {
        std::string cardId = std::string("card-") + kPresets[i].id;
        if (Rml::Element* card = m_document->GetElementById(cardId)) {
            card->AddEventListener(Rml::EventId::Click, m_listener);
        }
    }
}

void MaterialGridModule::shutdown() {
    if (m_document) {
        m_document->Close();
        m_document = nullptr;
    }
    delete m_listener;
    m_listener = nullptr;
}

} // namespace kke

#endif // KKE_ENABLE_FEMFX
