#pragma once

#include "kke/Module.h"
#include "kke/MarketplaceIndex.h"

#include <RmlUi/Core/EventListener.h>

#include <memory>
#include <string>

namespace Rml {
class ElementDocument;
}

namespace kke {

// Renders MarketplaceIndex's scanned game list as an actual RmlUi
// document — the first real proof that scanning game.json manifests and
// displaying them are connected, not just two pieces that both exist.
//
// Requires UiModule (declared via dependencies(), not just assumed) since
// it adds a document into UiModule's Rml::Context rather than owning one
// itself.
//
// HONEST LIMITATIONS, matching the rest of this integration's state:
//   - On a phone or console the list folds into a "Games" button so it
//     doesn't cover the game's own panel (kke/UiProfile.h).
//   - Non-interactive. RmlUi has no input wiring yet (see docs/HISTORY.md
//     Roadmap "RmlUi slice 4") — nothing can be clicked, so there is no
//     "launch this game" button, just a static list.
//   - Scans once, at init. No live refresh if the marketplace directory
//     changes while running.
//   - Every manifest field is passed through kke::escapeRmlText before
//     being placed in the generated markup — game.json content is
//     exactly the kind of "not this engine's own trusted code" input
//     that rule exists for (see docs/HISTORY.md's marketplace security note).
class MarketplaceUiModule : public Module {
public:
    explicit MarketplaceUiModule(std::string marketplaceDirectory);

    const char* name() const override { return "MarketplaceUi"; }
    std::vector<ModuleDependency> dependencies() const override;

    void init(Application& app) override;
    void shutdown() override;

private:
    std::string buildDocumentRml() const;

    // Opens and folds the list on a phone or console.
    class FoldListener : public Rml::EventListener {
    public:
        explicit FoldListener(MarketplaceUiModule& owner) : m_owner(owner) {}
        void ProcessEvent(Rml::Event& event) override;
    private:
        MarketplaceUiModule& m_owner;
    };
    std::unique_ptr<FoldListener> m_foldListener;

    std::string m_marketplaceDirectory;
    MarketplaceIndex m_index;
    Rml::ElementDocument* m_document = nullptr;
};

} // namespace kke
