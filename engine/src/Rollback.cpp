#include "kke/Rollback.h"

#include <algorithm>
#include <climits>
#include <cstring>

namespace kke {

uint64_t fnv1a(const Bytes& bytes) {
    uint64_t h = 1469598103934665603ull;
    for (uint8_t b : bytes) {
        h ^= b;
        h *= 1099511628211ull;
    }
    return h;
}

namespace {

constexpr uint8_t kMagic = 'R', kVersion = 1;
constexpr size_t kMaxCount = 256;

struct Out {
    Bytes b;
    void u8(uint8_t v) { b.push_back(v); }
    void u16(uint16_t v) {
        b.push_back(static_cast<uint8_t>(v));
        b.push_back(static_cast<uint8_t>(v >> 8));
    }
    void i32(int32_t v) {
        const auto u = static_cast<uint32_t>(v);
        for (int i = 0; i < 4; ++i) b.push_back(static_cast<uint8_t>(u >> (8 * i)));
    }
    void u64(uint64_t v) {
        for (int i = 0; i < 8; ++i) b.push_back(static_cast<uint8_t>(v >> (8 * i)));
    }
};

struct In {
    const uint8_t* p;
    size_t n, pos = 0;
    bool bad = false;
    bool has(size_t k) const { return !bad && n - pos >= k; }
    uint8_t u8() {
        if (!has(1)) {
            bad = true;
            return 0;
        }
        return p[pos++];
    }
    uint16_t u16() {
        const uint16_t lo = u8();
        return static_cast<uint16_t>(lo | (u8() << 8));
    }
    int32_t i32() {
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i) v |= static_cast<uint32_t>(u8()) << (8 * i);
        return static_cast<int32_t>(v);
    }
    uint64_t u64() {
        uint64_t v = 0;
        for (int i = 0; i < 8; ++i) v |= static_cast<uint64_t>(u8()) << (8 * i);
        return v;
    }
};

} // namespace

RollbackSession::RollbackSession(const RollbackConfig& c) : m_config(c), m_firstIncorrect(INT_MAX) {
    m_config.players = std::clamp(c.players, 1, 8);
    m_config.inputSize = std::clamp(c.inputSize, 1, 64);
    m_config.inputDelay = std::clamp(c.inputDelay, 0, 16);
    m_config.maxPrediction = std::clamp(c.maxPrediction, 1, 32);
    m_config.checksumInterval = std::max(0, c.checksumInterval);
    m_players.resize(static_cast<size_t>(m_config.players));
}

bool RollbackSession::setLocalPlayer(int player) {
    if (m_local >= 0 || player < 0 || player >= m_config.players) return false;
    m_local = player;
    // the frames before the delay has passed: nothing pressed
    for (int f = 0; f < m_config.inputDelay; ++f) confirm(player, f, Bytes(static_cast<size_t>(m_config.inputSize), 0));
    return true;
}

bool RollbackSession::addLocalInput(const Bytes& input) {
    if (m_local < 0 || input.size() != static_cast<size_t>(m_config.inputSize)) return false;
    m_pending = input;
    m_hasPending = true;
    return true;
}

int RollbackSession::confirmedFrame() const {
    int c = INT_MAX;
    for (const Player& p : m_players)
        if (!p.disconnected) c = std::min(c, p.contiguous);
    return c == INT_MAX ? m_frame - 1 : c;
}

Bytes RollbackSession::inputOf(int player, int frame, bool& predicted) const {
    const Player& p = m_players[static_cast<size_t>(player)];
    if (p.disconnected && frame >= p.disconnectFrame) {
        predicted = false;
        return Bytes(static_cast<size_t>(m_config.inputSize), 0);
    }
    const auto it = p.confirmed.find(frame);
    if (it != p.confirmed.end()) {
        predicted = false;
        return it->second;
    }
    predicted = true;
    const auto after = p.confirmed.lower_bound(frame); // a guess: what they did last
    if (after == p.confirmed.begin()) return Bytes(static_cast<size_t>(m_config.inputSize), 0);
    return std::prev(after)->second;
}

