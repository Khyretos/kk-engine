#pragma once

#include "Minigame.h"
#include "NetParty.h"
#include "People.h"

#include "kke/Module.h"
#include "kke/RigidWorld.h"

#include <RmlUi/Core/DataModelHandle.h>

#include <memory>
#include <string>
#include <vector>

namespace kke {
class AudioModule;
class DynamicMeshRenderer;
class InputModule;
class LobbyModule;
class NetModule;
class RigidBodyModule;
class VoiceModule;
namespace net { struct GameEventMsg; }
} // namespace kke
namespace Rml { class ElementDocument; }

namespace party {

// Party (README.md): a show of minigames for up to four on one screen,
// CPU beans and friends online. It starts in the lobby (kke::LobbyModule):
// every controller that presses A joins, each player dresses their bean
// (colour, pattern, face, hat), player 1 sets the CPU beans, how many
// rounds and which games. Each round is a minigame from the playlist
// (Minigame.h); its places give points (Show.h); after the last round the
// top three stand on the podium.
//
// Headless / demo switches: KKE_PARTY_LOBBY=0 (straight into the show),
// KKE_PARTY_GAME=<id> (only that minigame, e.g. glass_bridge),
// KKE_PARTY_CPUS=<n> (CPU beans without the menu, default 5),
// KKE_PARTY_AUTOPILOT=1 (no menu, and player 1 is a CPU too),
// KKE_PARTY_ROUNDS=<n>, KKE_PARTY_SEED=<n> (the playlist and levels),
// KKE_PARTY_QUIT=<s> (quit after that long, logging how every round went).
class PartyModule : public kke::Module, public Arena {
public:
    PartyModule();
    ~PartyModule() override;
    const char* name() const override { return "Party"; }
    std::vector<kke::ModuleDependency> dependencies() const override;
    void init(kke::Application& app) override;
    void update(const kke::UpdateContext& ctx) override;
    void render(const kke::RenderContext& ctx) override;
    void renderShadow(const kke::ShadowRenderContext& ctx) override;
    void onEvent(const SDL_Event& event) override;
    void shutdown() override;

    // Arena (what the minigames use).
    kke::Application& app() override { return *m_app; }
    kke::RigidWorld& world() override;
    std::vector<Bean>& beans() override { return m_beans; }
    float time() const override { return m_playTime; }
    bool playing() const override { return m_phase == Phase::Play; }
    bool authority() const override;
    Rng& rng() override { return m_rng; }
    Rng& botRng() override { return m_botRng; }
    uint32_t seed() const override { return m_roundSeed; }
    kke::RigidWorld::BodyId staticBox(const glm::vec3& center, const glm::vec3& half, const glm::vec3& color, const glm::quat& rot,
                                      float friction) override;
    MeshBuilder levelMesh() override { return MeshBuilder{ m_levelV, m_levelI }; }
    int addPart(const kke::RigidWorld::BodyDesc& desc, std::vector<kke::Vertex> v, std::vector<uint32_t> idx) override;
    int addVisual(std::vector<kke::Vertex> v, std::vector<uint32_t> idx) override;
    Part& part(int index) override { return m_parts[static_cast<size_t>(index)]; }
    void removePart(int index) override;
    int partCount() const override { return static_cast<int>(m_parts.size()); }
    void movePart(int index, const glm::vec3& pos, const glm::quat& rot, float dt) override;
    void finish(Bean& b) override;
    void eliminate(Bean& b, const std::string& why) override;
    void knock(Bean& b, const glm::vec3& velocity, float stun) override;
    void respawn(Bean& b) override;
    void place(Bean& b, const glm::vec3& feet, float yaw) override;
    int finishedCount() const override;
    int outCount() const override;
    int activeCount() const override;
    void burst(const glm::vec3& at, const glm::vec3& color, int count, float speed) override;
    void flame(const glm::vec3& at, float size) override;
    void sound(const glm::vec3& at, uint32_t material, float intensity) override;
    void tone(int earcon, float gain) override;
    void flash(const std::string& text, float seconds) override;
    void status(const std::string& text) override { m_status = text; }
    void event(int kind, int a, int b) override;

private:
    enum class Phase : uint8_t { Lobby, Intro, Countdown, Play, RoundOver, Results, Podium };

    // The roster (Lobby.cpp): who plays, from the menu (and online).
    struct Entry {
        int seat = -1, difficulty = 1;
        std::string name;
        BeanLook look;
        int netId = -1;
        bool remote = false;
    };
    void setupLobby();
    std::vector<Entry> wantedRoster() const;
    void buildBeans(const std::vector<Entry>& roster);
    void removeBean(Bean& b);
    void applyLooks();
    void updateLobby(float dt);
    void startParty();       // from the menu: a new show
    void backToLobby();
    void buildStage();       // the lobby's (and podium's) floor
    std::vector<std::string> chosenGames() const;
    int chosenRounds() const;

    // Rounds (PartyModule.cpp).
    void loadRound();        // the show's current minigame: level, beans at the start
    void buildRound(const std::string& game, uint32_t seed);
    void clearLevel();
    void startPlay();        // GO
    void endRound();
    void nextRound();
    void showPodium();
    std::vector<RoundResult> roundResults() const;

