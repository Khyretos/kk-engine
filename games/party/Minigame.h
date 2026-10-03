#pragma once

// A minigame (README.md "Making a minigame"): its level, its rules, and
// how the CPU beans play it. The party (PartyModule) owns the beans, the
// cameras, the show and the network; a minigame gets them through Arena.
// Each lives in minigames/<Name>.cpp and is listed in Minigames.cpp.

#include "Bean.h"
#include "Show.h"

#include "kke/CameraRig.h"
#include "kke/RigidWorld.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace kke {
class Application;
class DynamicMeshRenderer;
struct RenderContext;
struct ShadowRenderContext;
} // namespace kke

namespace party {

struct PersonBody; // People.h

// What a bean wants this frame: from a player's controls, a CPU's brain,
// or (online) another machine.
struct BeanInput {
    glm::vec2 move{0.0f};  // world X/Z, length 0..1
    bool jump = false;     // pressed this frame
    bool dive = false;     // pressed this frame
    bool jumpHeld = false;
    bool diveHeld = false;
    bool push = false;     // pressed this frame: shove whoever is in front
};

// One player or CPU in the party.
struct Bean {
    // Who.
    int seat = -1;             // lobby seat (-1: a CPU, or another screen's)
    int player = 0;            // InputModule player (this screen's players)
    bool bot = false;
    int difficulty = 1;        // CPU: 0 Easy .. 3 Expert
    bool remote = false;       // another machine plays it
    int netId = -1;            // network player (-1: offline)
    std::string name;
    BeanLook look;
    int index = 0;             // in the party's roster (points)

    // The body. The capsule is a RigidWorld character, moved by our own
    // controller (PartyModule::moveBean): run, jump, dive, get knocked.
    kke::RigidWorld::CharacterId id = 0;
    glm::vec3 velocity{0.0f};  // horizontal, what the controller asks for
    glm::vec3 push{0.0f};      // knocked: extra velocity that fades
    float launch = 0.0f;       // knocked: m/s upwards, next step
    float bumped = 0.0f;       // s before another bump counts
    glm::vec3 drawFeet{0.0f};  // remote: where it's drawn (smoothed toward its machine's)
    float yaw = 0.0f;          // degrees, 0 = -Z
    bool grounded = true;
    float stun = 0.0f;         // s: tumbling, no control
    float dive = 0.0f;         // s left of a dive (belly slide)
    float airTime = 0.0f;
    bool active = true;        // in this round (false: out, or finished and waiting)
    bool finished = false;
    bool out = false;
    bool hidden = false;       // not drawn (out, gone in a poof)
    RoundResult result;
    glm::vec3 checkpoint{0.0f}; // where a fall puts you back (races)
    float checkpointYaw = 0.0f;
    BeanInput input;           // this frame's
    float frozen = 0.0f;       // s the controls are locked (the minigame's rule)
    bool pulling = false;      // set by the minigame: hauling on a rope (drawn leaning back)
    // Stamina (Beans.cpp): jumping, diving and pushing use it, standing
    // still and running on the ground bring it back.
    float stamina = 1.0f;      // 0..1
    float diveCooldown = 0.0f; // s before the next dive
    float pushCooldown = 0.0f; // s before the next push
    float pushPose = 0.0f;     // s left of the push's arms-out (drawing)
    float charge = 0.0f;       // s the dive button has been held (games with a charged dive)
    float divePower = 1.0f;    // the last dive's power: 1, up to 2 fully charged
    bool clashing = false;     // locked in a clash (Beans.cpp): mash to win it
    float jumpBuffer = 0.0f;   // s a jump press waits for the ground
    float groundTime = 0.0f; // s on the ground since the last landing
    int repeatJumps = 0;       // jumps in a row: each goes less high
    bool jumped = false;       // in the air from a jump of our own
    // Before GO: where the bean may move (its start area).
    bool fenced = false;
    glm::vec3 fenceMin{0.0f}, fenceMax{0.0f};

    // Drawing: squash and stretch, leaning into turns, waddling.
    float squash = 0.0f, squashVel = 0.0f;
    glm::vec2 lean{0.0f}, leanVel{0.0f};
    float walkPhase = 0.0f, tumble = 0.0f, wave = 0.0f;
    glm::vec3 lastVelocity{0.0f};
    std::shared_ptr<PersonBody> person; // a person's model and animator (People), or none: a bean
    std::shared_ptr<kke::DynamicMeshRenderer> body, limb;
    BeanLook builtLook;
    bool meshBuilt = false;