void RollbackSession::confirm(int player, int frame, const Bytes& input) {
    Player& p = m_players[static_cast<size_t>(player)];
    if (p.confirmed.count(frame)) return;
    if (p.disconnected && frame >= p.disconnectFrame) return;
    p.confirmed[frame] = input;
    while (p.confirmed.count(p.contiguous + 1)) ++p.contiguous;
    if (frame < m_frame) {
        const auto u = p.used.find(frame);
        if (u != p.used.end() && u->second.predicted && u->second.input != input) {
            m_firstIncorrect = std::min(m_firstIncorrect, frame);
            ++m_stats.mispredictions;
        }
    }
}

bool RollbackSession::inputFor(int player, int frame, Bytes& out, bool* predicted) const {
    if (player < 0 || player >= m_config.players) return false;
    const Player& p = m_players[static_cast<size_t>(player)];
    const auto u = p.used.find(frame);
    if (u == p.used.end()) return false;
    out = u->second.input;
    if (predicted) *predicted = u->second.predicted;
    return true;
}

bool RollbackSession::disconnectPlayer(int player, int fromFrame) {
    if (player < 0 || player >= m_config.players || player == m_local) return false;
    Player& p = m_players[static_cast<size_t>(player)];
    if (p.disconnected) return false;
    p.disconnected = true;
    p.disconnectFrame = fromFrame >= 0 ? fromFrame : p.contiguous + 1;
    // inputs from their disconnect on are zeros, whatever arrived: frames the simulation already ran with a different input are re-run
    for (auto it = p.confirmed.lower_bound(p.disconnectFrame); it != p.confirmed.end();) it = p.confirmed.erase(it);
    for (const auto& [f, u] : p.used) {
        if (f < p.disconnectFrame) continue;
        for (uint8_t b : u.input)
            if (b != 0) {
                m_firstIncorrect = std::min(m_firstIncorrect, f);
                break;
            }
    }
    RollbackEvent e;
    e.kind = RollbackEvent::Kind::Disconnected;
    e.player = player;
    e.frame = p.disconnectFrame;
    m_events.push_back(e);
    return true;
}

bool RollbackSession::disconnected(int player) const {
    return player >= 0 && player < m_config.players && m_players[static_cast<size_t>(player)].disconnected;
}

void RollbackSession::simulate(int frame, std::vector<RollbackRequest>& out) {
    RollbackRequest adv;
    adv.kind = RollbackRequest::Kind::Advance;
    adv.frame = frame;
    for (int p = 0; p < m_config.players; ++p) {
        bool predicted = false;
        Bytes in = inputOf(p, frame, predicted);
        m_players[static_cast<size_t>(p)].used[frame] = { in, predicted };
        if (predicted && p != m_local) ++m_stats.predictedInputs;
        adv.inputs.push_back(std::move(in));
        adv.predicted.push_back(predicted);
    }
    out.push_back(std::move(adv));
}

void RollbackSession::rollback(std::vector<RollbackRequest>& out) {
    if (m_firstIncorrect >= m_frame) {
        m_firstIncorrect = INT_MAX;
        return;
    }
    const int from = m_firstIncorrect;
    RollbackRequest load;
    load.kind = RollbackRequest::Kind::Load;
    load.frame = from;
    out.push_back(load);
    for (int f = from; f < m_frame; ++f) {
        if (f != from) { // the state at `from` is the one just loaded
            RollbackRequest save;
            save.kind = RollbackRequest::Kind::Save;
            save.frame = f;
            out.push_back(save);
        }
        simulate(f, out);
    }
    ++m_stats.rollbacks;
    m_stats.framesResimulated += static_cast<uint64_t>(m_frame - from);
    m_stats.maxRollbackDepth = std::max(m_stats.maxRollbackDepth, m_frame - from);
    RollbackEvent e;
    e.kind = RollbackEvent::Kind::Rollback;
    e.frame = from;
    e.other = m_frame - from;
    m_events.push_back(e);
    m_firstIncorrect = INT_MAX;
}

void RollbackSession::resolve(std::vector<RollbackRequest>& out) { rollback(out); }

