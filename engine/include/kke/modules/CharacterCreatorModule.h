#pragma once

#include "kke/Human.h"
#include "kke/HumanLook.h"
#include "kke/HumanPack.h"
#include "kke/Module.h"
#include "kke/modules/ModelModule.h"

#include <RmlUi/Core/DataModelHandle.h>

#include <functional>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace Rml {
class ElementDocument;
class Event;
}

namespace kke {

class OrbitCameraModule;
class UiModule;

// A character creator screen (docs/HUMANS.md "The creator screen"): a
// MakeHuman body in the middle, tabs of sliders on the right (body type,
// shape, torso, arms and legs, head, eyes, nose, mouth, ears, skin),
// mirror, undo and redo, randomise, camera views, save, load and the
// share code other players need to see this character.
//
// Any game can add it: it needs a UiModule and a ModelModule (and uses an
// OrbitCameraModule when there is one). The screen's files are
// assets/ui/character_creator/ (kke_use_character_creator(TARGET) in
// CMake copies them and the human pack next to the game). A game shows it
// with open(), hears onDone() and reads look() or shareCode().
//
// While a slider moves only the vertices move (ModelModule's deformed
// vertices); bakeSkinned() gives the game the finished, animatable model.
class CharacterCreatorModule : public Module {
public:
    // saveName: the file in the game's save folder ("character" ->
    // character.kkhuman).
    explicit CharacterCreatorModule(std::string saveName = "character");
    ~CharacterCreatorModule() override;
    const char* name() const override { return "CharacterCreator"; }

    void init(Application& app) override;
    void update(const UpdateContext& ctx) override;
    void shutdown() override;

    void open();
    void close();
    bool isOpen() const { return m_open; }

    // The finished character: called by the screen's Done button.
    using DoneFn = std::function<void(const HumanLook&)>;
    void onDone(DoneFn fn) { m_onDone = std::move(fn); }

    // The look as other players will see it (rounded like the share code).
    HumanLook look() const;
    std::string shareCode() const;
    void setLook(const HumanLook& look);
    const HumanPack* pack() const { return m_pack.get(); }
    // The current body as a skinned model (bones and weights), for a game
    // to add to its ModelModule and animate.
    ModelData bakeSkinned() const;

    // Where the save goes (empty when the platform has no save folder).
    std::string savePath() const;

    // The view the camera keeps to: "body", "face", "profile" or "back".
    void setView(const std::string& view);

private:
    struct TabView {
        Rml::String id, label;
    };
    struct SliderView {
        Rml::String name, label, pair; // pair: the other side's slider while mirrored
        float value = 0.0f, min = 0.0f, max = 1.0f;
        float shown = 0.0f; // what the screen showed last frame, to spot drags
    };
    struct SwatchView {
        Rml::String name, css;
        float r = 0, g = 0, b = 0;
    };

    void buildModel();
    void loadDocument();
    void fillSliders();
    void pushUndo();
    void restore(const std::map<std::string, float>& values);
    void reshape();
    void spawnBody();
    void setStatus(const std::string& text);
    void applyColours();
    void randomise(bool everything);
    void save();
    void load();

    Application* m_app = nullptr;
    UiModule* m_ui = nullptr;
    ModelModule* m_models = nullptr;
    OrbitCameraModule* m_orbit = nullptr;
    std::string m_saveName;
    std::unique_ptr<HumanPack> m_pack;
    std::unique_ptr<Human> m_human;
    HumanLook m_look;
    Human::Bake m_bodyBake, m_partsBake;
    ModelModule::InstanceId m_body = 0, m_parts = 0;
    bool m_shapeDirty = false;
    bool m_open = false;
    bool m_selfTested = false;
    DoneFn m_onDone;

    Rml::ElementDocument* m_doc = nullptr;
    Rml::DataModelHandle m_handle;
    std::vector<TabView> m_tabs;
    Rml::String m_tab = "body";
    std::vector<SliderView> m_sliders;
    std::vector<SwatchView> m_skinSwatches, m_eyeSwatches;
    bool m_mirror = true;
    bool m_turntable = false;
    Rml::String m_code, m_codeShort, m_status, m_view = "body";
    float m_statusTime = 0.0f;

    std::vector<std::map<std::string, float>> m_undo, m_redo;
    float m_sinceEdit = 1e9f;
    std::mt19937 m_rng;
};

} // namespace kke
