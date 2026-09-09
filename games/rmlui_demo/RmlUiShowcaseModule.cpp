#include "RmlUiShowcaseModule.h"
#include "kke/Application.h"
#include "kke/modules/UiModule.h"
#include "kke/Log.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Debugger.h>
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
        // pointer-events: none on body -- see LightingControlsModule's
        // own comment (and MarketplaceUiModule's earlier, already-
        // proven version of this exact fix) for the full account:
        // every document's body covers the full viewport for hit-
        // testing by default, and rmlui_demo shows several separate
        // documents/panels at once, so without this the most-recently-
        // shown one's invisible body would absorb clicks meant for
        // panels behind it.
        body { color: #ffffff; font-family: Noto Sans; pointer-events: none; }
        .panel { position: absolute; background-color: #1e1e2e; padding: 12px; border: 1px #555555; pointer-events: auto; font-family: Noto Sans; }
        .panel h1 { font-size: 16px; color: #a0c8ff; margin-bottom: 8px; font-family: Noto Sans; }
        input.text { background-color: #333344; color: #ffffff; padding: 4px; border: 1px #666677; width: 200px; font-family: Noto Sans; }
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
        select { color: #ffffff; width: 200px; font-family: Noto Sans; }
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
        select selectbox option { padding: 4px 8px; color: #dddddd; font-family: Noto Sans; }
        select selectbox option:checked { background-color: #3d3d4f; font-weight: bold; }
        select selectbox option:hover { background-color: #4a7ac9; color: #ffffff; }
        textarea { background-color: #333344; color: #ffffff; padding: 4px; width: 200px; height: 44px; border: 1px #666677; font-family: Noto Sans; }
        button { background-color: #4a7ac9; color: #ffffff; padding: 6px 14px; border: 1px #6a9aee; font-family: Noto Sans; }
        button:hover { background-color: #5a8ad9; }
        tabset tabs { display: block; }
        tabset tab { background-color: #333344; color: #cccccc; padding: 4px 12px; font-family: Noto Sans; }
        tabset tab:selected { background-color: #4a7ac9; color: #ffffff; }
        tabset panel { display: block; background-color: #262636; color: #ffffff; padding: 10px; font-family: Noto Sans; }
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
<body style="width:1280px; height:720px; pointer-events:none;">

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

    <div class="panel" style="left:700px; top:560px; width:270px;">
        <h1>Real &lt;img&gt;, not a stub</h1>
        <img src="assets/textures/test_icon.png" style="display:block; width:64px; height:64px;"/>
        <div style="display:block; width:64px; height:64px; margin-top:8px; decorator: image(assets/textures/test_icon.png);"/>
        <p style="display:block; margin-top:8px;">Decoded with stb_image, uploaded through the same real GPU
           texture path font glyphs already use -- LoadTexture is no
           longer a 1x1-white stand-in. Below: the same file via RCSS
           background-image's own decorator mechanism, a genuinely
           separate code path worth verifying independently.</p>
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

    // A real diagnostic tool, not a permanent feature -- wired in
    // specifically to investigate the range slider not responding to
    // clicks or drags, after CSS-level fixes that worked immediately
    // for checkbox/radio had no effect here. Rml::Debugger ships with
    // RmlUi itself (Source/Debugger, already built unconditionally as
    // part of this project's existing RmlUi fetch -- confirmed by
    // checking Source/CMakeLists.txt directly, not assumed) and gives
    // a real element inspector: click its own "click element to
    // inspect" tool, then click the slider, and see its actual
    // computed box/hit-test target directly instead of guessing from
    // screenshots.
    if (!Rml::Debugger::Initialise(ui->context())) {
        kke::log::get(name())->warn("Rml::Debugger::Initialise failed -- debugger will not be available");
    } else {
        // Visible=false by default -- a dev tool, not something a
        // demo's normal viewer should see unprompted. Its "Outlines"
        // tool used to crash reliably (confirmed via a real gdb
        // backtrace, deep inside the lavapipe driver itself with no
        // usable symbols) -- that's genuinely fixed now, not just
        // avoided: installing real Vulkan validation layers in this
        // same sandbox (which had none before) turned the opaque
        // segfault into an exact, actionable error --
        // "vkCmdWriteTimestamp(): was called in VkCommandBuffer ...
        // which is invalid because bound VkBuffer ... was destroyed."
        // The real bug was in this engine's own RmlVulkanRenderInterface,
        // not RmlUi or the driver: ReleaseGeometry()/ReleaseTexture()
        // destroyed their underlying GPU resources immediately, with
        // no check that the GPU had actually finished using them --
        // fine for normal RmlUi content, which rarely releases
        // geometry mid-session, but Outlines churns through far more
        // temporary geometry per frame than anything else in this
        // engine ever has, making the race far more likely to actually
        // hit. Fixed with real deferred destruction (see
        // RmlVulkanRenderInterface.h's own PendingDeletion comment for
        // the full account) and reverified: Outlines now renders real
        // red borders around every visible element, exactly as
        // intended, with zero validation errors and zero crashes
        // across a real, sustained run.
        Rml::Debugger::SetVisible(false);
    }
}

void RmlUiShowcaseModule::shutdown() {
    if (m_document) {
        m_document->Close();
        m_document = nullptr;
    }
}

} // namespace kke_demo
