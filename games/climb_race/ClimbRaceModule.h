#pragma once

#include "kke/AnimRig.h"
#include "kke/Animator.h"
#include "kke/CameraRig.h"
#include "kke/ClimbWall.h"
#include "kke/Climber.h"
#include "kke/Locomotion.h"
#include "kke/Module.h"
#include "kke/RigidWorld.h"
#include "kke/modules/ModelModule.h"

#include <RmlUi/Core/DataModelHandle.h>

#include <memory>
#include <string>
#include <vector>

namespace kke {
class DynamicMeshRenderer;
class InputModule;
class RigidBodyModule;
class UiModule;
} // namespace kke
namespace Rml { class ElementDocument; }

namespace climb_race {

// Climb Race: two identical generated rock faces side by side, like a
// speed-climbing final. You start on the ground in front of yours; the
// first to mantle over the summit wins. On the rock you choose each hold
// yourself (kke::Climber): a bumper reaches precisely, a trigger held and
// let go lunges (further the longer you hold), both together snatch
// quickly. Stamina runs out on bad holds and overhangs; stand on a ledge
// or hang on two jugs to get it back. Loose holds come off under a lunge
// and fall down the face (Jolt bodies).
//
// The rival on the other face is kke::ClimbBot, or a second player with a
// controller in split screen (F2). Walking, jumping and catching ledges
// between climbs is kke::Locomotion; the body is the UAL mannequin with
// two-bone IK on all four limbs.
//
// Headless / demo switches: KKE_CLIMB_SEED=<n> (the mountain),
// KKE_CLIMB_AUTOPILOT=1 (you climb by yourself too), KKE_CLIMB_SPLIT=1
// (split screen from the start), KKE_CLIMB_BOT_PAUSE=<s> (the rival's
// breath between moves), KKE_CLIMB_QUIT=<s> (quit after that long, with a
// log of both climbers' heights every few seconds), KKE_CLIMB_ROCKFALL=1
// (every loose hold on your face comes off two seconds in: watch them bounce
// down the rock and the ledges; where they came to rest is logged).
class ClimbRaceModule : public kke::Module {
public:
    ClimbRaceModule();
    ~ClimbRaceModule() override;
    const char* name() const override { return "ClimbRace"; }
    std::vector<kke::ModuleDependency> dependencies() const override;
    void init(kke::Application& app) override;
    void update(const kke::UpdateContext& ctx) override;
    void render(const kke::RenderContext& ctx) override;
    void renderShadow(const kke::ShadowRenderContext& ctx) override;
    void onEvent(const SDL_Event& event) override;

private:
    // One face of the mountain (a lane): the generated rock, its Jolt
    // bodies, what draws it, and its loose holds.
    struct Loose {
        int hold = -1;
        kke::RigidWorld::BodyId body = kke::RigidWorld::kNoBody;
        std::unique_ptr<kke::DynamicMeshRenderer> mesh;
        bool fallen = false;
    };
    struct Lane {
        std::unique_ptr<kke::ClimbWall> wall;
        glm::vec3 offset{0.0f}; // wall space -> world
        std::vector<kke::RigidWorld::BodyId> bodies;
        std::unique_ptr<kke::DynamicMeshRenderer> mesh;
        std::vector<Loose> loose;
    };
    struct Racer {
        int lane = 0;
        int player = 0;             // input map; for a bot, the map it would use
        bool bot = false;
        std::string name;
        glm::vec3 tint{1.0f};
        kke::RigidWorld::CharacterId id = 0;
        std::unique_ptr<kke::Locomotion> loco;
        std::unique_ptr<kke::Climber> climber;
        std::unique_ptr<kke::ClimbBot> brain;
        kke::CameraRig rig;
        kke::Camera camera;
        kke::ModelModule::InstanceId model = 0;
        std::unique_ptr<kke::Animator> anim;
        float armWeight = 0.0f, legWeight = 0.0f, footWeight = 0.0f;
        float grip[2] = {};         // fingers closed on a hold (0 open .. 1 closed)
        float handAim[2] = {};      // hand turned to its hold (0 = as animated)
        struct { float worst = 0.0f, sum = 0.0f; int samples = 0; } gripError; // knuckles to hold, m
        float time = 0.0f;          // race clock
        bool finished = false;
        float regrab = 0.0f;        // after a fall: no grabbing for a moment
        float idleLook = 0.0f;      // seconds without looking around (camera settles)
        float restTimer = 0.0f;     // bot standing on a ledge
        int crosshair = -1;         // hold under the crosshair (mouse aiming)
        bool crosshairOut = false;  // ... out of reach
        bool jumpQueued = false;
        bool wasClimbing = false;
        float fallStartY = 0.0f;
        int falls = 0;
    };
    struct RacerInput {
        kke::Locomotion::Input loco;
        kke::Climber::Input climb;
        glm::vec2 look{0.0f};        // degrees this frame
        bool grab = false;           // any grab button: get on the rock
        bool mantle = false;         // jump: over the edge
    };

