#pragma once

#include <glm/glm.hpp>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Rml {
class Context;
class ElementDocument;
}

namespace kke_sandbox {

class PlayPaletteListener;

// Play mode's row of big pictures along the bottom (play-to-make's Simple
// level, docs/PLAY_TO_MAKE.md), in RmlUi: what a player uses is RmlUi, the
// ImGui windows are for developers (docs/DEMO_PANEL.md).
//
// The sandbox says what the row holds each frame (set()); the document is
// rebuilt only when that changes. A press on a picture calls onPress with
// its id at once, on the press and not the release, so a picture can be
// dragged out into the world and let go there. A finger and the gamepad's
// cursor are the mouse, so they press pictures the same way.
class PlayPalette {
public:
    struct Cell {
        std::string id, label;
        std::string word;  // big text when there is no picture ("Hand", "Up!")
        std::string image; // a thumbnail PNG on disk, or empty
        bool on = false;   // the tool in hand
    };

    PlayPalette();
    ~PlayPalette();
    PlayPalette(const PlayPalette&) = delete;
    PlayPalette& operator=(const PlayPalette&) = delete;

    // Needs a UiModule's context; without one nothing is shown.
    void attach(Rml::Context* context, float pixelsPerPoint);
    // Closes the document; before the UiModule shuts RmlUi down.
    void detach();

    void setVisible(bool visible);
    void set(const std::string& hint, const std::vector<Cell>& cells);
    // What graphs say and the score, big at the top (empty: none).
    void setWords(const std::string& said, const std::string& score);

    // Whether a point (window points, like SDL's mouse) is on the row.
    bool contains(const glm::vec2& point) const;
    // The pictures' centres in window points, left to right (for the
    // gamepad's LB / RB, kke::stepPaletteCell).
    std::vector<glm::vec2> cellCentres() const;

    std::function<void(const std::string& id)> onPress;

private:
    friend class PlayPaletteListener;
    Rml::Context* m_context = nullptr;
    Rml::ElementDocument* m_doc = nullptr;
    std::unique_ptr<PlayPaletteListener> m_listener;
    float m_ppp = 1.0f;
    bool m_visible = false;
    std::string m_shown; // the RML last built, to rebuild only on change
    std::string m_words; // said + score shown
};

} // namespace kke_sandbox