RollbackSession::Status RollbackSession::advance(std::vector<RollbackRequest>& out) {
    if (m_local < 0) return Status::NoLocalPlayer;
    checkChecksums(); // the requests of the last tick have been carried out: the checksums are current
    if (m_frame - (confirmedFrame() + 1) >= m_config.maxPrediction) {
        ++m_stats.stalls;
        rollback(out); // waiting is no reason to leave a mistake in
        return Status::PredictionLimit;
    }
    // the local input of this tick is what the simulation will see inputDelay frames from now
    Bytes in = m_hasPending ? m_pending : Bytes(static_cast<size_t>(m_config.inputSize), 0);
    if (!m_hasPending) {
        const Player& me = m_players[static_cast<size_t>(m_local)];
        if (!me.confirmed.empty()) in = me.confirmed.rbegin()->second; // nothing new was added: the last again
    }
    confirm(m_local, m_frame + m_config.inputDelay, in);
    m_hasPending = false;

    rollback(out);
    RollbackRequest save;
    save.kind = RollbackRequest::Kind::Save;
    save.frame = m_frame;
    out.push_back(save);
    simulate(m_frame, out);
    ++m_frame;
    ++m_stats.framesAdvanced;

    // forget what can no longer matter
    const int keepUsed = m_frame - m_config.maxPrediction - 8;
    // Inputs are kept back to the newest frame everyone's input is known for (a rollback re-runs frames
    // after it with every player's input, whichever player is the one that lags), and our own until
    // every other player has them as well (they are sent again until then).
    int ackedByAll = INT_MAX;
    for (int r = 0; r < m_config.players; ++r)
        if (r != m_local && !m_players[static_cast<size_t>(r)].disconnected) ackedByAll = std::min(ackedByAll, m_players[static_cast<size_t>(r)].ackedUs);
    // (never a frame the simulation has not reached: with an input delay the confirmed frames run ahead of it)
    const int settled = std::min(confirmedFrame(), m_frame);
    for (int i = 0; i < m_config.players; ++i) {
        Player& p = m_players[static_cast<size_t>(i)];
        p.used.erase(p.used.begin(), p.used.lower_bound(keepUsed));
        int keepConfirmed = settled;
        if (i == m_local && ackedByAll != INT_MAX) keepConfirmed = std::min(keepConfirmed, ackedByAll);
        p.confirmed.erase(p.confirmed.begin(), p.confirmed.lower_bound(keepConfirmed));
    }
    return Status::Ok;
}

void RollbackSession::reportChecksum(int frame, uint64_t checksum) {
    m_checksums[frame] = checksum;
    while (m_checksums.size() > 1024) m_checksums.erase(m_checksums.begin());
}

int RollbackSession::localAdvantage(int remote) const { return m_frame - m_players[static_cast<size_t>(remote)].remoteFrame; }

int RollbackSession::recommendedWait() const {
    int wait = 0;
    for (int r = 0; r < m_config.players; ++r) {
        if (r == m_local || m_players[static_cast<size_t>(r)].disconnected) continue;
        wait = std::max(wait, (localAdvantage(r) - m_players[static_cast<size_t>(r)].remoteAdvantage) / 2);
    }
    return std::clamp(wait, 0, 8);
}

void RollbackSession::checkChecksums() {
    if (m_config.checksumInterval <= 0 || m_local < 0) return;
    const int finalLimit = std::min(confirmedFrame() + 1, m_frame);
    for (int r = 0; r < m_config.players; ++r) {
        if (r == m_local) continue;
        Player& p = m_players[static_cast<size_t>(r)];
        for (const auto& [f, theirs] : p.checksums) {
            if (f > finalLimit || m_firstIncorrect <= f) continue; // our state at f is not settled yet
            const auto key = std::make_pair(r, f);
            if (m_reportedDesync.count(key)) continue;
            const auto mine = m_checksums.find(f);
            if (mine == m_checksums.end()) continue;
            m_reportedDesync[key] = mine->second != theirs;
            if (mine->second != theirs) {
                RollbackEvent e;
                e.kind = RollbackEvent::Kind::Desync;
                e.frame = f;
                e.player = r;
                e.local = mine->second;
                e.remote = theirs;
                m_events.push_back(e);
            }
        }
        while (p.checksums.size() > 64) p.checksums.erase(p.checksums.begin());
    }
    while (m_reportedDesync.size() > 4096) m_reportedDesync.erase(m_reportedDesync.begin());
}

