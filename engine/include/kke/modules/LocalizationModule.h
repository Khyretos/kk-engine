#pragma once

#include "kke/Capabilities.h"
#include "kke/Localization.h"
#include "kke/Module.h"

#include <string>
#include <vector>

namespace kke {

// The player's language (ENG-F84, docs/LOCALIZATION.md): loads the language
// files, follows the `gameplay.language` setting, and gives scripts and
// documents their text.
//
// Language files are looked for in assets/lang (KKE_LANG_DIR) and then in the
// game's own lang/ folder next to its scripts, so a game adds to the engine's
// strings and replaces them. Files are JSON or YAML (see kke/Localization.h
// for the layout). With none found, every key shows as itself.
//
// In documents: `<p i18n="menu.start"/>` shows the text of a key, `i18n-title`,
// `i18n-placeholder` and `i18n-value` do the same for those attributes,
// `i18n-count="3"` picks the plural form, `i18n-args="name=Kees;gold=5"` fills
// {name} and {gold}. They are translated when a document opens and again
// whenever the language changes. In Lua: `i18n.tr`, `i18n.trn`, `i18n.setLanguage`...
class LocalizationModule : public Module, public ISettingsListener, public ILocalizer {
public:
    const char* name() const override { return "Localization"; }
    std::vector<ModuleDependency> dependencies() const override;
    void init(Application& app) override;
    void update(const UpdateContext& ctx) override;
    void onSettingsChanged(const EngineSettings& settings) override;

    Localization& localization() override { return m_loc; }
    // "auto" (the machine's language, else the default) or a code. False
    // when nothing is loaded for a code; the language then stays as it was.
    bool chooseLanguage(const std::string& setting);
    // Folders language files were read from, for the log and the docs.
    const std::vector<std::string>& folders() const { return m_folders; }

private:
    void registerLua();

    Application* m_app = nullptr;
    Localization m_loc;
    std::vector<std::string> m_folders;
    std::string m_setting;     // the last setting applied
    bool m_hookPending = false; // LanguageChanged fires from update(), never from inside a script call
};

} // namespace kke
