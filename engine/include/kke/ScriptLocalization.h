#pragma once
// ScriptLocalization: the loc.* Lua API over kke::Localizer (docs/LOCALIZATION.md).
#include "kke/Localization.h"

#include <functional>
#include <string>
#include <vector>

namespace kke {

class ScriptVM;

class ScriptLocalization {
public:
    // `folder`: the game's own folder; loc.load and loc.loadFolder read inside it only.
    explicit ScriptLocalization(std::string folder = {}) : m_folder(std::move(folder)) {}
    void bind(ScriptVM& vm);
    // Takes away what `source` added (its loc.add tables stay: a language is the game's).
    void release(const std::string&) {}

    Localizer& localizer() { return m_loc; }
    // Called with the font files (names, as in fonts.json) a language needs
    // whenever it is chosen: the UI loads them as fallback faces.
    std::function<void(const std::vector<std::string>& files)> onFonts;
    void setFolder(std::string f) { m_folder = std::move(f); }

private:
    Localizer m_loc;
    std::string m_folder;
};

} // namespace kke