    // The player's camera (split screen: one each).
    kke::CameraRig rig;
    kke::Camera camera;
    int watching = -1;         // out: whose round they watch (bean index)
    float idleLook = 0.0f;

    // Minigame scratch: what the running minigame keeps per bean.
    float a = 0.0f, b = 0.0f, c = 0.0f;
    int i = 0, j = 0;
    glm::vec3 v{0.0f};
    float botTimer = 0.0f;

    glm::vec3 feet(const kke::RigidWorld& w) const { return w.characterPosition(id); }
};

// A piece of a level that moves, falls or goes away (a sweeper, a glass
// pane, a tile), with its own mesh. Static scenery goes in the level mesh.
struct Part {
    kke::RigidWorld::BodyId body = kke::RigidWorld::kNoBody;
    std::shared_ptr<kke::DynamicMeshRenderer> mesh;
    glm::mat4 transform{1.0f};  // drawn here when it has no body
    bool visible = true;
    bool translucent = false;   // glass: drawn after everything else, see-through
    bool shadow = true;
    float roughness = 0.6f, metallic = 0.0f;
    bool alive = true;
};

// What the party gives a minigame.
class Arena {
public:
    virtual ~Arena() = default;
    virtual kke::Application& app() = 0;
    virtual kke::RigidWorld& world() = 0;
    virtual std::vector<Bean>& beans() = 0;
    virtual float time() const = 0;           // s since GO (0 during the countdown)
    virtual bool playing() const = 0;         // after GO, before the end
    virtual bool authority() const = 0;       // offline or hosting: this machine decides who's out
    virtual Rng& rng() = 0;                   // seeded per round: the same on every machine (use it only in build/start)
    virtual Rng& botRng() = 0;                // the CPU beans' own (differs between machines)
    virtual uint32_t seed() const = 0;

    // Building the level (in Minigame::build).
    // A static box: in the level's one mesh, and a Jolt body.
    virtual kke::RigidWorld::BodyId staticBox(const glm::vec3& center, const glm::vec3& half, const glm::vec3& color,
                                              const glm::quat& rot = glm::quat(1.0f, 0.0f, 0.0f, 0.0f), float friction = 0.8f) = 0;
    // Only drawn (no body): decoration, far scenery, markings.
    virtual MeshBuilder levelMesh() = 0;
    // A part with a body (motion Kinematic to move it, Dynamic to fall).
    virtual int addPart(const kke::RigidWorld::BodyDesc& desc, std::vector<kke::Vertex> v, std::vector<uint32_t> idx) = 0;
    // A part with no body (a rope, a doll, a flag): moved with its transform.
    virtual int addVisual(std::vector<kke::Vertex> v, std::vector<uint32_t> idx) = 0;
    virtual Part& part(int index) = 0;
    virtual void removePart(int index) = 0;   // its body goes; its mesh after the frames in flight
    virtual int partCount() const = 0;
    // Moves a kinematic part to where it should be now (velocity from the
    // move, so beans standing on it ride along).
    virtual void movePart(int index, const glm::vec3& pos, const glm::quat& rot, float dt) = 0;

    // The rules.
    virtual void finish(Bean& b) = 0;                         // over the line
    virtual void eliminate(Bean& b, const std::string& why) = 0; // out (authority decides online)
    virtual void knock(Bean& b, const glm::vec3& velocity, float stun) = 0; // shoved (with a tumble when stun > 0)
    virtual void respawn(Bean& b) = 0;                        // back to the checkpoint
    virtual void place(Bean& b, const glm::vec3& feet, float yaw) = 0; // put down there, standing
    virtual int finishedCount() const = 0;
    virtual int outCount() const = 0;
    virtual int activeCount() const = 0;                      // still playing