Bytes RollbackSession::makePacket() {
    if (m_local < 0) return {};
    const Player& me = m_players[static_cast<size_t>(m_local)];
    // from the oldest of our inputs some other player has not confirmed
    int first = INT_MAX;
    int advantage = 0;
    for (int r = 0; r < m_config.players; ++r) {
        if (r == m_local || m_players[static_cast<size_t>(r)].disconnected) continue;
        first = std::min(first, m_players[static_cast<size_t>(r)].ackedUs + 1);
        advantage = std::max(advantage, localAdvantage(r));
    }
    const int last = me.confirmed.empty() ? -1 : me.confirmed.rbegin()->first;
    if (first == INT_MAX) first = last + 1;
    first = std::max(first, last - static_cast<int>(kMaxCount) + 1);
    first = std::max(first, me.confirmed.empty() ? 0 : me.confirmed.begin()->first);
    const int count = std::max(0, last - first + 1);

    Out o;
    o.u8(kMagic);
    o.u8(kVersion);
    o.u8(static_cast<uint8_t>(m_local));
    o.u8(0);
    o.i32(m_frame);
    o.i32(advantage);
    o.u8(static_cast<uint8_t>(m_config.players));
    for (const Player& p : m_players) o.i32(p.contiguous);
    o.i32(first);
    o.u16(static_cast<uint16_t>(count));
    for (int f = first; f < first + count; ++f) {
        const auto it = me.confirmed.find(f);
        const Bytes zeros(static_cast<size_t>(m_config.inputSize), 0);
        const Bytes& in = it != me.confirmed.end() ? it->second : zeros;
        o.b.insert(o.b.end(), in.begin(), in.end());
    }
    // the newest checksum of a state that is final: a lost packet is made up for by the next
    int checksumFrame = -1;
    uint64_t checksum = 0;
    if (m_config.checksumInterval > 0) {
        const int limit = std::min(confirmedFrame() + 1, m_frame);
        for (auto it = m_checksums.rbegin(); it != m_checksums.rend(); ++it) {
            if (it->first <= limit && it->first % m_config.checksumInterval == 0 && it->first < m_firstIncorrect) {
                checksumFrame = it->first;
                checksum = it->second;
                break;
            }
        }
    }
    o.i32(checksumFrame);
    o.u64(checksum);
    ++m_stats.packetsSent;
    m_stats.bytesSent += o.b.size();
    return std::move(o.b);
}

bool RollbackSession::receive(const uint8_t* data, size_t size) {
    auto refuse = [&]() {
        ++m_stats.packetsRefused;
        return false;
    };
    if (m_local < 0 || !data || size < 8 || size > kMaxPacket) return refuse();
    In in{ data, size };
    if (in.u8() != kMagic || in.u8() != kVersion) return refuse();
    const int sender = in.u8();
    in.u8();
    const int frame = in.i32();
    const int advantage = in.i32();
    const int players = in.u8();
    if (in.bad || sender >= m_config.players || sender == m_local || players != m_config.players || frame < 0) return refuse();
    std::vector<int> acks(static_cast<size_t>(players));
    for (int& a : acks) a = in.i32();
    const int start = in.i32();
    const int count = in.u16();
    if (in.bad || start < 0 || count > static_cast<int>(kMaxCount)) return refuse();
    if (static_cast<int64_t>(start) + count > static_cast<int64_t>(m_frame) + m_config.maxPrediction + 1024) return refuse();
    const size_t need = static_cast<size_t>(count) * static_cast<size_t>(m_config.inputSize);
    if (!in.has(need + 12) || in.n - in.pos != need + 12) return refuse();
    std::vector<Bytes> inputs;
    for (int i = 0; i < count; ++i) {
        Bytes b(data + in.pos, data + in.pos + m_config.inputSize);
        in.pos += static_cast<size_t>(m_config.inputSize);
        inputs.push_back(std::move(b));
    }
    const int checksumFrame = in.i32();
    const uint64_t checksum = in.u64();
    if (in.bad) return refuse();
    for (int a : acks)
        if (a < -1) return refuse();

    for (int i = 0; i < count; ++i) confirm(sender, start + i, inputs[static_cast<size_t>(i)]);
    Player& p = m_players[static_cast<size_t>(sender)];
    p.remoteFrame = std::max(p.remoteFrame, frame);
    p.remoteAdvantage = std::clamp(advantage, -1000, 1000);
    // an acknowledgement of an input we have not sent is a lie (or noise): never past our newest
    const Player& me = m_players[static_cast<size_t>(m_local)];
    const int newest = me.confirmed.empty() ? -1 : me.confirmed.rbegin()->first;
    p.ackedUs = std::max(p.ackedUs, std::min(acks[static_cast<size_t>(m_local)], newest));
    if (checksumFrame >= 0) p.checksums[checksumFrame] = checksum;
    ++m_stats.packetsReceived;
    checkChecksums();
    return true;
}

