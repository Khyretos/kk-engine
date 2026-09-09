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
    rml << R"(<rml><head><title>Marketplace</title></head>)"
        << R"(<body style="position:absolute; left:0px; top:0px; width:1280px; height:720px; font-family:Noto Sans; pointer-events:none;">)"
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
        << R"(<div style="display:block; position:absolute; left:51.25%; top:15.56%; width:420px; background-color:#1a1d2e; padding:16px; pointer-events:auto;">)"
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
        << R"(<p class="draggable-handle" style="display:block; font-size:22px; color:#ffffff; pointer-events:auto; font-family:Noto Sans;">Marketplace (# )" << m_index.count() << R"( games)</p>)";

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

        rml << R"(<div style="display:block; margin-top:12px; padding:10px; background-color:#2a2f4a;">)"
            << R"(<p style="display:block; font-size:18px; color:#ffffff; font-family:Noto Sans;">)" << title << "</p>"
            << R"(<p style="display:block; font-size:13px; color:#9aa0c0; font-family:Noto Sans;">)" << id << "</p>";
        if (!description.empty()) {
            rml << R"(<p style="display:block; font-size:14px; color:#c8ccdc; margin-top:6px; font-family:Noto Sans;">)" << description << "</p>";
        }
        if (!tags.empty()) {
            rml << R"(<p style="display:block; font-size:12px; color:#7fd8a0; margin-top:6px; font-family:Noto Sans;">)" << tags << "</p>";
        }
        rml << "</div>";
    }

    rml << "</div></body></rml>";
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
        m_document->Show();
    } else {
        std::cerr << "[marketplace-ui] generated document failed to load — see rmlui log output above" << std::endl;
    }
}

} // namespace kke
