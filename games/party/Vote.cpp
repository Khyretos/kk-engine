// Picking the next game by vote (README.md "Modes"): with Next game set
// to Vote, before each round everyone stands on the stage and three games
// come up; each player points at one with the stick (left, right) and
// votes with jump (they can change their mind until it closes), the CPU
// beans vote too. The most votes wins; a tie is settled by a coin. Online
// the host runs it: players send their ballots, the host sends the vote
// as it stands, then the round.

#include "PartyModule.h"

#include "kke/ImpactSynth.h"
#include "kke/Log.h"
#include "kke/modules/InputModule.h"
#include "kke/modules/NetModule.h"
#include "kke/modules/RigidBodyModule.h"

#include <algorithm>
#include <cmath>

namespace party {

namespace {
constexpr int kChoices = 3;
constexpr float kVoteTime = 12.0f;  // s to vote
constexpr float kEveryoneIn = 1.2f; // s after the last vote before it closes early
constexpr float kShowWinner = 2.2f; // s the winner stays up before the round
} // namespace

void PartyModule::standOnStage() {
    const glm::vec3 at(0.0f, 0.0f, 400.0f);
    const int n = static_cast<int>(m_beans.size());
    for (int i = 0; i < n; ++i) {
        Bean& b = m_beans[static_cast<size_t>(i)];
        b.active = true;
        b.hidden = b.out = b.finished = false;
        b.stun = b.dive = 0.0f;
        b.push = b.velocity = glm::vec3(0.0f);
        // Two rows when there are many.
        const int row = n > 6 && i >= (n + 1) / 2 ? 1 : 0;
        const int inRow = row == 0 ? std::min(n, n > 6 ? (n + 1) / 2 : n) : n - (n + 1) / 2;
        const int k = row == 0 ? i : i - (n + 1) / 2;
        const glm::vec3 feet = at + glm::vec3((static_cast<float>(k) - static_cast<float>(inRow - 1) * 0.5f) * 1.5f, 0.0f, row == 0 ? -1.5f : 0.3f);
        if (!b.remote) place(b, feet, 180.0f); // facing the camera
    }
}

// The host: the choices for this round, everyone on the stage.
void PartyModule::openVote() {
    clearLevel();
    m_game = nullptr;
    m_vote = VoteState{};
    m_vote.index = static_cast<uint8_t>(std::clamp(m_show.round, 0, 255));
    m_vote.games = m_show.candidates(kChoices, m_seed * 31u + static_cast<uint32_t>(m_show.round) * 977u + 5u);
    m_vote.left = kVoteTime;
    const size_t n = m_beans.size();
    m_vote.votes.assign(n, -1);
    m_vote.cursor.assign(n, 1); // the middle one
    m_vote.lastX.assign(n, 0.0f);
    m_vote.botAt.assign(n, 0.0f);
    for (float& t : m_vote.botAt) t = m_botRng.range(1.5f, 6.0f);
    m_phase = Phase::Vote;
    m_phaseTime = 0.0f;
    if (m_mood != "sunset") m_app->setMood(m_mood = "sunset");
    standOnStage();
    tone(static_cast<int>(kke::Earcon::Focus), 0.8f);
    kke::log::get(name())->info("vote for round {} of {}: {}", m_show.round + 1, m_show.rounds, fmt::join(m_vote.games, ", "));
    if (netHost()) {
        sendRound(); // who plays, the points so far
        sendVote();
    }
}

void PartyModule::castVote(Bean& b, int choice) {
    const size_t i = static_cast<size_t>(b.index);
    if (i >= m_vote.votes.size() || choice < 0 || choice >= static_cast<int>(m_vote.games.size()) || m_vote.winner >= 0) return;
    if (m_vote.votes[i] == choice) return;
    m_vote.votes[i] = choice;
    if (b.seat >= 0 && !b.remote) tone(static_cast<int>(kke::Earcon::ToggleOn), 0.6f);
    b.input.jump = true; // a little hop: I've voted
    if (netClient()) {
        if (b.netId >= 0 && m_net && m_net->connected())
            m_net->sendEvent(netparty::kEventBallot, netparty::encode(netparty::Ballot{ m_vote.index, static_cast<uint8_t>(b.netId), static_cast<int8_t>(choice) }));
    } else if (netHost()) {
        sendVote();
    }
}

void PartyModule::decideVote() {
    m_vote.winner = tally(m_vote.votes, static_cast<int>(m_vote.games.size()), m_seed * 131u + m_vote.index);
    m_vote.decidedAt = m_phaseTime;
    if (m_vote.winner < 0) return;
    const std::string& game = m_vote.games[static_cast<size_t>(m_vote.winner)];
    m_show.choose(game);
    std::string title = game;
    for (const auto& g : m_games)
        if (g->id() == game) title = g->title();
    flash(title + "!", kShowWinner);
    tone(static_cast<int>(kke::Earcon::Activate), 0.8f);
    kke::log::get(name())->info("the vote picks {}", game);
    if (netHost()) sendVote();
}

void PartyModule::updateVote(float dt) {
    const bool host = !netClient();
    m_vote.left = std::max(0.0f, m_vote.left - dt);
    for (Bean& b : m_beans) {
        const size_t i = static_cast<size_t>(b.index);
        if (b.remote || i >= m_vote.votes.size()) continue;
        b.input = BeanInput{};
        if (m_vote.winner >= 0) continue;
        if (b.bot) {
            // A CPU makes up its mind after a moment.
            if (m_vote.votes[i] < 0 && m_phaseTime >= m_vote.botAt[i] && !m_vote.games.empty()) {
                m_vote.cursor[i] = m_botRng.below(static_cast<int>(m_vote.games.size()));
                castVote(b, m_vote.cursor[i]);
            }
            continue;
        }
        kke::InputMap& in = m_input->map(b.player);
        const float x = in.axis2("move").x;
        const int count = static_cast<int>(m_vote.games.size());
        const int was = m_vote.cursor[i];
        if (x > 0.5f && m_vote.lastX[i] <= 0.5f) ++m_vote.cursor[i];
        if (x < -0.5f && m_vote.lastX[i] >= -0.5f) --m_vote.cursor[i];
        m_vote.lastX[i] = x;
        m_vote.cursor[i] = std::clamp(m_vote.cursor[i], 0, std::max(0, count - 1));
        if (m_vote.cursor[i] != was) tone(static_cast<int>(kke::Earcon::Tick), 0.35f);
        if (in.pressed("jump")) castVote(b, m_vote.cursor[i]);
    }
    if (!host) return;
    if (m_vote.winner < 0) {
        // Everyone's in: close a moment later (a last change of mind).
        const bool all = std::none_of(m_vote.votes.begin(), m_vote.votes.end(), [](int v) { return v < 0; });
        if (all && m_vote.left > kEveryoneIn) m_vote.left = kEveryoneIn;
        if (m_vote.left <= 0.0f) decideVote();
        // Online, the clock now and then (the clients count down on their own between).
        const int before = static_cast<int>(std::ceil(m_vote.left + dt)), now = static_cast<int>(std::ceil(m_vote.left));
        if (netHost() && m_vote.winner < 0 && before != now && now % 3 == 0) sendVote();
    } else if (m_phaseTime - m_vote.decidedAt > kShowWinner) {
        loadRound();
    }
}

// ---- Online ------------------------------------------------------------

void PartyModule::sendVote() {
    if (!m_net || !m_net->connected()) return;
    netparty::Vote v;
    v.index = m_vote.index;
    v.games = m_vote.games;
    v.secondsLeft = static_cast<uint8_t>(std::clamp(static_cast<int>(std::ceil(m_vote.left)), 0, 60));
    v.winner = static_cast<int8_t>(m_vote.winner);
    for (const Bean& b : m_beans)
        if (b.netId >= 0 && static_cast<size_t>(b.index) < m_vote.votes.size())
            v.ballots.push_back({ v.index, static_cast<uint8_t>(b.netId), static_cast<int8_t>(m_vote.votes[static_cast<size_t>(b.index)]) });
    m_net->sendEvent(netparty::kEventVote, netparty::encode(v));
}

// A client: the vote as the host has it (the first one opens it here).
void PartyModule::applyVote(const netparty::Vote& v) {
    if (m_phase != Phase::Vote || m_vote.index != v.index || m_vote.games != v.games) {
        clearLevel();
        m_game = nullptr;
        m_vote = VoteState{};
        m_vote.index = v.index;
        m_vote.games = v.games;
        const size_t n = m_beans.size();
        m_vote.votes.assign(n, -1);
        m_vote.cursor.assign(n, 1);
        m_vote.lastX.assign(n, 0.0f);
        m_vote.botAt.assign(n, 0.0f); // the host votes for its CPUs (remote here); ours, on autopilot, vote as it would
        for (float& t : m_vote.botAt) t = m_botRng.range(1.5f, 6.0f);
        m_show.round = v.index;
        m_phase = Phase::Vote;
        m_phaseTime = 0.0f;
        if (m_mood != "sunset") m_app->setMood(m_mood = "sunset");
        standOnStage();
        tone(static_cast<int>(kke::Earcon::Focus), 0.8f);
    }
    m_vote.left = static_cast<float>(v.secondsLeft);
    for (const netparty::Ballot& ballot : v.ballots)
        if (Bean* b = beanOfNet(ballot.player); b && static_cast<size_t>(b->index) < m_vote.votes.size() && (b->remote || ballot.choice >= 0))
            m_vote.votes[static_cast<size_t>(b->index)] = ballot.choice;
    if (v.winner >= 0 && m_vote.winner < 0 && v.winner < static_cast<int>(m_vote.games.size())) {
        m_vote.winner = v.winner;
        m_vote.decidedAt = m_phaseTime;
        std::string title = m_vote.games[static_cast<size_t>(v.winner)];
        for (const auto& g : m_games)
            if (g->id() == title) title = g->title();
        flash(title + "!", kShowWinner);
        tone(static_cast<int>(kke::Earcon::Activate), 0.8f);
    }
}

// The host: a player's pick.
void PartyModule::applyBallot(const netparty::Ballot& ballot) {
    if (m_phase != Phase::Vote || ballot.index != m_vote.index || m_vote.winner >= 0) return;
    Bean* b = beanOfNet(ballot.player);
    if (!b || !b->remote || static_cast<size_t>(b->index) >= m_vote.votes.size()) return;
    if (ballot.choice < 0 || ballot.choice >= static_cast<int>(m_vote.games.size())) return;
    m_vote.votes[static_cast<size_t>(b->index)] = ballot.choice;
    sendVote();
}

} // namespace party
