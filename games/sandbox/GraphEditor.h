#pragma once

#include "kke/NodeGraph.h"

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <vector>
#include <utility>

namespace Rml {
class Context;
class Element;
class ElementDocument;
class EventListener;
}

namespace kke_sandbox {

class GraphWires;

// The node graph window of play-to-make's Intermediate level
// (docs/PLAY_TO_MAKE.md), in RmlUi and styled after Drawflow
// (github.com/jerosoler/drawflow, MIT): white rounded blocks with a
// coloured head, round ports on their edges, soft curved wires, a dotted
// canvas. Drawflow is a JavaScript library, so its look (the RCSS in
// GraphEditor.cpp) and its way of working are ported, not its code.
//
// It edits one kke::NodeGraph in place; the sandbox compiles and reloads
// the graph whenever update() says it changed, so every edit is live in
// the game running behind it.
//
// Made for fingers and gamepads as much as a mouse (one finger and the
// gamepad's cursor are the mouse):
// - Blocks are added by tapping them in the list on the left (they land in
//   the middle of the view), or by dragging a wire from a port into empty
//   space, which offers only the blocks that fit it and wires them up.
// - A wire is made by dragging from a port to another; dragging from a
//   wired input picks that wire up again. Tapping a wire or a block
//   selects it and shows a round x to remove it.
// - Values change with - / + buttons and by tapping through choices, so
//   typing is rarely needed.
// - Drag the empty canvas to move around; the wheel, pinch, the zoom
//   buttons or the triggers zoom. pan()/zoom() take two-finger gestures
//   and the right stick (the sandbox routes them here).
class GraphEditor {
public:
    GraphEditor();
    ~GraphEditor();
    GraphEditor(const GraphEditor&) = delete;
    GraphEditor& operator=(const GraphEditor&) = delete;

    // Needs a UiModule's context; without one the editor stays closed.
    void attach(Rml::Context* context, float pixelsPerPoint);
    // Closes the document; before the UiModule shuts RmlUi down.
    void detach();

    // Starts editing `graph` (kept by pointer: it must outlive the edit or
    // be reopened). `title` is what it belongs to: "Bat", "Level", ...
    void open(kke::NodeGraph* graph, const kke::NodeLibrary* library, std::string title);
    void close();
    bool isOpen() const { return m_graph != nullptr; }
    const kke::NodeGraph* graph() const { return m_graph; }

    // What the last compile said, shown on the blocks, and the Lua it made.
    void setCompiled(const kke::CompiledGraph& compiled, const std::string& shownLua);
    // A Lua error while it ran, on node `node` (0: the graph as a whole).
    void setRuntimeError(int node, const std::string& message);
    // `node` just ran: it and the wires into it light up for a moment.
    void ran(int node);
    // The block ids a "What" port can be set to, in palette order.
    void setBlockChoices(std::vector<std::string> blocks) { m_blocks = std::move(blocks); }

    enum class Result { None, Changed, Moved, Closed };
    // Once a frame. `mouse`: the pointer in window points and whether the
    // (first) button is down; drags follow it even off the window.
    // Changed: recompile and reload. Moved: only positions changed (save,
    // don't restart the graph).
    Result update(float dt, const glm::vec2& mouse, bool mouseDown);

    // Whether a point (window points) is on the editor.
    bool contains(const glm::vec2& point) const;
    void pan(const glm::vec2& deltaPoints);
    void zoom(float steps, const glm::vec2& aroundPoints); // + in, - out
    void removeSelected();
    void showAll();

    // For GraphWires: what to draw.
    struct Wire { glm::vec2 a, b; glm::vec4 color; float width; };
    const std::vector<Wire>& wires() const { return m_wires; }
    glm::vec2 gridOffset() const { return m_pan; }
    float zoomLevel() const { return m_zoom; }

    // The listener's entry points (RmlUi events on the document).
    void onMouseDown(Rml::Element* target, const glm::vec2& px);
    void onClick(Rml::Element* target);
    void onValue(Rml::Element* target, const std::string& value);
    void onScroll(float delta, const glm::vec2& px);

private:
    struct PinRef { int node = 0; std::string pin; bool output = false; };
    bool pinFromElement(Rml::Element* e, PinRef& out) const;
    int nodeFromElement(Rml::Element* e) const;

    void rebuild();                 // the blocks, from the graph
    void rebuildLua();
    void updateStatus();
    void applyTransform();
    void layoutWires();             // port positions -> wires
    glm::vec2 toWorld(const glm::vec2& px) const; // window pixels -> graph units
    glm::vec2 canvasOrigin() const;
    int addNode(const std::string& type, glm::vec2 at);
    void setValue(int node, const std::string& pin, const std::string& value);
    int hitWire(const glm::vec2& world) const;
    void openFitsMenu(const PinRef& from, const glm::vec2& world);

    Rml::Context* m_context = nullptr;
    Rml::ElementDocument* m_doc = nullptr;
    std::unique_ptr<Rml::EventListener> m_listener;
    float m_ppp = 1.0f;

    kke::NodeGraph* m_graph = nullptr;
    const kke::NodeLibrary* m_library = nullptr;
    std::string m_title;
    std::vector<std::string> m_blocks;

    // View: graph units are dp; pan in pixels.
    glm::vec2 m_pan{0.0f};
    float m_zoom = 1.0f;
    bool m_fitPending = false;

    enum class Drag { None, Canvas, Node, Wire };
    Drag m_drag = Drag::None;
    glm::vec2 m_dragStartPx{0.0f}, m_dragLastPx{0.0f};
    glm::vec2 m_nodeStart{0.0f};
    bool m_dragMoved = false;
    int m_dragNode = 0;
    PinRef m_dragPin;
    glm::vec2 m_mousePx{0.0f};

    int m_selectedNode = 0;
    int m_selectedLink = -1;      // index into m_graph->links

    // A wire let go on empty canvas: the blocks that fit it.
    bool m_fitsOpen = false;
    PinRef m_fitsFrom;
    glm::vec2 m_fitsAt{0.0f};

    std::vector<kke::GraphIssue> m_issues;
    std::string m_lua;
    std::vector<int> m_lineNode;
    int m_errorNode = 0;
    std::string m_runtimeError;
    bool m_showLua = false;
    struct Lit { int node; float left; };
    std::vector<Lit> m_lit;       // nodes that just ran, and for how much longer

    std::vector<Wire> m_wires;
    bool m_changed = false, m_moved = false, m_closeAsked = false, m_needRebuild = false;
};

} // namespace kke_sandbox
