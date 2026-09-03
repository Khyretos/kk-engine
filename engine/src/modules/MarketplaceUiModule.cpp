#include "kke/modules/MarketplaceUiModule.h"
#include "kke/modules/UiModule.h"
#include "kke/RmlTextSafety.h"
#include "kke/Application.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>

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
        << R"(<div style="position:absolute; left:820px; top:140px; width:420px; background-color:#1a1d2e; padding:16px;">)"
        // NOTE: card <p> elements currently render running into each
        // other rather than stacking on separate lines — a block-layout/
        // RCSS detail (verified: not a data or escaping problem, the
        // content itself is complete and correct) worth revisiting when
        // this gets real visual design attention, not blocking on it now.
        << R"(<p style="font-size:22px; color:#ffffff;">Marketplace (# )" << m_index.count() << R"( games)</p>)";

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

        rml << R"(<div style="margin-top:12px; padding:10px; background-color:#2a2f4a;">)"
            << R"(<p style="font-size:18px; color:#ffffff;">)" << title << "</p>"
            << R"(<p style="font-size:13px; color:#9aa0c0;">)" << id << "</p>";
        if (!description.empty()) {
            rml << R"(<p style="font-size:14px; color:#c8ccdc; margin-top:6px;">)" << description << "</p>";
        }
        if (!tags.empty()) {
            rml << R"(<p style="font-size:12px; color:#7fd8a0; margin-top:6px;">)" << tags << "</p>";
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
