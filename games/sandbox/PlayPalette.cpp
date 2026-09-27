#include "PlayPalette.h"

#include "kke/Log.h"
#include "kke/RmlTextSafety.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/EventListener.h>

#include <string>
#include <vector>

namespace kke_sandbox {

namespace {

// The body lets clicks through to the world; only the row takes them.
const char* kDocument = R"RML(<rml>
<head>
<title>Play</title>
<style>
body { font-family: Noto Sans; font-size: 16dp; color: #1f2430; pointer-events: none; width: 100%; height: 100%; }
div { display: block; }
/* Centred without a transform: RmlUi's element offsets (contains(),
   cellCentres()) don't include transforms. */
#row { position: absolute; left: 0; right: 0; bottom: 12dp; text-align: center; }
#bar { display: inline-block; pointer-events: auto;
       padding: 10dp 12dp 8dp 12dp; background-color: #ffffffe6; border: 2dp #d5d9e3; border-radius: 22dp;
       white-space: nowrap; text-align: center; }
#hint { font-size: 19dp; font-weight: bold; color: #2b2f3a; margin: 0 4dp 8dp 4dp; }
.cell { display: inline-block; width: 92dp; margin: 0 4dp; vertical-align: top; }
.pic { width: 92dp; height: 92dp; border-radius: 16dp; background-color: #eef0f5; border: 3dp #e0e3ec;
       text-align: center; transition: background-color border-color 0.1s; }
.cell:hover .pic { background-color: #e2e7f5; border-color: #9ccbff; }
.cell:active .pic { background-color: #cfd9f2; }
.cell.on .pic { background-color: #fff1c7; border-color: #ffb629; }
.pic img { width: 86dp; height: 86dp; }
.word { display: block; line-height: 86dp; font-size: 22dp; font-weight: bold; color: #4a5268; }
.label { font-size: 16dp; font-weight: bold; margin-top: 3dp; color: #2b2f3a; }
</style>
</head>
<body><div id="row"><div id="bar"><div id="hint"></div><div id="cells"></div></div></div></body>
</rml>)RML";

std::string esc(const std::string& s) { return kke::escapeRmlText(s); }

// The picture (or one of its children) that was pressed.
Rml::Element* cellOf(Rml::Element* e) {
    for (; e; e = e->GetParentNode())
        if (e->HasAttribute("data-cell")) return e;
    return nullptr;
}

} // namespace

class PlayPaletteListener : public Rml::EventListener {
public:
    explicit PlayPaletteListener(PlayPalette& p) : m_palette(p) {}
    void ProcessEvent(Rml::Event& event) override {
        if (event.GetId() != Rml::EventId::Mousedown || event.GetParameter<int>("button", 0) != 0) return;
        Rml::Element* cell = cellOf(event.GetTargetElement());
        if (cell && m_palette.onPress) m_palette.onPress(cell->GetAttribute<Rml::String>("data-cell", ""));
    }

private:
    PlayPalette& m_palette;
};

PlayPalette::PlayPalette() = default;

PlayPalette::~PlayPalette() { detach(); }

void PlayPalette::attach(Rml::Context* context, float pixelsPerPoint) {
    m_context = context;
    m_ppp = pixelsPerPoint > 0.0f ? pixelsPerPoint : 1.0f;
    if (!m_context || m_doc) return;
    m_doc = m_context->LoadDocumentFromMemory(kDocument, "play-palette");
    if (!m_doc) {
        kke::log::get("Sandbox")->warn("the Play palette's document didn't load");
        return;
    }
    m_listener = std::make_unique<PlayPaletteListener>(*this);
    m_doc->AddEventListener(Rml::EventId::Mousedown, m_listener.get());
    m_shown.clear();
    if (m_visible) m_doc->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
    else m_doc->Hide();
}

void PlayPalette::detach() {
    if (m_doc) {
        m_doc->RemoveEventListener(Rml::EventId::Mousedown, m_listener.get());
        m_doc->Close();
        m_doc = nullptr;
    }
    m_listener.reset();
    m_context = nullptr;
}

void PlayPalette::setVisible(bool visible) {
    if (visible == m_visible) return;
    m_visible = visible;
    if (!m_doc) return;
    if (visible) m_doc->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
    else m_doc->Hide();
}

void PlayPalette::set(const std::string& hint, const std::vector<Cell>& cells) {
    if (!m_doc) return;
    std::string rml;
    for (const Cell& c : cells) {
        rml += "<div class=\"cell" + std::string(c.on ? " on" : "") + "\" data-cell=\"" + esc(c.id) + "\"><div class=\"pic\">";
        if (!c.image.empty()) rml += "<img src=\"" + esc(c.image) + "\"/>";
        else rml += "<span class=\"word\">" + esc(c.word.empty() ? c.label : c.word) + "</span>";
        rml += "</div><div class=\"label\">" + esc(c.label) + "</div></div>";
    }
    const std::string all = hint + '\n' + rml;
    if (all == m_shown) return;
    m_shown = all;
    m_doc->GetElementById("hint")->SetInnerRML(esc(hint));
    m_doc->GetElementById("cells")->SetInnerRML(rml);
    m_doc->UpdateDocument(); // lay it out now, so cellCentres() is right this frame
}

bool PlayPalette::contains(const glm::vec2& point) const {
    if (!m_doc || !m_visible) return false;
    Rml::Element* bar = m_doc->GetElementById("bar");
    const Rml::Vector2f o = bar->GetAbsoluteOffset(Rml::BoxArea::Border);
    const Rml::Vector2f s = bar->GetBox().GetSize(Rml::BoxArea::Border);
    const glm::vec2 px = point * m_ppp;
    return px.x >= o.x && px.y >= o.y && px.x <= o.x + s.x && px.y <= o.y + s.y;
}

std::vector<glm::vec2> PlayPalette::cellCentres() const {
    std::vector<glm::vec2> out;
    if (!m_doc || !m_visible) return out;
    Rml::Element* cells = m_doc->GetElementById("cells");
    for (int i = 0; i < cells->GetNumChildren(); ++i) {
        Rml::Element* pic = cells->GetChild(i)->GetFirstChild();
        if (!pic) continue;
        const Rml::Vector2f o = pic->GetAbsoluteOffset(Rml::BoxArea::Border);
        const Rml::Vector2f s = pic->GetBox().GetSize(Rml::BoxArea::Border);
        out.emplace_back((o.x + s.x * 0.5f) / m_ppp, (o.y + s.y * 0.5f) / m_ppp);
    }
    return out;
}

} // namespace kke_sandbox
