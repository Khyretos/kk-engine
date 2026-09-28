#include "kke/modules/MarketplaceUiModule.h"
#include "kke/modules/UiModule.h"
#include "kke/RmlTextSafety.h"
#include "kke/Application.h"
#include "kke/Log.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Element.h>

#include <iostream>
#include <sstream>
#include <typeindex>

namespace kke {

MarketplaceUiModule::MarketplaceUiModule(std::string marketplaceDirectory)
    : m_marketplaceDirectory(std::move(marketplaceDirectory)) {}

std::vector<ModuleDependency> MarketplaceUiModule::dependencies() const {
    return { { std::type_index(typeid(UiModule)), /*required=*/true, "adds its document into UiModule's Rml::Context" } };
}

std::string MarketplaceUiModule::buildDocumentRml() const {
    std::ostringstream rml;
    // A scrolling box needs its scrollbar sized: RmlUi has no default, and
    // without one the text beside it is laid out a word per line.
    rml << R"(<rml><head><title>Marketplace</title><style>)"
        << R"(scrollbarvertical { width: 8dp; } scrollbarvertical slidertrack { background-color: #00000000; })"
        << R"(scrollbarvertical sliderbar { background-color: #3a4670; border-radius: 4dp; min-height: 24dp; })"
        << R"(scrollbarvertical sliderarrowdec, scrollbarvertical sliderarrowinc { height: 0; })"
        // Phone and console (body.kke-phone / .kke-console, set by UiModule):
        // the list folds into a "Games" button at the bottom left so it
        // doesn't fight the game's own panel for the screen; a tap on it
        // opens the list over the whole screen, its Close button folds it
        // again. A PC keeps it open beside the game's panel.
        << R"(#list { display:block; position:absolute; left:12dp; top:12dp; width:45%; max-width:420dp; height:88%; overflow-y:auto; background-color:#1a1d2e; padding:16dp; pointer-events:auto; })"
        << R"(#fold, #close { display:none; })"
        << R"(body.kke-phone #list, body.kke-console #list { left:0dp; top:0dp; width:100%; max-width:none; height:100%; })"
        << R"(body.kke-phone #fold, body.kke-console #fold { display:block; position:absolute; left:12dp; bottom:12dp; padding:8dp 16dp; border-radius:18dp; background-color:#2f6fd6f0; color:#ffffff; font-size:16dp; pointer-events:auto; })"
        << R"(body.kke-phone #close, body.kke-console #close { display:block; float:right; padding:6dp 14dp; border-radius:16dp; background-color:#c9405af0; color:#ffffff; font-size:15dp; pointer-events:auto; })"
        << R"(body.kke-phone.kke-folded #list, body.kke-console.kke-folded #list { display:none; })"
        << R"(body.kke-phone #fold.hidden, body.kke-console #fold.hidden { display:none; })"
        << R"(</style></head>)"
        << R"(<body style="position:absolute; left:0dp; top:0dp; width:100%; height:100%; font-family:Noto Sans; pointer-events:none;">)"
        // pointer-events:none is load-bearing, not decorative: without it,
        // this full-screen body swallowed every mouse hit-test across the
        // ENTIRE window for any document in the same Rml::Context loaded
        // before it — including UiModule's own test document — because
        // RmlUi resolves hit-testing per-context across all its loaded
        // documents, and a later, larger, unstyled-for-interaction
        // full-screen body sits in front of everything hit-test-wise even
        // though its background is fully transparent. Found this by
        // instrumenting Context::GetHoverElement() and seeing it resolve
        // to "body"/"#root" everywhere on screen, not by guessing.
        //
        // left/top as percentages, not pixels -- a real, reported bug
        // this fixes: fixed-pixel positioning kept this panel at the
        // same absolute screen position regardless of actual window
        // size, pushing it partly or entirely off-screen at any
        // resolution other than the 1600x900 this was designed
        // against (confirmed by actually resizing the window and
        // screenshotting the result). 51.25%/15.56% is exactly
        // 820px/140px against that same 1600x900 reference design,
        // just expressed so RmlUi resolves it against whatever the
        // window's own current size actually is.
        //
        // Now on the left edge, at most 45% wide and 90% tall (it
        // scrolls): games put their own panel (DemoPanelModule) on the
        // right, and at 51% the list covered it on a phone held upright,
        // where the screen is only ~900dp wide.
        << R"(<div id="list">)"
        // Confirmed and fixed: display:block was missing from every <p>
        // and <div> below. RmlUi has no built-in "p/div default to
        // block" rule the way a browser does — that behavior in
        // RmlUi's own Samples comes from a *stylesheet* the samples
        // link in (Samples/assets/rml.rcss), not something baked into
        // the engine for every document. This document never linked
        // one, so every element defaulted to inline, which is exactly
        // why titles/ids/descriptions/tags all ran together on one
        // line instead of stacking. Same root cause, same fix, as the
        // rmlui_demo tabset layout bug found earlier this session.
        << R"(<div id="close">Close</div><p class="draggable-handle" style="display:block; font-size:22dp; color:#ffffff; pointer-events:auto; font-family:Noto Sans;">Marketplace (# )" << m_index.count() << R"( games)</p>)";

    for (const auto& game : m_index.games()) {
        // Every field below came from someone else's game.json, not this
        // engine's own code — escapeRmlText is what makes it safe to
        // place directly into markup instead of, say, a malicious title
        // like "</p><div style=\"position:absolute;...\">" being able to
        // restructure this document. See kke/RmlTextSafety.h.
        std::string title = escapeRmlText(game.title);
        std::string description = escapeRmlText(game.description);
        std::string id = escapeRmlText(game.id);

        std::string tags;
        for (const auto& tag : game.tags) {
            if (!tags.empty()) tags += ", ";
            tags += escapeRmlText(tag);
        }

        rml << R"(<div style="display:block; margin-top:12dp; padding:10dp; background-color:#2a2f4a;">)"
            << R"(<p style="display:block; font-size:18dp; color:#ffffff; font-family:Noto Sans;">)" << title << "</p>"
            << R"(<p style="display:block; font-size:13dp; color:#9aa0c0; font-family:Noto Sans;">)" << id << "</p>";
        if (!description.empty()) {
            rml << R"(<p style="display:block; font-size:14dp; color:#c8ccdc; margin-top:6dp; font-family:Noto Sans;">)" << description << "</p>";
        }
        if (!tags.empty()) {
            rml << R"(<p style="display:block; font-size:12dp; color:#7fd8a0; margin-top:6dp; font-family:Noto Sans;">)" << tags << "</p>";
        }
        rml << "</div>";
    }

    rml << "</div>";
    rml << R"(<div id="fold">Games ()" << m_index.count() << ")</div>";
    rml << "</body></rml>";
    return rml.str();
}

void MarketplaceUiModule::init(Application& app) {
    m_index.scanDirectory(m_marketplaceDirectory);
    std::cout << "[marketplace-ui] scanned '" << m_marketplaceDirectory << "': "
               << m_index.count() << " game(s) found" << std::endl;

    UiModule* ui = app.getModule<UiModule>();
    if (!ui || !ui->context()) {
        std::cerr << "[marketplace-ui] UiModule/context not available — nothing will render" << std::endl;
        return;
    }

    std::string rml = buildDocumentRml();
    m_document = ui->context()->LoadDocumentFromMemory(rml);
    if (m_document) {
        // Folded to start with (only shows on a phone or console, see the
        // style sheet above).
        m_document->SetClass("kke-folded", true);
        m_foldListener = std::make_unique<FoldListener>(*this);
        if (Rml::Element* fold = m_document->GetElementById("fold")) fold->AddEventListener(Rml::EventId::Click, m_foldListener.get());
        if (Rml::Element* close = m_document->GetElementById("close")) close->AddEventListener(Rml::EventId::Click, m_foldListener.get());
        m_document->Show();
    } else {
        std::cerr << "[marketplace-ui] generated document failed to load — see rmlui log output above" << std::endl;
    }
}

void MarketplaceUiModule::FoldListener::ProcessEvent(Rml::Event& event) {
    Rml::ElementDocument* doc = m_owner.m_document;
    if (!doc) return;
    const bool opening = event.GetCurrentElement() && event.GetCurrentElement()->GetId() == "fold";
    doc->SetClass("kke-folded", !opening);
    if (Rml::Element* fold = doc->GetElementById("fold")) fold->SetClass("hidden", opening);
}

void MarketplaceUiModule::shutdown() {
    if (m_document) {
        if (Rml::Element* fold = m_document->GetElementById("fold")) fold->RemoveEventListener(Rml::EventId::Click, m_foldListener.get());
        if (Rml::Element* close = m_document->GetElementById("close")) close->RemoveEventListener(Rml::EventId::Click, m_foldListener.get());
    }
    m_document = nullptr;
}

} // namespace kke