    // Beans (Beans.cpp): controls, the controller, bumping, drawing.
    void defineControls();
    BeanInput readPlayer(Bean& b, float dt);
    void moveBean(Bean& b, float dt);
    void bumpBeans(float dt);
    void animateBean(Bean& b, float dt);
    void drawBean(const Bean& b, const kke::RenderContext* ctx, const kke::ShadowRenderContext* shadow);
    glm::mat4 bodyMatrix(const Bean& b); // where it's drawn: at its feet, turned, leaning, tumbling
    void ensureMesh(Bean& b);
    void park(Bean& b);      // out of the way (out, or a seat nobody plays)
    void updateCameras(float dt);
    kke::Camera& cameraOf(Bean& b);
    void checkFalls();

    // Particles (Beans.cpp): confetti, glass bits, flames. One mesh, rebuilt each frame.
    struct Particle {
        glm::vec3 pos{0.0f}, vel{0.0f}, color{1.0f}, color2{1.0f};
        float size = 0.05f, life = 1.0f, age = 0.0f, gravity = 9.8f, spin = 0.0f;
    };
    std::vector<Particle> m_particles;
    std::unique_ptr<kke::DynamicMeshRenderer> m_particleMesh;
    void updateParticles(float dt);

    // HUD (Hud.cpp, ui/party_hud.rml).
    void buildHud();
    void updateHud(float dt);

    // Online (Net.cpp).
    void setupNet();
    void updateNet(float dt);
    void sendNet();
    void onNetEvent(const kke::net::GameEventMsg& e);
    void syncNetPlayers();
    bool netClient() const;
    bool netHost() const;
    bool online() const { return netClient() || netHost(); }
    std::vector<Entry> onlineRoster() const;
    void sendRound();                      // host: the round everyone plays, and the phase it's in
    void applyRound(const netparty::Round& r);
    void applyPhase(Phase phase);
    void sendResult(uint8_t kind, const Bean& b);
    Bean* beanOfNet(int netId);
    std::string netStatus() const;
    std::string m_netName;
    float m_netTime = 0.0f, m_netSearchAt = 0.0f;
    bool m_wasOnline = false;
    uint32_t m_netRound = 0;               // +1 each round the host builds (Round::round)
    uint32_t m_sentRound = 0xffffffffu;    // host: the last Round sent
    std::string m_lastNetStatus;
    bool m_netApplying = false;            // applying another machine's event: don't send it back

    kke::Application* m_app = nullptr;
    kke::RigidBodyModule* m_rigid = nullptr;
    kke::InputModule* m_input = nullptr;
    kke::LobbyModule* m_lobby = nullptr;
    kke::NetModule* m_net = nullptr;
    kke::AudioModule* m_audio = nullptr;
    kke::VoiceModule* m_voice = nullptr;

    std::vector<std::unique_ptr<Minigame>> m_games;
    Minigame* m_game = nullptr;
    Show m_show;
    Phase m_phase = Phase::Lobby;
    float m_phaseTime = 0.0f;              // s in this phase
    float m_playTime = 0.0f;               // s since GO
    uint32_t m_seed = 1, m_roundSeed = 1;
    Rng m_rng, m_botRng;
    People m_people;
    std::vector<int> m_bodyChoices; // the menu's Body row: choice -> BeanLook::body
    std::vector<Bean> m_beans;
    std::vector<int> m_roundPoints;        // what each bean got this round (the results screen)
    int m_finishOrder = 0, m_outOrder = 0;
    std::string m_flash, m_status;
    float m_flashTime = 0.0f;
    bool m_captured = false;
    bool m_autopilot = false;
    int m_defaultCpus = 5;
    float m_quitAfter = -1.0f, m_startAfter = -1.0f, m_clock = 0.0f;
    std::string m_onlyGame;                // KKE_PARTY_GAME
    int m_forcedRounds = 0;                // KKE_PARTY_ROUNDS
    bool m_rosterChanged = false;
    std::string m_mood;

    // The level: static boxes in one mesh; parts that move or break.
    std::vector<kke::Vertex> m_levelV;
    std::vector<uint32_t> m_levelI;
    std::unique_ptr<kke::DynamicMeshRenderer> m_level;
    std::vector<kke::RigidWorld::BodyId> m_statics;
    std::vector<Part> m_parts;
    std::vector<kke::RigidWorld::BodyId> m_stage;
    std::unique_ptr<kke::DynamicMeshRenderer> m_stageMesh;
    kke::Camera m_overview;                // Overview games, the lobby and the podium

    // HUD.
    struct PlayerHud {
        std::string name, accent, status, points, x, y, w;
        bool out = false, won = false, talking = false;
    };
    struct RowHud {
        std::string place, name, accent, got, total, note;
    };
    struct Hud {
        std::vector<PlayerHud> players;
        std::vector<RowHud> rows;          // results and podium
        std::string banner, sub, hint, clock, status, round, title, goal, controls, talk;
        bool show = false, card = false, table = false;
    };
    Hud m_hud;
    Rml::DataModelHandle m_hudModel;
    Rml::ElementDocument* m_hudDoc = nullptr;
};

} // namespace party
