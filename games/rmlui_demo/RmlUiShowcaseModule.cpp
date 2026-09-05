#include "RmlUiShowcaseModule.h"
#include "kke/Application.h"
#include "kke/modules/UiModule.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>
#include <stdexcept>

namespace kke_demo {

namespace {

// Absolute positioning throughout, matching UiModule's own existing
// test document's proven-working style convention. Deliberately
// confined to x:[700,1270] y:[20,540] -- checked directly against
// this demo's actual ImGui panel positions (StatsModule's Performance
// panel at (10,10)-(330,~160); DebugControlModule's own panel at
// (340,250)-(680,~420)) and UiModule's own default test document
// (three boxes at y:560-700) rather than guessed, after the first
// version of this layout badly overlapped all three.
const char* kShowcaseRml = R"(
<rml>
<head>
    <title>RmlUi Showcase</title>
    <style>
        body { color: #ffffff; font-family: Noto Sans; }
        .panel { position: absolute; background-color: #1e1e2e; padding: 12px; border: 1px #555555; }
        .panel h1 { font-size: 16px; color: #a0c8ff; margin-bottom: 8px; }
        input.text { background-color: #333344; color: #ffffff; padding: 4px; border: 1px #666677; width: 200px; }
        /* Same root cause found for the select/slider fixes above,
           discovered while investigating why nothing here responded to
           clicks at all: RmlUi auto-assigns a .checkbox/.radio class
           to these inputs (confirmed against the same real RmlUi
           sample, invader.rcss), but nothing here ever styled them --
           so they rendered at their default near-zero size, meaning
           there was no visible glyph AND no meaningfully clickable
           area. Not a hit-testing bug; there was genuinely nothing
           there to hit. */
        input.checkbox, input.radio { width: 16px; height: 16px; background-color: #333344; border: 1px #666677; vertical-align: -3px; }
        input.checkbox:hover, input.radio:hover { background-color: #3d3d4f; }
        input.checkbox:checked, input.radio:checked { background-color: #4a7ac9; border: 1px #6a9aee; }
        select { color: #ffffff; width: 200px; }
        /* select's own outer box was the only thing styled before --
           the actual VISIBLE parts are named sub-elements RmlUi creates
           internally (confirmed against a real, working RmlUi sample,
           Samples/assets/invader.rcss, not guessed): selectvalue (the
           closed box showing the current choice), selectarrow (the
           dropdown indicator), and selectbox (the popup list) with its
           own option children. None of these inherit from `select`
           automatically, which is exactly why the dropdown had no
           visible background or hover effect before this fix -- the
           parts a user actually sees and clicks were never styled at all. */
        select selectvalue { display: block; background-color: #333344; padding: 4px 8px; border: 1px #666677; }
        select selectvalue:hover { background-color: #3d3d4f; }
        select selectarrow { width: 22px; background-color: #43435a; }
        select selectbox { background-color: #2a2a3a; border: 1px #666677; padding: 2px; margin-top: 2px; }
        select selectbox option { padding: 4px 8px; color: #dddddd; }
        select selectbox option:checked { background-color: #3d3d4f; font-weight: bold; }
        select selectbox option:hover { background-color: #4a7ac9; color: #ffffff; }
        textarea { background-color: #333344; color: #ffffff; padding: 4px; width: 200px; height: 44px; border: 1px #666677; }
        button { background-color: #4a7ac9; color: #ffffff; padding: 6px 14px; border: 1px #6a9aee; }
        button:hover { background-color: #5a8ad9; }
        tabset tabs { display: block; }
        tabset tab { background-color: #333344; color: #cccccc; padding: 4px 12px; }
        tabset tab:selected { background-color: #4a7ac9; color: #ffffff; }
        tabset panel { display: block; background-color: #262636; color: #ffffff; padding: 10px; }
        /* Same class of gap as the select dropdown above: only the
           outer <input type="range"> box existed in CSS before, never
           its actual visible/draggable parts. slidertrack is the
           groove; sliderbar is the real draggable thumb -- without an
           explicit size and color, it rendered with nothing to see or
           grab, which is exactly why the slider "didn't work." Verified
           against the same real RmlUi sample as the dropdown fix above. */
        input.range { width: 200px; height: 20px; }
        input.range slidertrack { display: block; width: 200px; height: 14px; margin-top: 3px; background-color: #333344; border: 1px #666677; }
        input.range sliderbar { display: block; width: 18px; height: 18px; margin-top: -2px; background-color: #4a7ac9; border: 1px #6a9aee; }
        input.range sliderbar:hover { background-color: #5a8ad9; }
        input.range sliderbar:active { background-color: #3a6ab9; }
        progress { width: 200px; height: 16px; }
        progress fill { background-color: #4a7ac9; }
        progress df-fill { background-color: #4a7ac9; }
    </style>
</head>
<body style="width:1280px; height:720px;">

    <div class="panel" style="left:700px; top:20px; width:250px;">
        <h1>Text input, button, textarea</h1>
        <input class="text" type="text" value="Kreative Kompas" /><br/><br/>
        <button>Submit</button><br/><br/>
        <textarea>Multi-line &lt;textarea&gt;, merged into RmlUi Core.</textarea>
    </div>

    <div class="panel" style="left:990px; top:20px; width:270px;">
        <h1>Checkbox + radio</h1>
        <input type="checkbox" checked="checked" /> Enable feature<br/><br/>
        <input type="radio" name="mode" checked="checked" /> Mode A<br/>
        <input type="radio" name="mode" /> Mode B<br/>
        <input type="radio" name="mode" /> Mode C
    </div>

    <div class="panel" style="left:700px; top:230px; width:250px;">
        <h1>Range slider + progress</h1>
        <input type="range" min="0" max="100" value="65" /><br/><br/>
        <progress direction="right" value="0.65"/>
    </div>

    <div class="panel" style="left:990px; top:230px; width:270px;">
        <h1>Select dropdown</h1>
        <select>
            <option value="wood">Wood (density 700)</option>
            <option value="steel">Steel (density 7870)</option>
            <option value="rubber">Rubber (density 1100)</option>
        </select>
    </div>

    <div class="panel" style="left:700px; top:390px; width:560px; height:150px;">
        <h1>Tabset</h1><br/>
        <tabset>
            <tab>Overview</tab>
            <panel>RmlUi's built-in &lt;tabset&gt; element -- click the other tabs.</panel>
            <tab>Details</tab>
            <panel>Each &lt;tab&gt; is paired with the &lt;panel&gt; that follows it, in order.</panel>
            <tab>Credits</tab>
            <panel>RmlUi 6.3, real Vulkan text rendering via a bundled Noto Sans font.</panel>
        </tabset>
    </div>

</body>
</rml>
)";

} // namespace

void RmlUiShowcaseModule::init(kke::Application& app) {
    kke::UiModule* ui = app.getModule<kke::UiModule>();
    if (!ui || !ui->context()) {
        throw std::runtime_error("RmlUiShowcaseModule requires kke::UiModule to be added and initialised first");
    }
    m_document = ui->context()->LoadDocumentFromMemory(kShowcaseRml);
    if (!m_document) {
        throw std::runtime_error("RmlUiShowcaseModule: LoadDocumentFromMemory failed");
    }
    m_document->Show();
}

void RmlUiShowcaseModule::shutdown() {
    if (m_document) {
        m_document->Close();
        m_document = nullptr;
    }
}

} // namespace kke_demo
