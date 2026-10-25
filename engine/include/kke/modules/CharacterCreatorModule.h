#pragma once

#include "kke/Hair.h"
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

class HairRenderer;
class OrbitCameraModule;
class RigidWorld;
class UiModule;

// A character creator screen (docs/HUMANS.md "The creator screen"): a
// MakeHuman body in the middle, tabs of sliders on the right (body type,
// shape, torso, arms and legs, head, eyes, nose, mouth, ears, skin,
// hair: strands grown from the body's own scalp, simulated by the engine's
// hair physics against the body; clothes: tops, shorts, trousers, socks
// and shoes that fit any body, skirts as the engine's cloth),
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
    void fixedUpdate(const FixedUpdateContext& ctx) override;
    void compute(VkCommandBuffer cmd) override;
    void render(const RenderContext& ctx) override;
    void renderShadow(const ShadowRenderContext& ctx) override;
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
    // The current body, dressed, as a skinned model (bones and weights), for a game
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
    struct HairChoice {
        Rml::String id, label; // id "" = no hair
    };
    struct GarmentView {
        Rml::String id, label, slot;
        bool worn = false;
    };
    struct WornPiece; // a garment on the figure: a model, or cloth and its renderer

    void buildModel();
    void loadDocument();
    void fillSliders();
    void pushUndo();
    void restore(const std::map<std::string, float>& values);
    void reshape();
    void spawnBody();
    void setStatus(const std::string& text);
    void applyColours();
    void applyStyle();     // m_anime on every model of the figure
    void randomise(bool everything);
    // A body type: "Average", "Slim", "Athletic", "Strong", "Curvy",
    // "Heavy" (the build sliders only: gender, age and face stay).
    void applyPreset(const std::string& preset);
    void save();
    void load();
    void buildWorld();     // the physics world: the body as a collider
    void buildHair();      // m_look's hairstyle on the body as it is now
    void placeFigure();    // the body, its collider and the hair's head at m_yaw
    void moveCap();        // the scalp cap onto the body as it is now
    void moveIrises();     // the pupils and irises onto the eyes as they are now
    void moveBrows();      // the brows and lashes onto the face as it is now
    void dressUp();        // m_look's clothes on the body as it is now
    void moveClothes();    // the fitted clothes onto the body as it is now (a drag)
    void undress();
    void fillClothes();    // the Clothes tab's buttons
    void wear(const std::string& id); // on, taking off what was in its slot; off if it was on

    Application* m_app = nullptr;
    UiModule* m_ui = nullptr;
    ModelModule* m_models = nullptr;
    OrbitCameraModule* m_orbit = nullptr;
    std::string m_saveName;
    std::unique_ptr<HumanPack> m_pack;
    std::string m_humansDir; // assets/humans: the pack and the skin maps
    void skinMaterial(ModelData& model) const; // the skin maps on "Human_body"
    std::unique_ptr<Human> m_human;
    HumanLook m_look;
    Human::Bake m_bodyBake, m_partsBake;
    ModelModule::InstanceId m_body = 0, m_parts = 0;
    // The scalp painted the hair's root colour, so no skin shows between
    // the hairs (docs/HAIR.md): the skin's scalp triangles, a hair over it.
    ModelModule::InstanceId m_cap = 0;
    std::vector<uint32_t> m_capFrom; // cap vertex -> body model vertex
    ModelData m_capModel;
    // Pupils and irises (kke/HumanEyes.h): white, tinted the eye colour.
    ModelModule::InstanceId m_irises = 0;
    ModelData m_irisModel;
    ModelModule::InstanceId m_brows = 0;
    ModelData m_browModel;
    bool m_shapeDirty = false;
    bool m_open = false;
    bool m_selfTested = false;
    DoneFn m_onDone;

    Rml::ElementDocument* m_doc = nullptr;
    Rml::DataModelHandle m_handle;
    std::vector<TabView> m_tabs;
    Rml::String m_tab = "body";
    std::vector<SliderView> m_sliders;
    std::vector<SwatchView> m_skinSwatches, m_eyeSwatches, m_hairSwatches;
    std::vector<HairChoice> m_hairChoices;
    std::vector<GarmentView> m_garments;
    std::vector<Rml::String> m_presets;
    std::vector<SwatchView> m_clothSwatches;
    Rml::String m_clothSlot = "top", m_clothSlotLabel; // what a colour swatch dyes
    float m_hairLength = 1.0f, m_hairLengthShown = 1.0f;
    Rml::String m_hairStyle;
    bool m_mirror = true;
    bool m_turntable = false;
    bool m_anime = false; // the Anime style: toon shading and outlines (ModelModule::setToon)
    Rml::String m_code, m_codeShort, m_status, m_view = "body";
    float m_statusTime = 0.0f;

    // Hair: a world of its own (the body is a still collider in it), the
    // strands simulated there and drawn by a HairRenderer.
    std::unique_ptr<RigidWorld> m_world;
    uint32_t m_bodyCollider = 0xffffffffu;
    uint32_t m_hair = 0;
    size_t m_hairRoots = 0; // guides buildHair asked for (cornrows have only 6-14)
    std::unique_ptr<HairRenderer> m_hairDrawn;
    std::vector<glm::vec3> m_guides;
    bool m_hairDirty = false; // rebuild once the sliders rest
    float m_yaw = 0.0f;       // the turntable turns the figure, so the hair swings
    glm::mat4 m_figure{ 1.0f };

    // Clothes (kke/HumanClothes.h): the body suit baked like the body, so
    // the fitted garments follow every slider; skirts hang in m_world.
    Human::Bake m_tightsBake;
    std::vector<std::unique_ptr<WornPiece>> m_worn;
    bool m_clothesDirty = false; // rebuild once the sliders rest
    uint32_t m_garmentSerial = 0; // model keys: each fitting is a model of its own

    std::vector<std::map<std::string, float>> m_undo, m_redo;
    float m_sinceEdit = 1e9f;
    std::mt19937 m_rng;
};

} // namespace kke