std::vector<RollbackEvent> RollbackSession::takeEvents() {
    std::vector<RollbackEvent> out;
    out.swap(m_events);
    return out;
}

// ---- the driver ------------------------------------------------------------------------

RollbackDriver::RollbackDriver(RollbackSession& session, RollbackGame game) : m_session(session), m_game(std::move(game)) {
    if (!m_game.checksum) m_game.checksum = fnv1a;
}

void RollbackDriver::execute(const std::vector<RollbackRequest>& requests) {
    for (const RollbackRequest& r : requests) {
        switch (r.kind) {
        case RollbackRequest::Kind::Save: {
            Bytes state = m_game.save();
            m_session.reportChecksum(r.frame, m_game.checksum(state));
            m_snapshots[r.frame] = std::move(state);
            while (m_snapshots.size() > static_cast<size_t>(m_session.snapshotSlots()) + 2) m_snapshots.erase(m_snapshots.begin());
            break;
        }
        case RollbackRequest::Kind::Load: {
            const auto it = m_snapshots.find(r.frame);
            if (it != m_snapshots.end()) m_game.load(it->second);
            break;
        }
        case RollbackRequest::Kind::Advance: m_game.advance(r.inputs); break;
        }
    }
}

RollbackSession::Status RollbackDriver::tick(const Bytes& localInput) {
    m_session.addLocalInput(localInput);
    std::vector<RollbackRequest> requests;
    const RollbackSession::Status s = m_session.advance(requests);
    execute(requests);
    return s;
}

void RollbackDriver::settle() {
    std::vector<RollbackRequest> requests;
    m_session.resolve(requests);
    execute(requests);
}

// ---- the sync test ----------------------------------------------------------------------

bool rollbackSyncTest(const RollbackGame& game, int players, int frames, int depth, const std::function<std::vector<Bytes>(int frame)>& inputs,
                      std::string* report) {
    (void)players;
    depth = std::max(depth, 1);
    std::map<int, Bytes> states; // the state at the start of each frame
    auto differs = [&](const Bytes& a, const Bytes& b, int frame, const char* what) {
        if (a == b) return false;
        size_t at = 0;
        while (at < a.size() && at < b.size() && a[at] == b[at]) ++at;
        if (report)
            *report = std::string("the state at frame ") + std::to_string(frame) + " " + what + ": " +
                      (a.size() != b.size() ? "its size changed (" + std::to_string(a.size()) + " vs " + std::to_string(b.size()) + " bytes)"
                                            : "byte " + std::to_string(at) + " differs");
        return true;
    };
    for (int f = 0; f < frames; ++f) {
        states[f] = game.save();
        game.advance(inputs(f));
        if (f + 1 < depth) continue;
        const Bytes after = game.save(); // the state at f + 1
        const int back = f + 1 - depth;
        game.load(states[back]);
        for (int g = back; g <= f; ++g) {
            if (g > back && differs(game.save(), states[g], g, "differs after loading and re-running")) return false;
            game.advance(inputs(g));
        }
        if (differs(game.save(), after, f + 1, "differs after loading and re-running")) return false;
        while (states.size() > static_cast<size_t>(depth) + 2) states.erase(states.begin());
    }
    return true;
}

} // namespace kke
