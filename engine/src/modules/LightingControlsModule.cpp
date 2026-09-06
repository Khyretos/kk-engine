#include "kke/modules/LightingControlsModule.h"
#include "kke/Application.h"
#include "kke/modules/UiModule.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/EventListener.h>

#include <stdexcept>
#include <cstdlib>

namespace kke {

namespace {

// Same absolute-positioning convention already proven working
// throughout every other RML document in this engine (RmlUiShowcaseModule,
// MarketplaceUiModule) -- positioned where UiModule's old test boxes
// used to sit, so every demo that already had screen space reserved
// there keeps looking the same shape, just with real content now.
const char* kLightingControlsRml = R"(
<rml>
<head>
    <title>Lighting Controls</title>
    <style>
        body { color: #ffffff; font-family: Noto Sans; }
        .panel { position: absolute; left: 40px; top: 500px; width: 740px; background-color: #1a1d2e; padding: 14px; }
        .panel h1 { display: block; font-size: 18px; color: #a0c8ff; margin-bottom: 8px; }
        .row { display: block; margin-top: 8px; }
        .row span { display: inline-block; width: 160px; }
        input.range { width: 300px; height: 18px; vertical-align: -4px; }
        input.range slidertrack { display: block; width: 300px; height: 12px; margin-top: 3px; background-color: #333344; border: 1px #666677; }
        input.range sliderbar { display: block; width: 16px; height: 16px; margin-top: -3px; background-color: #4a7ac9; border: 1px #6a9aee; }
        input.range sliderbar:hover { background-color: #5a8ad9; }
        button { display: inline-block; background-color: #333344; color: #ffffff; padding: 6px 14px; border: 1px #666677; margin-top: 10px; margin-right: 8px; }
        button:hover { background-color: #4a7ac9; }
    </style>
</head>
<body style="width:1280px; height:720px;">
    <div id="lighting-controls-panel" class="panel">
        <h1>Lighting Controls -- real kke::Lighting state, changed live</h1>
        <div class="row"><span>Ambient</span><input id="ambient" type="range" min="0" max="1" step="0.01" value="0.15"/></div>
        <div class="row"><span>Key light intensity</span><input id="key-intensity" type="range" min="0" max="3" step="0.05" value="1.0"/></div>
        <div class="row"><span>Fill light intensity</span><input id="fill-intensity" type="range" min="0" max="2" step="0.05" value="0.0"/></div>
        <button id="preset-warm">Warm key + cool fill</button>
        <button id="preset-dramatic">Dramatic (low ambient)</button>
        <button id="preset-flat">Flat (bright ambient)</button>
        <button id="preset-reset">Reset to default</button>
    </div>
</body>
</rml>
)";

float readSliderValue(Rml::Element* element, float fallback) {
    if (!element) return fallback;
    if (auto* control = dynamic_cast<Rml::ElementFormControl*>(element)) {
        try {
            return std::stof(control->GetValue());
        } catch (...) {
            return fallback;
        }
    }
    return fallback;
}

} // namespace

// A real Rml::EventListener, not a workaround -- button clicks already
// dispatch correctly through RmlUi's normal event path (proven
// elsewhere in this engine: tabs, "Submit", radio/checkbox all already
// work via the standard path), so this needs none of the click-
// position handling UiModule applies specifically to input.range.
class LightingControlsModule::PresetButtonListener : public Rml::EventListener {
public:
    explicit PresetButtonListener(Application* app) : m_app(app) {}

    void ProcessEvent(Rml::Event& event) override {
        Rml::Element* target = event.GetTargetElement();
        if (!target || !m_app) return;
        const Rml::String& id = target->GetId();

        Lighting& lighting = m_app->lighting();
        Rml::ElementDocument* doc = target->GetOwnerDocument();
        auto setSlider = [doc](const char* elementId, float value) {
            if (Rml::Element* el = doc->GetElementById(elementId)) {
                if (auto* control = dynamic_cast<Rml::ElementFormControl*>(el)) {
                    control->SetValue(std::to_string(value));
                }
            }
        };

        if (id == "preset-warm") {
            lighting.ambientColor = glm::vec3(0.15f, 0.15f, 0.15f);
            lighting.lights[0].color = glm::vec3(1.0f, 0.85f, 0.65f);
            lighting.lights[0].intensity = 1.0f;
            lighting.lights[1].enabled = true;
            lighting.lights[1].isDirectional = true;
            lighting.lights[1].direction = glm::normalize(glm::vec3(0.6f, -0.3f, 0.5f));
            lighting.lights[1].color = glm::vec3(0.55f, 0.65f, 0.85f);
            lighting.lights[1].intensity = 0.4f;
            setSlider("ambient", lighting.ambientColor.x);
            setSlider("key-intensity", lighting.lights[0].intensity);
            setSlider("fill-intensity", lighting.lights[1].intensity);
        } else if (id == "preset-dramatic") {
            lighting.ambientColor = glm::vec3(0.03f, 0.03f, 0.05f);
            lighting.lights[0].intensity = 1.6f;
            lighting.lights[1].enabled = false;
            setSlider("ambient", lighting.ambientColor.x);
            setSlider("key-intensity", lighting.lights[0].intensity);
            setSlider("fill-intensity", 0.0f);
        } else if (id == "preset-flat") {
            lighting.ambientColor = glm::vec3(0.6f, 0.6f, 0.6f);
            lighting.lights[0].intensity = 0.5f;
            lighting.lights[1].enabled = false;
            setSlider("ambient", lighting.ambientColor.x);
            setSlider("key-intensity", lighting.lights[0].intensity);
            setSlider("fill-intensity", 0.0f);
        } else if (id == "preset-reset") {
            lighting = Lighting{}; // real default-constructed state, not hand-copied duplicate values
            lighting.lights[1].enabled = false;
            setSlider("ambient", lighting.ambientColor.x);
            setSlider("key-intensity", lighting.lights[0].intensity);
            setSlider("fill-intensity", 0.0f);
        }
    }

private:
    Application* m_app;
};

// Defined here, not defaulted in the header — PresetButtonListener is
// only forward-declared there (via std::unique_ptr<PresetButtonListener>
// in LightingControlsModule.h), and unique_ptr's destructor needs the
// complete type to know how to delete it. An implicit/defaulted
// destructor at the header's point of use would need that complete
// type too early; a real destructor defined here, where
// PresetButtonListener's full definition is already visible just
// above, resolves it correctly. The body can still just be "= default"
// — this exists for the compiler's benefit, not because any custom
// cleanup logic is actually needed beyond what the members already do.
LightingControlsModule::~LightingControlsModule() = default;

LightingControlsModule::LightingControlsModule(float left, float top) : m_left(left), m_top(top) {}

void LightingControlsModule::init(Application& app) {
    m_app = &app;
    UiModule* ui = app.getModule<UiModule>();
    if (!ui || !ui->context()) {
        throw std::runtime_error("LightingControlsModule requires kke::UiModule to be added and initialised first");
    }

    m_document = ui->context()->LoadDocumentFromMemory(kLightingControlsRml);
    if (!m_document) {
        throw std::runtime_error("LightingControlsModule: LoadDocumentFromMemory failed");
    }
    m_document->Show();

    // Real repositioning via SetProperty, not baked into the RML/RCSS
    // string — lets more than one instance of this kind of content
    // module share a screen at different positions instead of always
    // landing at the same hardcoded spot. Only actually moves anything
    // when a caller passes non-default left/top; existing call sites
    // that don't keep rendering exactly where they always did.
    if (Rml::Element* panel = m_document->GetElementById("lighting-controls-panel")) {
        panel->SetProperty("left", std::to_string(m_left) + "px");
        panel->SetProperty("top", std::to_string(m_top) + "px");
    }

    m_listener = new PresetButtonListener(m_app);
    for (const char* buttonId : { "preset-warm", "preset-dramatic", "preset-flat", "preset-reset" }) {
        if (Rml::Element* button = m_document->GetElementById(buttonId)) {
            button->AddEventListener(Rml::EventId::Click, m_listener);
        }
    }
}

void LightingControlsModule::update(const UpdateContext& /*ctx*/) {
    if (!m_document || !m_app) return;

    // Polled once per frame rather than driven by RmlUi's own Change
    // event -- see the class comment for why this deliberate
    // simplicity is the right call for the small number of values
    // here.
    float ambient = readSliderValue(m_document->GetElementById("ambient"), m_lastAmbient);
    float keyIntensity = readSliderValue(m_document->GetElementById("key-intensity"), m_lastKeyIntensity);
    float fillIntensity = readSliderValue(m_document->GetElementById("fill-intensity"), m_lastFillIntensity);

    if (ambient != m_lastAmbient) {
        m_app->lighting().ambientColor = glm::vec3(ambient);
        m_lastAmbient = ambient;
    }
    if (keyIntensity != m_lastKeyIntensity) {
        m_app->lighting().lights[0].intensity = keyIntensity;
        m_lastKeyIntensity = keyIntensity;
    }
    if (fillIntensity != m_lastFillIntensity) {
        m_app->lighting().lights[1].enabled = (fillIntensity > 0.0f);
        m_app->lighting().lights[1].isDirectional = true;
        if (m_app->lighting().lights[1].intensity <= 0.0f) {
            // First time fill gets turned on from a slider (rather than
            // a preset button, which sets a real direction/color of its
            // own) -- give it a sensible default direction/color so
            // it's not just a same-angle duplicate of the key light.
            m_app->lighting().lights[1].direction = glm::normalize(glm::vec3(0.6f, -0.3f, 0.5f));
            m_app->lighting().lights[1].color = glm::vec3(0.55f, 0.65f, 0.85f);
        }
        m_app->lighting().lights[1].intensity = fillIntensity;
        m_lastFillIntensity = fillIntensity;
    }
}

void LightingControlsModule::shutdown() {
    if (m_document) {
        m_document->Close();
        m_document = nullptr;
    }
    delete m_listener;
    m_listener = nullptr;
}

} // namespace kke
