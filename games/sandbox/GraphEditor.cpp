#include "GraphEditor.h"

#include "kke/Log.h"
#include "kke/PlayScript.h"
#include "kke/RmlTextSafety.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/ElementInstancer.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/EventListener.h>
#include <RmlUi/Core/Factory.h>
#include <RmlUi/Core/Geometry.h>
#include <RmlUi/Core/Mesh.h>
#include <RmlUi/Core/RenderManager.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace kke_sandbox {

namespace {

// Drawflow's look (github.com/jerosoler/drawflow, MIT), in RCSS: a light
// dotted canvas (the dots are drawn by <graphwires>), white rounded
// blocks with a coloured head, round ports on the edges, a blue outline
// on what's selected and a round x to remove it. Sizes in dp so it
// scales with the window (UiModule), and big enough for fingers.
const char* kDocument = R"RML(<rml>
<head>
<title>Graph</title>
<style>
body { font-family: Noto Sans; font-size: 15dp; color: #2b2f3a; pointer-events: none; width: 100%; height: 100%; }
div, p, h1 { display: block; }
#panel { position: absolute; left: 1%; top: 1%; width: 66%; height: 74%; pointer-events: auto;
         background-color: #ffffff; border: 2dp #d5d9e3; border-radius: 14dp; }
#top { height: 46dp; padding: 7dp 10dp; border-bottom: 1dp #e3e6ee; }
#title { display: inline-block; font-size: 18dp; font-weight: bold; color: #1f2430; margin-right: 12dp; vertical-align: middle; }
#status { display: inline-block; padding: 4dp 10dp; border-radius: 12dp; font-size: 13dp; vertical-align: middle; }
#status.ok { background-color: #dff5e5; color: #227a3d; }
#status.bad { background-color: #fde2e0; color: #b3261e; }
#status.empty { background-color: #eef0f5; color: #6b7285; }
#tools { position: absolute; right: 10dp; top: 7dp; }
button { display: inline-block; padding: 6dp 14dp; margin-left: 6dp; border-radius: 10dp; background-color: #eef0f5;
         color: #2b2f3a; font-size: 14dp; border: 1dp #d5d9e3; text-align: center; }
button:hover { background-color: #e2e7f5; border-color: #4ea9ff; }
button:active { background-color: #cfd9f2; }
button.primary { background-color: #4ea9ff; color: #ffffff; border-color: #3b8fe0; }
button.primary:hover { background-color: #3b98f0; }
button.danger { background-color: #ffe4e1; color: #b3261e; border-color: #f3b8b1; }
button.off { opacity: 0.4; }
#luabutton { width: 64dp; }
#main { position: absolute; left: 0; right: 0; top: 61dp; bottom: 0; }
#why { position: absolute; left: 0; right: 0; top: 61dp; height: 0; }
#blocks { position: absolute; left: 0; top: 0; bottom: 0; width: 200dp; overflow-y: auto; padding: 8dp;
          background-color: #f7f8fb; border-right: 1dp #e3e6ee; border-bottom-left-radius: 14dp; }
#blocks h1 { font-size: 12dp; color: #8a90a2; margin: 10dp 4dp 4dp 4dp; font-weight: bold; }
.add { display: block; margin: 3dp 0dp; padding: 7dp 10dp; border-radius: 9dp; background-color: #ffffff;
       border: 1dp #e0e3ec; border-left-width: 5dp; font-size: 14dp; }
.add:hover { border-color: #4ea9ff; background-color: #f2f7ff; }
.k-event { border-left-color: #35b464; }
.k-action { border-left-color: #4e8cff; }
.k-flow { border-left-color: #ff9f40; }
.k-value { border-left-color: #a6abba; }
#canvas { position: absolute; left: 201dp; right: 0; top: 0; bottom: 0; overflow: hidden; background-color: #f4f6fa;
          border-bottom-right-radius: 14dp; }
#canvas.withlua { right: 38%; }
/* Big, so blocks size to their contents instead of squeezing into it. */
#world { position: absolute; left: 0; top: 0; width: 20000dp; height: 20000dp; transform-origin: left top; }
/* Dots and wires under the blocks, also after the blocks are rebuilt. */
graphwires { display: block; position: absolute; left: 0; top: 0; width: 1dp; height: 1dp; z-index: 0; }
#nodes { position: absolute; left: 0; top: 0; width: 100%; height: 100%; z-index: 1; }
.node { position: absolute; min-width: 190dp; background-color: #ffffff; border: 2dp #cfd4df; border-bottom-width: 4dp;
        border-radius: 12dp; }
.node.selected { border-color: #4ea9ff; }
.node.broken { border-color: #e5484d; }
.node.lit { border-color: #ffc93d; }
.head { position: relative; padding: 8dp 30dp 8dp 30dp; border-top-left-radius: 10dp; border-top-right-radius: 10dp; color: #ffffff;
        font-weight: bold; font-size: 15dp; }
.node.k-event .head { background-color: #35b464; }
.node.k-action .head { background-color: #4e8cff; }
.node.k-flow .head { background-color: #ff9f40; }
.node.k-value .head { background-color: #8d93a5; }
.node.unknown .head { background-color: #e5484d; }
.msg { padding: 4dp 12dp; font-size: 12dp; max-width: 240dp; }
.msg.error { color: #b3261e; background-color: #fff0ee; }
.msg.hint { color: #8a6100; background-color: #fff7df; }
.row { position: relative; height: 34dp; padding: 5dp 18dp; }
.row.out { text-align: right; }
.label { display: inline-block; font-size: 13dp; color: #5a6072; margin-right: 6dp; vertical-align: middle; }
.port { position: absolute; top: 8dp; width: 18dp; height: 18dp; border-radius: 9dp; border: 3dp #9aa1b2; background-color: #ffffff; }
.port:hover { background-color: #d6e9ff; }
.row.in .port { left: -11dp; }
.row.out .port { right: -11dp; }
.port.flowport { border-radius: 4dp; }
.port.linked { background-color: #9aa1b2; }
.head .port { top: 7dp; }
.head .port.in { left: -11dp; }
.head .port.out { right: -11dp; }
.t-flow { border-color: #6b7285; }
.t-flow.linked { background-color: #6b7285; }
.t-thing { border-color: #ff8a57; }
.t-thing.linked { background-color: #ff8a57; }
.t-vec { border-color: #e8b400; }
.t-vec.linked { background-color: #e8b400; }
.t-number { border-color: #35b464; }
.t-number.linked { background-color: #35b464; }
.t-bool { border-color: #e5484d; }
.t-bool.linked { background-color: #e5484d; }
.t-text, .t-block, .t-sound { border-color: #b05cff; }
.t-text.linked, .t-block.linked, .t-sound.linked { background-color: #b05cff; }
.row button { padding: 3dp 9dp; margin-left: 3dp; font-size: 13dp; border-radius: 8dp; }
.row button.choice { min-width: 70dp; background-color: #f5edff; border-color: #dcc6ff; }
input.text { display: inline-block; width: 64dp; padding: 3dp 6dp; font-size: 13dp; background-color: #f7f8fb;
             border: 1dp #d5d9e3; border-radius: 7dp; vertical-align: middle; }
input.text.wide { width: 120dp; }
input.text:focus { border-color: #4ea9ff; background-color: #ffffff; }
.me { font-size: 12dp; color: #8a90a2; }
.x { position: absolute; width: 26dp; height: 26dp; border-radius: 13dp; background-color: #1f2430; color: #ffffff;
     text-align: center; font-size: 16dp; line-height: 24dp; font-weight: bold; }
.x:hover { background-color: #e5484d; }
.node .x { right: -13dp; top: -13dp; }
#fits { position: absolute; width: 230dp; max-height: 300dp; overflow-y: auto; padding: 8dp; background-color: #ffffff;
        border: 2dp #4ea9ff; border-radius: 12dp; }
#fits h1 { font-size: 12dp; color: #8a90a2; margin: 0dp 4dp 6dp 4dp; }
#lua { position: absolute; top: 0; bottom: 0; right: 0; width: 38%; overflow: auto; padding: 8dp; background-color: #1f2430;
       color: #d8dcea; font-size: 13dp; border-bottom-right-radius: 14dp; }
#lua p { white-space: pre; }
#lua .n { color: #6b7285; }
#lua .bad { color: #ff8a80; }
#lua h1 { font-size: 12dp; color: #8a90a2; margin-bottom: 6dp; }
scrollbarvertical { width: 8dp; }
scrollbarvertical slidertrack { background-color: #00000000; }
scrollbarvertical sliderbar { background-color: #cfd4df; border-radius: 4dp; min-height: 24dp; }
scrollbarvertical sliderbar:hover { background-color: #aab1c2; }
scrollbarvertical sliderarrowdec, scrollbarvertical sliderarrowinc { height: 0; }
scrollbarhorizontal { height: 8dp; }
scrollbarhorizontal slidertrack { background-color: #00000000; }
scrollbarhorizontal sliderbar { background-color: #4a5063; border-radius: 4dp; min-width: 24dp; }
scrollbarhorizontal sliderarrowdec, scrollbarhorizontal sliderarrowinc { width: 0; }
#problem { position: absolute; left: 212dp; top: 70dp; max-width: 60%; padding: 6dp 12dp; border-radius: 10dp;
           background-color: #fff0ee; color: #b3261e; font-size: 13dp; }
</style>
</head>
<body>
<div id="panel">
  <div id="top">
    <div id="title"></div><div id="status"></div>
    <div id="tools">
      <button data-act="zoomout">-</button><button data-act="zoomin">+</button><button data-act="fit">Fit</button>
      <button data-act="lua" id="luabutton">Show Lua</button>
      <button data-act="remove" id="removebutton" class="danger">Remove</button>
      <button data-act="done" class="primary">Done</button>
    </div>
  </div>
  <div id="main">
    <div id="blocks"></div>
    <div id="canvas"><div id="world"><graphwires id="wires"></graphwires><div id="nodes"></div></div><div id="overlay"></div></div>
    <div id="lua"></div>
  </div>
  <div id="problem"></div>
</div>
</body>
</rml>)RML";

// Kinds and types as the class names the RCSS colours.
const char* kindClass(kke::NodeDef::Kind k) {
    switch (k) {
    case kke::NodeDef::Kind::Event: return "k-event";
    case kke::NodeDef::Kind::Action: return "k-action";
    case kke::NodeDef::Kind::Flow: return "k-flow";
    case kke::NodeDef::Kind::Value: return "k-value";
    }
    return "k-value";
}

glm::vec4 typeColor(const std::string& t) {
    using namespace kke::api_type;
    if (t == "flow") return { 0.42f, 0.45f, 0.52f, 1.0f };
    if (t == Thing) return { 1.0f, 0.54f, 0.34f, 1.0f };
    if (t == Vec) return { 0.91f, 0.71f, 0.0f, 1.0f };
    if (t == Number) return { 0.21f, 0.71f, 0.39f, 1.0f };
    if (t == Bool) return { 0.9f, 0.28f, 0.3f, 1.0f };
    if (t == Text || t == Block || t == Sound) return { 0.69f, 0.36f, 1.0f, 1.0f };
    return { 0.6f, 0.63f, 0.7f, 1.0f };
}

std::string typeClass(const std::string& t) {
    if (t == "any") return "t-any";
    return "t-" + t;
}

std::string esc(const std::string& s) { return kke::escapeRmlText(s); }

std::string fmt(float v) {
    char b[32];
    std::snprintf(b, sizeof(b), "%.2f", double(v));
    return b;
}

// The shown value of an unwired input: what was typed, else the default
// written the way a person would ("wood", not "\"wood\"").
std::string shownValue(const kke::GraphNode& n, const kke::PinDef& p) {
    auto it = n.values.find(p.name);
    if (it != n.values.end() && !it->second.empty()) return it->second;
    std::string v = p.fallback;
    if (v == "nil") return {};
    if (v.size() >= 2 && v.front() == '"' && v.back() == '"') return v.substr(1, v.size() - 2);
    if (v.rfind("Vec(", 0) == 0 && v.size() > 5) {
        v = v.substr(4, v.size() - 5);
        std::string out;
        for (char c : v)
            if (c != ',') out += c;
        return out;
    }
    return v;
}

glm::vec2 bezier(const glm::vec2& a, const glm::vec2& c1, const glm::vec2& c2, const glm::vec2& b, float t) {
    const float u = 1.0f - t;
    return u * u * u * a + 3.0f * u * u * t * c1 + 3.0f * u * t * t * c2 + t * t * t * b;
}

// Drawflow's curve: out to the right, in from the left, bending half the
// horizontal distance (at least a little, so short wires still curve).
void curve(const glm::vec2& a, const glm::vec2& b, glm::vec2& c1, glm::vec2& c2) {
    const float bend = std::max(40.0f, std::abs(b.x - a.x) * 0.5f);
    c1 = a + glm::vec2(bend, 0.0f);
    c2 = b - glm::vec2(bend, 0.0f);
}

float distanceToSegment(const glm::vec2& p, const glm::vec2& a, const glm::vec2& b) {
    const glm::vec2 ab = b - a;
    const float len2 = glm::dot(ab, ab);
    const float t = len2 > 1e-6f ? std::clamp(glm::dot(p - a, ab) / len2, 0.0f, 1.0f) : 0.0f;
    return glm::length(p - (a + ab * t));
}

Rml::Element* withAttribute(Rml::Element* e, const char* name) {
    for (; e; e = e->GetParentNode())
        if (e->GetAttribute(name)) return e;
    return nullptr;
}

std::string attr(Rml::Element* e, const char* name) { return e ? e->GetAttribute<Rml::String>(name, "") : std::string(); }

} // namespace

// ---------------------------------------------------------------- the wires

// <graphwires>: the dots of the canvas and the curved wires, drawn as
// geometry inside the zoomed and panned world (so they move and scale
// with the blocks). RmlUi has no curves of its own.
class GraphWires : public Rml::Element {
public:
    explicit GraphWires(const Rml::String& tag) : Rml::Element(tag) {}
    GraphEditor* editor = nullptr;
    glm::vec2 gridMin{0.0f}, gridMax{0.0f}; // local pixels to cover with dots
    float gridStep = 24.0f;

protected:
    void OnRender() override {
        if (!editor || !editor->isOpen()) return;
        Rml::Mesh mesh;
        auto colour = [](const glm::vec4& c) {
            return Rml::Colourb(Rml::byte(c.r * 255.0f), Rml::byte(c.g * 255.0f), Rml::byte(c.b * 255.0f), Rml::byte(c.a * 255.0f))
                .ToPremultiplied();
        };
        auto quad = [&](glm::vec2 a, glm::vec2 b, glm::vec2 c, glm::vec2 d, Rml::ColourbPremultiplied col) {
            const int base = int(mesh.vertices.size());
            for (const glm::vec2& p : { a, b, c, d }) mesh.vertices.push_back(Rml::Vertex{ Rml::Vector2f(p.x, p.y), col, Rml::Vector2f(0, 0) });
            mesh.indices.insert(mesh.indices.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
        };
        // Dots, Drawflow's background.
        if (gridStep > 1.0f && gridMax.x > gridMin.x) {
            const Rml::ColourbPremultiplied dot = colour({ 0.72f, 0.75f, 0.82f, 1.0f });
            const float r = std::max(1.0f, gridStep * 0.06f);
            const float x0 = std::floor(gridMin.x / gridStep) * gridStep, y0 = std::floor(gridMin.y / gridStep) * gridStep;
            int count = 0;
            for (float y = y0; y <= gridMax.y && count < 20000; y += gridStep)
                for (float x = x0; x <= gridMax.x && count < 20000; x += gridStep, ++count)
                    quad({ x - r, y - r }, { x + r, y - r }, { x + r, y + r }, { x - r, y + r }, dot);
        }
        // Wires: a strip along the curve.
        for (const GraphEditor::Wire& w : editor->wires()) {
            glm::vec2 c1, c2;
            curve(w.a, w.b, c1, c2);
            const Rml::ColourbPremultiplied col = colour(w.color);
            constexpr int kSteps = 32;
            glm::vec2 prev = w.a;
            for (int i = 1; i <= kSteps; ++i) {
                const glm::vec2 p = bezier(w.a, c1, c2, w.b, float(i) / kSteps);
                glm::vec2 d = p - prev;
                const float len = glm::length(d);
                if (len < 1e-4f) continue;
                const glm::vec2 n = glm::vec2(-d.y, d.x) / len * (w.width * 0.5f);
                // Overlap each piece a little so the joins don't crack.
                const glm::vec2 ext = d / len * (w.width * 0.25f);
                quad(prev - ext + n, p + ext + n, p + ext - n, prev - ext - n, col);
                prev = p;
            }
        }
        if (mesh.indices.empty()) return;
        Rml::RenderManager* rm = GetRenderManager();
        if (!rm) return;
        Rml::Geometry geometry = rm->MakeGeometry(std::move(mesh));
        geometry.Render(GetAbsoluteOffset(Rml::BoxArea::Border));
    }
};

namespace {

Rml::ElementInstancerGeneric<GraphWires>& wiresInstancer() {
    static Rml::ElementInstancerGeneric<GraphWires> instancer;
    return instancer;
}

} // namespace

// ---------------------------------------------------------------- events

class GraphEditorListener : public Rml::EventListener {
public:
    explicit GraphEditorListener(GraphEditor& e) : m_editor(e) {}
    void ProcessEvent(Rml::Event& event) override {
        Rml::Element* target = event.GetTargetElement();
        const glm::vec2 px(event.GetParameter<float>("mouse_x", 0.0f), event.GetParameter<float>("mouse_y", 0.0f));
        switch (event.GetId()) {
        case Rml::EventId::Mousedown:
            if (event.GetParameter<int>("button", 0) == 0) m_editor.onMouseDown(target, px);
            break;
        case Rml::EventId::Click:
            m_editor.onClick(target);
            break;
        case Rml::EventId::Change:
            // Text fields: applied on Enter (and on leaving the field, below),
            // not on every letter, so the game isn't restarted mid-word.
            if (event.GetParameter<bool>("linebreak", false))
                if (auto* f = dynamic_cast<Rml::ElementFormControl*>(target)) m_editor.onValue(target, f->GetValue());
            break;
        case Rml::EventId::Blur:
            if (auto* f = dynamic_cast<Rml::ElementFormControl*>(target)) m_editor.onValue(target, f->GetValue());
            break;
        case Rml::EventId::Mousescroll:
            m_editor.onScroll(event.GetParameter<float>("wheel_delta_y", 0.0f), m_editor.isOpen() ? px : glm::vec2(0.0f));
            event.StopPropagation();
            break;
        default:
            break;
        }
    }

private:
    GraphEditor& m_editor;
};

// ---------------------------------------------------------------- editor

GraphEditor::GraphEditor() = default;

GraphEditor::~GraphEditor() { detach(); }

void GraphEditor::detach() {
    close();
    if (m_doc) {
        m_doc->RemoveEventListener(Rml::EventId::Blur, m_listener.get(), true);
        for (Rml::EventId id : { Rml::EventId::Mousedown, Rml::EventId::Click, Rml::EventId::Change, Rml::EventId::Mousescroll })
            m_doc->RemoveEventListener(id, m_listener.get());
        m_doc->Close();
        m_doc = nullptr;
    }
    m_listener.reset();
    m_context = nullptr;
}

void GraphEditor::attach(Rml::Context* context, float pixelsPerPoint) {
    m_context = context;
    m_ppp = pixelsPerPoint > 0.0f ? pixelsPerPoint : 1.0f;
    if (!m_context || m_doc) return;
    static bool registered = false;
    if (!registered) {
        Rml::Factory::RegisterElementInstancer("graphwires", &wiresInstancer());
        registered = true;
    }
    m_doc = m_context->LoadDocumentFromMemory(kDocument, "graph-editor");
    if (!m_doc) {
        kke::log::get("Sandbox")->warn("the node graph editor's document didn't load");
        return;
    }
    m_listener = std::make_unique<GraphEditorListener>(*this);
    for (Rml::EventId id : { Rml::EventId::Mousedown, Rml::EventId::Click, Rml::EventId::Change, Rml::EventId::Mousescroll })
        m_doc->AddEventListener(id, m_listener.get());
    m_doc->AddEventListener(Rml::EventId::Blur, m_listener.get(), true); // blur doesn't bubble: capture it
    if (auto* w = dynamic_cast<GraphWires*>(m_doc->GetElementById("wires"))) w->editor = this;
    m_doc->Hide();
}

void GraphEditor::open(kke::NodeGraph* graph, const kke::NodeLibrary* library, std::string title) {
    if (!m_doc) return;
    m_graph = graph;
    m_library = library;
    m_title = std::move(title);
    m_issues.clear();
    m_lua.clear();
    m_lineNode.clear();
    m_errorNode = 0;
    m_runtimeError.clear();
    m_lit.clear();
    m_drag = Drag::None;
    m_selectedNode = 0;
    m_selectedLink = -1;
    m_fitsOpen = false;
    m_fitPending = true;
    // The list of blocks, by section.
    std::string rml;
    for (const std::string& cat : m_library->categories()) {
        rml += "<h1>" + esc(cat) + "</h1>";
        for (const kke::NodeDef& d : m_library->nodes()) {
            if (d.category != cat) continue;
            rml += "<div class=\"add " + std::string(kindClass(d.kind)) + "\" data-act=\"add\" data-type=\"" + esc(d.type) + "\" title=\"" +
                   esc(d.doc) + "\">" + esc(d.title) + "</div>";
        }
    }
    m_doc->GetElementById("blocks")->SetInnerRML(rml);
    m_doc->GetElementById("title")->SetInnerRML("Inside: " + esc(m_title));
    m_needRebuild = true;
    m_doc->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
}

void GraphEditor::close() {
    m_graph = nullptr;
    m_library = nullptr;
    m_drag = Drag::None;
    if (m_doc) m_doc->Hide();
}

void GraphEditor::setCompiled(const kke::CompiledGraph& compiled, const std::string& shownLua) {
    m_issues = compiled.issues;
    m_lineNode = compiled.lineNode;
    m_lua = shownLua;
    m_errorNode = 0;
    m_runtimeError.clear();
    m_needRebuild = true;
}

void GraphEditor::setRuntimeError(int node, const std::string& message) {
    m_errorNode = node;
    m_runtimeError = message;
    m_needRebuild = true;
}

void GraphEditor::ran(int node) {
    for (Lit& l : m_lit)
        if (l.node == node) {
            l.left = 0.6f;
            return;
        }
    if (m_lit.size() < 64) m_lit.push_back({ node, 0.6f });
}

bool GraphEditor::contains(const glm::vec2& p) const {
    if (!isOpen() || !m_doc) return false;
    Rml::Element* panel = m_doc->GetElementById("panel");
    const Rml::Vector2f o = panel->GetAbsoluteOffset(Rml::BoxArea::Border);
    const Rml::Vector2f s = panel->GetBox().GetSize(Rml::BoxArea::Border);
    const glm::vec2 px = p * m_ppp;
    return px.x >= o.x && px.y >= o.y && px.x <= o.x + s.x && px.y <= o.y + s.y;
}

glm::vec2 GraphEditor::canvasOrigin() const {
    const Rml::Vector2f o = m_doc->GetElementById("canvas")->GetAbsoluteOffset(Rml::BoxArea::Padding);
    return { o.x, o.y };
}

glm::vec2 GraphEditor::toWorld(const glm::vec2& px) const {
    const float dp = m_context ? m_context->GetDensityIndependentPixelRatio() : 1.0f;
    return (px - canvasOrigin() - m_pan) / (m_zoom * dp);
}

void GraphEditor::pan(const glm::vec2& d) {
    if (!isOpen()) return;
    m_pan += d * m_ppp;
    applyTransform();
}

void GraphEditor::zoom(float steps, const glm::vec2& aroundPoints) {
    if (!isOpen() || steps == 0.0f) return;
    const glm::vec2 px = aroundPoints * m_ppp;
    const glm::vec2 before = toWorld(px);
    m_zoom = std::clamp(m_zoom * std::pow(1.15f, steps), 0.3f, 2.5f);
    // Keep the point under the finger (or cursor) where it is.
    const float dp = m_context->GetDensityIndependentPixelRatio();
    m_pan = px - canvasOrigin() - before * (m_zoom * dp);
    applyTransform();
}

void GraphEditor::removeSelected() {
    if (!isOpen()) return;
    if (m_selectedNode) {
        m_graph->remove(m_selectedNode);
        m_selectedNode = 0;
        m_changed = m_needRebuild = true;
    } else if (m_selectedLink >= 0 && size_t(m_selectedLink) < m_graph->links.size()) {
        m_graph->links.erase(m_graph->links.begin() + m_selectedLink);
        m_selectedLink = -1;
        m_changed = m_needRebuild = true;
    }
}

void GraphEditor::showAll() { m_fitPending = true; }

void GraphEditor::applyTransform() {
    if (!m_doc) return;
    char t[128];
    std::snprintf(t, sizeof(t), "translate(%.1fpx, %.1fpx) scale(%.3f)", double(m_pan.x), double(m_pan.y), double(m_zoom));
    m_doc->GetElementById("world")->SetProperty("transform", t);
}

int GraphEditor::nodeFromElement(Rml::Element* e) const {
    Rml::Element* n = withAttribute(e, "data-node");
    return n ? std::atoi(attr(n, "data-node").c_str()) : 0;
}

bool GraphEditor::pinFromElement(Rml::Element* e, PinRef& out) const {
    Rml::Element* p = withAttribute(e, "data-port");
    if (!p) return false;
    out.node = nodeFromElement(p);
    out.pin = attr(p, "data-port");
    out.output = attr(p, "data-side") == "out";
    return out.node != 0;
}

int GraphEditor::addNode(const std::string& type, glm::vec2 at) {
    const int id = m_graph->add(type, glm::round(at));
    m_selectedNode = id;
    m_selectedLink = -1;
    m_changed = m_needRebuild = true;
    return id;
}

void GraphEditor::setValue(int node, const std::string& pin, const std::string& value) {
    kke::GraphNode* n = m_graph ? m_graph->find(node) : nullptr;
    if (!n) return;
    if (n->values[pin] == value) return;
    n->values[pin] = value;
    m_changed = m_needRebuild = true;
}

void GraphEditor::onMouseDown(Rml::Element* target, const glm::vec2& px) {
    if (!isOpen()) return;
    m_mousePx = px;
    // Buttons, fields and the block list do their own thing (click).
    if (withAttribute(target, "data-act") || dynamic_cast<Rml::ElementFormControl*>(target)) return;
    if (Rml::Element* fits = m_doc->GetElementById("fits"); fits && (target == fits || target->GetParentNode() == fits)) return;
    m_fitsOpen = false;
    m_dragStartPx = m_dragLastPx = px;
    m_dragMoved = false;
    PinRef pin;
    if (pinFromElement(target, pin)) {
        // From a wired input: pick its wire up (drag it somewhere else).
        if (!pin.output) {
            if (const kke::GraphLink* l = m_graph->linkInto(pin.node, pin.pin)) {
                const kke::GraphLink old = *l;
                m_graph->disconnect(old);
                m_changed = m_needRebuild = true;
                pin = PinRef{ old.fromNode, old.fromPin, true };
            }
        }
        m_drag = Drag::Wire;
        m_dragPin = pin;
        return;
    }
    if (const int node = nodeFromElement(target)) {
        m_drag = Drag::Node;
        m_dragNode = node;
        m_nodeStart = m_graph->find(node) ? m_graph->find(node)->pos : glm::vec2(0.0f);
        if (m_selectedNode != node || m_selectedLink >= 0) {
            m_selectedNode = node;
            m_selectedLink = -1;
            m_needRebuild = true;
        }
        return;
    }
    // The canvas: a wire under the pointer is selected, else drag the view.
    Rml::Element* canvas = m_doc->GetElementById("canvas");
    bool onCanvas = false;
    for (Rml::Element* e = target; e; e = e->GetParentNode()) onCanvas = onCanvas || e == canvas;
    if (!onCanvas) return;
    const int link = hitWire(toWorld(px));
    if (link != m_selectedLink || m_selectedNode) {
        m_selectedLink = link;
        m_selectedNode = 0;
        m_needRebuild = true;
    }
    m_drag = link >= 0 ? Drag::None : Drag::Canvas;
}

void GraphEditor::onClick(Rml::Element* target) {
    if (!isOpen()) return;
    Rml::Element* e = withAttribute(target, "data-act");
    if (!e) return;
    const std::string act = attr(e, "data-act");
    const int node = nodeFromElement(e);
    const std::string pin = attr(e, "data-pin");
    if (act == "done") m_closeAsked = true;
    else if (act == "lua") {
        m_showLua = !m_showLua;
        m_needRebuild = true;
    } else if (act == "fit") m_fitPending = true;
    else if (act == "zoomin" || act == "zoomout") {
        const Rml::Vector2f s = m_doc->GetElementById("canvas")->GetBox().GetSize(Rml::BoxArea::Padding);
        zoom(act == "zoomin" ? 1.0f : -1.0f, (canvasOrigin() + glm::vec2(s.x, s.y) * 0.5f) / m_ppp);
    } else if (act == "remove" || act == "delnode" || act == "dellink") removeSelected();
    else if (act == "add") {
        // In the middle of the view, nudged so repeated taps don't stack.
        const Rml::Vector2f s = m_doc->GetElementById("canvas")->GetBox().GetSize(Rml::BoxArea::Padding);
        const glm::vec2 mid = toWorld(canvasOrigin() + glm::vec2(s.x * 0.4f, s.y * 0.35f));
        const float jitter = float(m_graph->nextId % 5) * 22.0f;
        addNode(attr(e, "data-type"), mid + glm::vec2(jitter));
    } else if (act == "fit-add") {
        // A block from the "what fits" menu, wired to the dropped wire.
        const std::string type = attr(e, "data-type");
        const std::string other = attr(e, "data-pin");
        const glm::vec2 at = m_fitsFrom.output ? m_fitsAt : m_fitsAt - glm::vec2(240.0f, 0.0f);
        const int id = addNode(type, at);
        m_graph->connect(*m_library, m_fitsFrom.output ? kke::GraphLink{ m_fitsFrom.node, m_fitsFrom.pin, id, other }
                                                      : kke::GraphLink{ id, other, m_fitsFrom.node, m_fitsFrom.pin });
        m_fitsOpen = false;
    } else if (act == "fit-cancel") {
        m_fitsOpen = false;
        m_needRebuild = true;
    } else if (act == "dec" || act == "inc") {
        const kke::GraphNode* n = m_graph->find(node);
        const kke::NodeDef* d = n ? m_library->find(n->type) : nullptr;
        const kke::PinDef* p = d ? d->input(pin) : nullptr;
        if (!p) return;
        std::string lua;
        double v = 0.0;
        if (kke::valueToLua(kke::api_type::Number, shownValue(*n, *p), lua)) v = std::strtod(lua.c_str(), nullptr);
        v += act == "inc" ? 1.0 : -1.0;
        char b[32];
        std::snprintf(b, sizeof(b), "%g", v);
        setValue(node, pin, b);
    } else if (act == "cycle") {
        const kke::GraphNode* n = m_graph->find(node);
        const kke::NodeDef* d = n ? m_library->find(n->type) : nullptr;
        const kke::PinDef* p = d ? d->input(pin) : nullptr;
        if (!p) return;
        const std::vector<std::string>& choices = p->type == kke::api_type::Sound ? kke::playSoundNames() : m_blocks;
        if (choices.empty()) return;
        const std::string cur = shownValue(*n, *p);
        auto at = std::find(choices.begin(), choices.end(), cur);
        setValue(node, pin, at == choices.end() || at + 1 == choices.end() ? choices.front() : *(at + 1));
    } else if (act == "toggle") {
        const kke::GraphNode* n = m_graph->find(node);
        const kke::NodeDef* d = n ? m_library->find(n->type) : nullptr;
        const kke::PinDef* p = d ? d->input(pin) : nullptr;
        if (!p) return;
        const std::string cur = shownValue(*n, *p);
        setValue(node, pin, cur == "false" || cur == "no" ? "yes" : "no");
    }
}

void GraphEditor::onValue(Rml::Element* target, const std::string& value) {
    if (!isOpen()) return;
    const int node = nodeFromElement(target);
    const std::string pin = attr(target, "data-pin");
    if (node && !pin.empty()) setValue(node, pin, value);
}

void GraphEditor::onScroll(float delta, const glm::vec2& px) {
    if (!isOpen() || delta == 0.0f) return;
    // Only on the canvas: the block list and the Lua scroll as usual.
    Rml::Element* hover = m_context->GetHoverElement();
    Rml::Element* canvas = m_doc->GetElementById("canvas");
    bool onCanvas = false;
    for (Rml::Element* e = hover; e; e = e->GetParentNode()) onCanvas = onCanvas || e == canvas;
    if (onCanvas) zoom(-delta, px / m_ppp);
}

int GraphEditor::hitWire(const glm::vec2& world) const {
    const float dp = m_context->GetDensityIndependentPixelRatio();
    const glm::vec2 local = world * dp;
    const float reach = 10.0f * dp;
    int best = -1;
    float bestDist = reach;
    for (size_t i = 0; i < m_wires.size() && i < m_graph->links.size(); ++i) {
        const Wire& w = m_wires[i];
        glm::vec2 c1, c2;
        curve(w.a, w.b, c1, c2);
        glm::vec2 prev = w.a;
        for (int s = 1; s <= 24; ++s) {
            const glm::vec2 p = bezier(w.a, c1, c2, w.b, float(s) / 24.0f);
            const float d = distanceToSegment(local, prev, p);
            if (d < bestDist) {
                bestDist = d;
                best = int(i);
            }
            prev = p;
        }
    }
    return best;
}

void GraphEditor::openFitsMenu(const PinRef& from, const glm::vec2& world) {
    const kke::GraphNode* n = m_graph->find(from.node);
    const kke::NodeDef* d = n ? m_library->find(n->type) : nullptr;
    const kke::PinDef* fp = d ? (from.output ? d->output(from.pin) : d->input(from.pin)) : nullptr;
    if (!fp) return;
    std::string rml = "<h1>Add a block here</h1>";
    int count = 0;
    for (const kke::NodeDef& c : m_library->nodes()) {
        const std::vector<kke::PinDef>& pins = from.output ? c.inputs : c.outputs;
        const kke::PinDef* match = nullptr;
        for (const kke::PinDef& p : pins) {
            const bool fits = from.output ? kke::NodeLibrary::compatible(fp->type, p.type) : kke::NodeLibrary::compatible(p.type, fp->type);
            // "any" fits everything: prefer a port of the very same type.
            if (fits && (!match || (p.type == fp->type && match->type != fp->type))) match = &p;
        }
        if (!match) continue;
        rml += "<div class=\"add " + std::string(kindClass(c.kind)) + "\" data-act=\"fit-add\" data-type=\"" + esc(c.type) + "\" data-pin=\"" +
               esc(match->name) + "\">" + esc(c.title) + "</div>";
        ++count;
    }
    if (!count) return;
    rml += "<div class=\"add\" data-act=\"fit-cancel\">Never mind</div>";
    m_fitsOpen = true;
    m_fitsFrom = from;
    m_fitsAt = world;
    // Shown at the drop point, in canvas pixels (outside the zoom).
    const float dp = m_context->GetDensityIndependentPixelRatio();
    const glm::vec2 at = world * (m_zoom * dp) + m_pan;
    char style[96];
    std::snprintf(style, sizeof(style), "left: %.0fpx; top: %.0fpx;", double(at.x), double(at.y));
    m_doc->GetElementById("overlay")->SetInnerRML("<div id=\"fits\" style=\"" + std::string(style) + "\">" + rml + "</div>");
}

void GraphEditor::rebuild() {
    m_needRebuild = false;
    if (!m_graph) return;
    std::ostringstream rml;
    for (const kke::GraphNode& n : m_graph->nodes) {
        const kke::NodeDef* d = m_library->find(n.type);
        const kke::GraphIssue* issue = nullptr;
        for (const kke::GraphIssue& i : m_issues)
            if (i.node == n.id && (!issue || (i.error && !issue->error))) issue = &i;
        const bool broken = n.id == m_errorNode || (issue && issue->error);
        bool lit = false;
        for (const Lit& l : m_lit) lit = lit || l.node == n.id;
        const std::string id = std::to_string(n.id);
        rml << "<div class=\"node " << (d ? kindClass(d->kind) : "unknown") << (n.id == m_selectedNode ? " selected" : "")
            << (broken ? " broken" : "") << (lit ? " lit" : "") << "\" id=\"n" << id << "\" data-node=\"" << id << "\" style=\"left: "
            << fmt(n.pos.x) << "dp; top: " << fmt(n.pos.y) << "dp;\">";
        // The head: the title, with the flow ports on its edges.
        rml << "<div class=\"head\">";
        if (d)
            for (const kke::PinDef& p : d->inputs)
                if (p.flow() && p.name == ">")
                    rml << "<div class=\"port in flowport t-flow" << (m_graph->linkInto(n.id, p.name) ? " linked" : "")
                        << "\" data-port=\">\" data-side=\"in\"></div>";
        rml << esc(d ? d->title : n.type);
        if (d)
            for (const kke::PinDef& p : d->outputs)
                if (p.flow() && p.name == ">") {
                    bool linked = false;
                    for (const kke::GraphLink& l : m_graph->links) linked = linked || (l.fromNode == n.id && l.fromPin == ">");
                    rml << "<div class=\"port out flowport t-flow" << (linked ? " linked" : "") << "\" data-port=\">\" data-side=\"out\"></div>";
                }
        rml << "</div>";
        if (n.id == m_errorNode) rml << "<div class=\"msg error\">" << esc(m_runtimeError.substr(0, 160)) << "</div>";
        else if (issue) rml << "<div class=\"msg " << (issue->error ? "error" : "hint") << "\">" << esc(issue->message) << "</div>";
        if (!d) {
            rml << "<div class=\"msg error\">Not in this version of the engine</div>";
        } else {
            for (const kke::PinDef& p : d->inputs) {
                if (p.flow() && p.name == ">") continue;
                const bool linked = m_graph->linkInto(n.id, p.name) != nullptr;
                rml << "<div class=\"row in\"><div class=\"port in " << typeClass(p.type) << (p.flow() ? " flowport" : "")
                    << (linked ? " linked" : "") << "\" data-port=\"" << esc(p.name) << "\" data-side=\"in\"></div>";
                rml << "<span class=\"label\">" << esc(p.label) << "</span>";
                if (!linked && !p.flow()) {
                    const std::string v = esc(shownValue(n, p));
                    const std::string at = " data-pin=\"" + esc(p.name) + "\"";
                    using namespace kke::api_type;
                    if (p.type == Thing) rml << "<span class=\"me\">me</span>";
                    else if (p.type == Sound || p.type == Block)
                        rml << "<button class=\"choice\" data-act=\"cycle\"" << at << ">" << (v.empty() ? "pick" : v) << "</button>";
                    else if (p.type == Bool)
                        rml << "<button data-act=\"toggle\"" << at << ">" << (v == "false" || v == "no" ? "No" : "Yes") << "</button>";
                    else if (p.type == Number)
                        rml << "<button data-act=\"dec\"" << at << ">-</button><input type=\"text\" class=\"text\"" << at << " value=\"" << v
                            << "\"/><button data-act=\"inc\"" << at << ">+</button>";
                    else
                        rml << "<input type=\"text\" class=\"text wide\"" << at << " value=\"" << v << "\"/>";
                }
                rml << "</div>";
            }
            for (const kke::PinDef& p : d->outputs) {
                if (p.flow() && p.name == ">") continue;
                bool linked = false;
                for (const kke::GraphLink& l : m_graph->links) linked = linked || (l.fromNode == n.id && l.fromPin == p.name);
                rml << "<div class=\"row out\"><span class=\"label\">" << esc(p.label) << "</span><div class=\"port out " << typeClass(p.type)
                    << (p.flow() ? " flowport" : "") << (linked ? " linked" : "") << "\" data-port=\"" << esc(p.name)
                    << "\" data-side=\"out\"></div></div>";
            }
        }
        if (n.id == m_selectedNode) rml << "<div class=\"x\" data-act=\"delnode\">&#215;</div>";
        rml << "</div>";
    }
    m_doc->GetElementById("nodes")->SetInnerRML(rml.str());
    if (!m_fitsOpen) m_doc->GetElementById("overlay")->SetInnerRML("");
    m_doc->GetElementById("canvas")->SetClass("withlua", m_showLua);
    Rml::Element* lua = m_doc->GetElementById("lua");
    lua->SetProperty("display", m_showLua ? "block" : "none");
    m_doc->GetElementById("luabutton")->SetInnerRML(m_showLua ? "Hide Lua" : "Show Lua");
    // Always there (buttons don't move under a finger), faded with nothing to remove.
    m_doc->GetElementById("removebutton")->SetClass("off", !m_selectedNode && m_selectedLink < 0);
    if (m_showLua) rebuildLua();
    updateStatus();
}

void GraphEditor::rebuildLua() {
    std::ostringstream rml;
    rml << "<h1>The Lua this graph is (read only)</h1>";
    std::istringstream in(m_lua);
    std::string line;
    int n = 0;
    while (std::getline(in, line)) {
        ++n;
        const int node = size_t(n) <= m_lineNode.size() ? m_lineNode[size_t(n) - 1] : 0;
        bool bad = node && node == m_errorNode;
        for (const kke::GraphIssue& i : m_issues) bad = bad || (i.error && node && i.node == node);
        char num[16];
        std::snprintf(num, sizeof(num), "%3d  ", n);
        rml << "<p" << (bad ? " class=\"bad\"" : "") << "><span class=\"n\">" << num << "</span>" << esc(line) << "</p>";
    }
    m_doc->GetElementById("lua")->SetInnerRML(rml.str());
}

void GraphEditor::updateStatus() {
    int errors = 0;
    const kke::GraphIssue* first = nullptr;
    for (const kke::GraphIssue& i : m_issues) {
        if (!i.error) continue;
        ++errors;
        if (!first) first = &i;
    }
    Rml::Element* status = m_doc->GetElementById("status");
    status->SetClassNames("");
    if (!m_runtimeError.empty()) {
        status->SetClass("bad", true);
        status->SetInnerRML("It broke while running");
    } else if (errors) {
        status->SetClass("bad", true);
        status->SetInnerRML(errors == 1 ? "1 thing to fix" : std::to_string(errors) + " things to fix");
    } else if (m_graph->empty()) {
        status->SetClass("empty", true);
        status->SetInnerRML("Empty: start with a When block");
    } else {
        status->SetClass("ok", true);
        status->SetInnerRML("Working");
    }
    // A problem not on any block (the graph as a whole) shows on top.
    std::string loose;
    if (!m_runtimeError.empty() && !m_errorNode) loose = m_runtimeError;
    for (const kke::GraphIssue& i : m_issues)
        if (i.error && !i.node && loose.empty()) loose = i.message;
    Rml::Element* problem = m_doc->GetElementById("problem");
    problem->SetProperty("display", loose.empty() ? "none" : "block");
    problem->SetInnerRML(esc(loose.substr(0, 200)));
}

void GraphEditor::layoutWires() {
    m_wires.clear();
    Rml::Element* world = m_doc->GetElementById("world");
    const Rml::Vector2f wo = world->GetAbsoluteOffset(Rml::BoxArea::Border);
    const float dp = m_context->GetDensityIndependentPixelRatio();
    // Port centres, in the world's own (unzoomed) pixels.
    auto portAt = [&](int node, const std::string& pin, bool output, glm::vec2& out) {
        Rml::Element* n = m_doc->GetElementById("n" + std::to_string(node));
        if (!n) return false;
        Rml::ElementList ports;
        n->GetElementsByClassName(ports, "port");
        for (Rml::Element* p : ports) {
            if (attr(p, "data-port") != pin || (attr(p, "data-side") == "out") != output) continue;
            const Rml::Vector2f o = p->GetAbsoluteOffset(Rml::BoxArea::Border);
            const Rml::Vector2f s = p->GetBox().GetSize(Rml::BoxArea::Border);
            out = glm::vec2(o.x - wo.x + s.x * 0.5f, o.y - wo.y + s.y * 0.5f);
            return true;
        }
        return false;
    };
    for (size_t i = 0; i < m_graph->links.size(); ++i) {
        const kke::GraphLink& l = m_graph->links[i];
        Wire w{};
        const kke::GraphNode* from = m_graph->find(l.fromNode);
        const kke::NodeDef* d = from ? m_library->find(from->type) : nullptr;
        const kke::PinDef* p = d ? d->output(l.fromPin) : nullptr;
        const bool ok = portAt(l.fromNode, l.fromPin, true, w.a) && portAt(l.toNode, l.toPin, false, w.b);
        const std::string type = p ? p->type : std::string("any");
        w.color = typeColor(type);
        w.width = (type == "flow" ? 5.0f : 3.5f) * dp;
        bool lit = false;
        for (const Lit& lt : m_lit) lit = lit || lt.node == l.toNode;
        if (lit) w.color = glm::vec4(1.0f, 0.79f, 0.24f, 1.0f);
        if (int(i) == m_selectedLink) {
            w.color = glm::vec4(0.31f, 0.66f, 1.0f, 1.0f);
            w.width += 2.0f * dp;
        }
        if (!ok) w.width = 0.0f; // keeps indices matching links; not drawn
        m_wires.push_back(w);
    }
    // The wire being dragged, to the pointer.
    if (m_drag == Drag::Wire) {
        Wire w{};
        glm::vec2 port;
        if (portAt(m_dragPin.node, m_dragPin.pin, m_dragPin.output, port)) {
            const glm::vec2 mouse = toWorld(m_mousePx) * dp;
            w.a = m_dragPin.output ? port : mouse;
            w.b = m_dragPin.output ? mouse : port;
            w.color = glm::vec4(0.31f, 0.66f, 1.0f, 1.0f);
            w.width = 4.0f * dp;
            m_wires.push_back(w);
        }
    }
    // The selected wire's x, at its middle.
    std::string overlay;
    if (m_selectedLink >= 0 && size_t(m_selectedLink) < m_wires.size() && m_wires[size_t(m_selectedLink)].width > 0.0f && !m_fitsOpen) {
        const Wire& w = m_wires[size_t(m_selectedLink)];
        glm::vec2 c1, c2;
        curve(w.a, w.b, c1, c2);
        const glm::vec2 mid = bezier(w.a, c1, c2, w.b, 0.5f) * m_zoom + m_pan; // canvas pixels
        char style[96];
        std::snprintf(style, sizeof(style), "left: %.0fpx; top: %.0fpx;", double(mid.x - 13.0f * dp), double(mid.y - 13.0f * dp));
        overlay = "<div class=\"x\" data-act=\"dellink\" style=\"" + std::string(style) + "\">&#215;</div>";
    }
    if (!m_fitsOpen) {
        Rml::Element* o = m_doc->GetElementById("overlay");
        if (o->GetInnerRML() != overlay) o->SetInnerRML(overlay);
    }
    // Dots over what the canvas shows, in the world's pixels.
    if (auto* wires = dynamic_cast<GraphWires*>(m_doc->GetElementById("wires"))) {
        const Rml::Vector2f cs = m_doc->GetElementById("canvas")->GetBox().GetSize(Rml::BoxArea::Padding);
        wires->gridStep = 24.0f * dp;
        wires->gridMin = -m_pan / m_zoom;
        wires->gridMax = (glm::vec2(cs.x, cs.y) - m_pan) / m_zoom;
    }
}

GraphEditor::Result GraphEditor::update(float dt, const glm::vec2& mouse, bool mouseDown) {
    if (!isOpen() || !m_doc) return Result::None;
    m_moved = false;
    const glm::vec2 px = mouse * m_ppp;
    m_mousePx = px;
    const float dp = m_context->GetDensityIndependentPixelRatio();

    // Drags follow the pointer until the button comes up.
    if (m_drag != Drag::None) {
        const glm::vec2 delta = px - m_dragLastPx;
        m_dragLastPx = px;
        if (glm::length(px - m_dragStartPx) > 6.0f * dp) m_dragMoved = true;
        if (m_drag == Drag::Canvas && m_dragMoved) {
            m_pan += delta;
            applyTransform();
        } else if (m_drag == Drag::Node && m_dragMoved) {
            if (kke::GraphNode* n = m_graph->find(m_dragNode)) {
                n->pos = glm::round(m_nodeStart + (px - m_dragStartPx) / (m_zoom * dp));
                if (Rml::Element* e = m_doc->GetElementById("n" + std::to_string(n->id))) {
                    e->SetProperty("left", fmt(n->pos.x) + "dp");
                    e->SetProperty("top", fmt(n->pos.y) + "dp");
                }
            }
        }
        if (!mouseDown) {
            if (m_drag == Drag::Node && m_dragMoved) m_moved = true;
            if (m_drag == Drag::Canvas && !m_dragMoved && (m_selectedNode || m_selectedLink >= 0)) {
                m_selectedNode = 0; // a tap on empty canvas lets go of the selection
                m_selectedLink = -1;
                m_needRebuild = true;
            }
            if (m_drag == Drag::Wire) {
                PinRef to;
                if (pinFromElement(m_context->GetHoverElement(), to) && to.output != m_dragPin.output) {
                    const kke::GraphLink link = m_dragPin.output ? kke::GraphLink{ m_dragPin.node, m_dragPin.pin, to.node, to.pin }
                                                                 : kke::GraphLink{ to.node, to.pin, m_dragPin.node, m_dragPin.pin };
                    if (m_graph->connect(*m_library, link)) m_changed = true;
                    m_needRebuild = true;
                } else if (m_dragMoved && contains(mouse)) {
                    openFitsMenu(m_dragPin, toWorld(px));
                }
            }
            m_drag = Drag::None;
        }
    }

    // What ran fades out.
    bool litChanged = false;
    for (Lit& l : m_lit) {
        const bool was = l.left > 0.0f;
        l.left -= dt;
        litChanged = litChanged || (was && l.left <= 0.0f);
    }
    if (litChanged) {
        std::erase_if(m_lit, [](const Lit& l) { return l.left <= 0.0f; });
        m_needRebuild = m_needRebuild || m_drag == Drag::None;
    }
    for (const Lit& l : m_lit)
        if (Rml::Element* e = m_doc->GetElementById("n" + std::to_string(l.node))) e->SetClass("lit", true);

    // Fitting needs the blocks laid out: never in the frame they're rebuilt.
    bool rebuilt = false;
    if (m_needRebuild && m_drag != Drag::Node) {
        rebuild();
        rebuilt = true;
    }
    if (m_fitPending && !rebuilt && !m_graph->empty()) {
        // Everything in view: the blocks' extent, fitted with a margin.
        glm::vec2 mn(1e9f), mx(-1e9f);
        for (const kke::GraphNode& n : m_graph->nodes) {
            glm::vec2 size(200.0f, 120.0f);
            if (Rml::Element* e = m_doc->GetElementById("n" + std::to_string(n.id))) {
                const Rml::Vector2f s = e->GetBox().GetSize(Rml::BoxArea::Border);
                size = glm::vec2(s.x, s.y) / dp;
            }
            mn = glm::min(mn, n.pos);
            mx = glm::max(mx, n.pos + size);
        }
        const Rml::Vector2f cs = m_doc->GetElementById("canvas")->GetBox().GetSize(Rml::BoxArea::Padding);
        const glm::vec2 view = glm::vec2(cs.x, cs.y) / dp;
        const glm::vec2 extent = glm::max(mx - mn, glm::vec2(1.0f)) + glm::vec2(80.0f);
        m_zoom = std::clamp(std::min(view.x / extent.x, view.y / extent.y), 0.3f, 1.2f);
        m_pan = (glm::vec2(cs.x, cs.y) - (mn + mx) * (m_zoom * dp)) * 0.5f;
        applyTransform();
        m_fitPending = false;
    } else if (m_fitPending && m_graph->empty()) {
        m_zoom = 1.0f;
        m_pan = glm::vec2(40.0f * dp);
        applyTransform();
        m_fitPending = false;
    }
    layoutWires();

    if (m_closeAsked) {
        m_closeAsked = false;
        close();
        return Result::Closed;
    }
    const bool changed = m_changed;
    m_changed = false;
    return changed ? Result::Changed : m_moved ? Result::Moved : Result::None;
}

} // namespace kke_sandbox