    void buildMountain(uint32_t seed);
    void clearMountain();
    void buildScenery();
    void spawnRacers();
    void resetRace();
    void setSplit(bool on);
    void assignControllers();

    RacerInput readPlayer(Racer& r, float dt);
    RacerInput readBot(Racer& r, float dt);
    void updateRacer(Racer& r, float dt);
    // The rival's breath between moves; your autopilot is a little quicker.
    float botPause(const Racer& r) const { return r.lane == 0 ? m_botPause * 0.8f : m_botPause; }
    void updateCamera(Racer& r, float dt, kke::Camera& out);
    int crosshairHold(const Racer& r, const kke::Camera& cam, bool& outOfReach) const;
    void dropLoose(Lane& lane, int hold, const glm::vec3& push);

    // The character (Body.cpp).
    void loadCharacter();
    void setupBody(Racer& r);
    void animateBody(Racer& r, float dt);
    // A climber on lane `lane`, with the body's proportions measured from
    // the character's skeleton (so every hold it takes is one the arms reach).
    std::unique_ptr<kke::Climber> makeClimber(int lane) const;

    // The HUD (Hud.cpp, ui/climb_hud.rml).
    void buildHud();
    void updateHud(float dt);

    glm::vec3 toWorld(const Racer& r, const glm::vec3& p) const { return p + m_lanes[static_cast<size_t>(r.lane)]->offset; }
    glm::vec3 toWall(const Racer& r, const glm::vec3& p) const { return p - m_lanes[static_cast<size_t>(r.lane)]->offset; }

    kke::Application* m_app = nullptr;
    kke::RigidBodyModule* m_rigid = nullptr;
    kke::InputModule* m_input = nullptr;
    kke::ModelModule* m_models = nullptr;

    uint32_t m_seed = 7;
    std::vector<std::unique_ptr<Lane>> m_lanes;
    std::vector<Racer> m_racers;
    std::vector<kke::RigidWorld::BodyId> m_scenery;
    std::unique_ptr<kke::DynamicMeshRenderer> m_ground, m_markers[4];

    enum class Phase { Countdown, Racing, Finished };
    Phase m_phase = Phase::Countdown;
    float m_countdown = 3.0f;
    float m_best = 0.0f;       // best time on this mountain (0 = none yet)
    std::string m_winner;
    bool m_split = false, m_autopilot = false, m_captured = false;
    // How to play: up when the game starts (KKE_CLIMB_INTRO=0 skips it,
    // =1 forces it; headless runs skip it) and on the help button. The
    // race waits while it's up.
    bool m_howto = false;
    void showHowTo(bool on);
    float m_botPause = 0.45f;
    float m_quitAfter = -1.0f, m_clock = 0.0f, m_reportAt = 0.0f;
    float m_rockfall = -1.0f; // KKE_CLIMB_ROCKFALL: seconds until it starts (-1 = off)
    void updateRockfall(float dt);
    float m_mouseSensitivity = 0.12f, m_stickSpeed = 200.0f;
    float m_climbCamera = 4.6f; // m behind you on the rock (KKE_CLIMB_CLOSEUP: nearer, to see the hands)

    // Character: the UAL mannequin's bones and clips (retargeting not
    // needed: it's the clips' own skeleton).
    kke::ModelModule::ModelId m_charModel = 0;
    kke::ModelData m_rigData;
    std::unique_ptr<kke::AnimationSet> m_animSet;
    float m_modelYaw = 0.0f;
    kke::TwoBoneChain m_arm[2], m_leg[2];
    kke::FootPlacer m_feet;
    int m_pelvis = -1;
    // Each hand as it is in the rest pose: which way the fingers point and
    // which way the thumb side faces (model space), to turn it onto a hold;
    // the finger bones, to close them around it.
    struct HandRig {
        glm::quat restModel{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 fingers{0.0f, 1.0f, 0.0f}, thumbSide{1.0f, 0.0f, 0.0f};
        float knuckles = 0.09f;     // m from the wrist to the knuckles
        int segment[4][3] = { { -1, -1, -1 }, { -1, -1, -1 }, { -1, -1, -1 }, { -1, -1, -1 } }; // index, middle, ring, pinky
        int thumb[3] = { -1, -1, -1 };
    };
    HandRig m_handRig[2];
    kke::Climber::Settings m_climbSettings; // proportions from the skeleton
    int m_stMove = -1, m_stJump = -1, m_stFall = -1, m_stLand = -1, m_stHang = -1, m_stTop = -1;
    std::unique_ptr<kke::DynamicMeshRenderer> m_capsule; // no character model: a block

    // HUD.
    struct PlayerHud {
        std::string name, time, stamina = "100%", staminaColor = "#6fe39a", left, right, height, status;
        bool low = false;
    };
    struct Hud {
        PlayerHud p[2];
        std::string banner, sub, hint;
        bool split = false;
        bool howto = false;         // the how-to-play screen is up
    };
    Hud m_hud;
    Rml::DataModelHandle m_hudModel;
    Rml::ElementDocument* m_hudDoc = nullptr;
};

} // namespace climb_race