    // Effects.
    virtual void burst(const glm::vec3& at, const glm::vec3& color, int count, float speed = 4.0f) = 0; // confetti, shards
    // Chunks of one colour dropping out of a disc (a floor crumbling): they
    // barely spread and fall like stones, nothing to collide with.
    virtual void rubble(const glm::vec3& at, const glm::vec3& color, int count, float radius) = 0;
    virtual void flame(const glm::vec3& at, float size) = 0;  // one frame of fire at a point
    virtual void sound(const glm::vec3& at, uint32_t material, float intensity) = 0; // kke::AudioMaterialTable ids
    virtual void tone(int earcon, float gain = 0.6f) = 0;
    virtual void flash(const std::string& text, float seconds = 1.6f) = 0; // a line in the middle for everyone
    virtual void status(const std::string& text) = 0;         // the line under the clock this frame ("Green light!")
    // Something every machine must see happen (a pane broke, the bomb
    // passed): Minigame::onEvent runs here now and on every other
    // machine when it arrives. Online, a client's goes through the host.
    virtual void event(int kind, int a, int b) = 0;
};

// How a minigame looks at its beans.
enum class CameraStyle : uint8_t {
    Follow,    // behind each player, the right stick turns it
    Overview,  // one fixed view of the whole arena (Minigame::overview), shared
};

class Minigame {
public:
    virtual ~Minigame() = default;
    virtual const char* id() const = 0;       // "obstacle" (the playlist, logs, KKE_PARTY_GAME)
    virtual const char* title() const = 0;    // "Obstacle Dash"
    virtual const char* goal() const = 0;     // the round card's one line: what to do
    virtual std::string controls() const { return "{move} run  ·  {jump} jump  ·  {dive} dive  ·  {push} push"; }
    virtual const char* mood() const { return "clear_day"; }
    virtual float timeLimit() const { return 90.0f; }
    virtual CameraStyle camera() const { return CameraStyle::Follow; }
    virtual void overview(kke::Camera& cam) const { (void)cam; }
    virtual float followYaw() const { return 0.0f; } // the camera's starting direction (degrees, 0 = -Z)
    // The camera behind each bean: how far to the side (m, over the right
    // shoulder) and how steeply it looks down (degrees), when the bean's
    // own body or a line of others would block the view.
    virtual float cameraSide() const { return 0.0f; }
    virtual float cameraPitch() const { return -16.0f; }
    virtual float killY() const { return -8.0f; }  // below this a bean has fallen off (fell())

    virtual void build(Arena& a) = 0;          // the level
    // Where bean `index` of `count` starts.
    virtual void spawn(Arena& a, int index, int count, glm::vec3& feet, float& yaw) = 0;
    virtual void start(Arena& a) { (void)a; }  // GO
    virtual void update(Arena& a, float dt) = 0; // every frame from the countdown on (moving parts, rules)
    virtual BeanInput bot(Arena& a, Bean& b, float dt) = 0;
    // A bean fell off the level: out, unless the game puts them back.
    virtual void fell(Arena& a, Bean& b) { a.eliminate(b, "fell off"); }
    virtual bool over(Arena& a) const = 0;     // the round ends now (or at the time limit)
    // Time's up: anything to settle (the last standing, the scores).
    virtual void timeUp(Arena& a) { (void)a; }
    // The player's own panel line ("12 taps", "holding the bomb!").
    virtual std::string beanStatus(Arena& a, const Bean& b) const { (void)a; (void)b; return {}; }
    // A push (the push button) is a bump on purpose: how hard (m/s).
    virtual float pushStrength() const { return 6.0f; }
    // Hold dive to charge it (up to twice as hard), let go to dive; two
    // beans diving head-on at the same power clash and mash it out.
    virtual bool chargedDive() const { return false; }
    // A dive started in the air: m/s upwards it gives (0: none, the usual).
    // The glass bridge's save: a dive the moment the glass goes under you.
    virtual float airDiveLift(Arena& a, const Bean& b) const { (void)a; (void)b; return 0.0f; }
    // Beans bump into each other (sumo, tag): how hard (m/s at a full run).
    virtual float bumpStrength() const { return 2.5f; }
    virtual void touched(Arena& a, Bean& by, Bean& other) { (void)a; (void)by; (void)other; } // one bean ran or dived into another
    virtual void onEvent(Arena& a, int kind, int x, int y) { (void)a; (void)kind; (void)x; (void)y; }
    virtual void render(Arena& a, const kke::RenderContext& ctx) { (void)a; (void)ctx; }
};

// Every minigame, in the order the menu lists them (Minigames.cpp).
std::vector<std::unique_ptr<Minigame>> makeMinigames();

} // namespace party
