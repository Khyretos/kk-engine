#include "kke/server/Leaderboard.h"

#include "kke/net/BitStream.h"
#include "kke/server/ServerAccess.h"
#include "kke/server/ServerFiles.h"

#include <nlohmann/json.hpp>

#include <algorithm>

namespace kke::server {

bool Leaderboard::validBoardName(const std::string& board) {
    if (board.empty() || board.size() > kMaxBoardName) return false;
    return std::all_of(board.begin(), board.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.'; });
}

void Leaderboard::sort(Board& b) const {
    // Ties: whoever got there first stays ahead.
    std::stable_sort(b.entries.begin(), b.entries.end(), [&](const Entry& x, const Entry& y) {
        if (x.score != y.score) return better(b, x.score, y.score);
        return x.time < y.time;
    });
}

bool Leaderboard::submit(const std::string& board, const std::string& player, int32_t score, uint64_t time) {
    if (!validBoardName(board) || player.empty() || player.size() > kMaxPlayerName) return false;
    auto it = m_boards.find(board);
    if (it == m_boards.end()) {
        if (m_boards.size() >= kMaxBoards) return false;
        it = m_boards.emplace(board, Board{}).first;
    }
    Board& b = it->second;
    const std::string folded = foldName(player);
    auto mine = std::find_if(b.entries.begin(), b.entries.end(), [&](const Entry& e) { return foldName(e.name) == folded; });
    if (mine != b.entries.end()) {
        if (!better(b, score, mine->score)) return false;
        *mine = { player, score, time };
    } else {
        if (b.entries.size() >= kMaxEntries && !better(b, score, b.entries.back().score)) return false;
        b.entries.push_back({ player, score, time });
    }
    sort(b);
    if (b.entries.size() > kMaxEntries) b.entries.resize(kMaxEntries);
    m_dirty = true;
    return true;
}

std::vector<Leaderboard::Entry> Leaderboard::top(const std::string& board, size_t count) const {
    const auto it = m_boards.find(board);
    if (it == m_boards.end()) return {};
    const auto& e = it->second.entries;
    return { e.begin(), e.begin() + static_cast<std::ptrdiff_t>(std::min(count, e.size())) };
}

std::optional<size_t> Leaderboard::rank(const std::string& board, const std::string& player) const {
    const auto it = m_boards.find(board);
    if (it == m_boards.end()) return std::nullopt;
    const std::string folded = foldName(player);
    const auto& e = it->second.entries;
    for (size_t i = 0; i < e.size(); ++i)
        if (foldName(e[i].name) == folded) return i + 1;
    return std::nullopt;
}

void Leaderboard::setLowerIsBetter(const std::string& board, bool lower) {
    if (!validBoardName(board)) return;
    auto it = m_boards.find(board);
    if (it == m_boards.end()) {
        if (m_boards.size() >= kMaxBoards) return;
        it = m_boards.emplace(board, Board{}).first;
    }
    if (it->second.lowerIsBetter == lower) return;
    it->second.lowerIsBetter = lower;
    sort(it->second);
    m_dirty = true;
}

bool Leaderboard::reset(const std::string& board) {
    auto it = m_boards.find(board);
    if (it == m_boards.end()) return false;
    it->second.entries.clear();
    m_dirty = true;
    return true;
}

std::vector<std::string> Leaderboard::boards() const {
    std::vector<std::string> out;
    for (const auto& [name, b] : m_boards) out.push_back(name);
    return out;
}

std::string Leaderboard::toJson() const {
    nlohmann::json j = nlohmann::json::object();
    for (const auto& [name, b] : m_boards) {
        nlohmann::json entries = nlohmann::json::array();
        for (const Entry& e : b.entries) entries.push_back({ { "name", e.name }, { "score", e.score }, { "time", e.time } });
        j[name] = { { "lowerIsBetter", b.lowerIsBetter }, { "entries", entries } };
    }
    return j.dump(1) + "\n";
}

bool Leaderboard::fromJson(const std::string& text, std::vector<std::string>& errors) {
    const size_t before = errors.size();
    const nlohmann::json j = nlohmann::json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        errors.push_back("leaderboards.json: not a JSON object");
        return false;
    }
    m_boards.clear();
    for (auto it = j.begin(); it != j.end(); ++it) {
        if (!validBoardName(it.key()) || !it->is_object() || m_boards.size() >= kMaxBoards) {
            errors.push_back("leaderboards.json: skipped board '" + it.key() + "'");
            continue;
        }
        Board b;
        if (it->contains("lowerIsBetter") && (*it)["lowerIsBetter"].is_boolean()) b.lowerIsBetter = (*it)["lowerIsBetter"].get<bool>();
        size_t skipped = 0;
        if (it->contains("entries") && (*it)["entries"].is_array())
            for (const auto& e : (*it)["entries"]) {
                const bool ok = e.is_object() && e.contains("name") && e["name"].is_string() && e.contains("score") && e["score"].is_number_integer() &&
                                e["score"].get<int64_t>() >= INT32_MIN && e["score"].get<int64_t>() <= INT32_MAX;
                const std::string name = ok ? e["name"].get<std::string>() : std::string();
                if (!ok || name.empty() || name.size() > kMaxPlayerName || b.entries.size() >= kMaxEntries) {
                    ++skipped;
                    continue;
                }
                const uint64_t time = e.contains("time") && e["time"].is_number_unsigned() ? e["time"].get<uint64_t>() : 0;
                b.entries.push_back({ name, static_cast<int32_t>(e["score"].get<int64_t>()), time });
            }
        if (skipped) errors.push_back("leaderboards.json: board '" + it.key() + "': skipped " + std::to_string(skipped) + " damaged entries");
        sort(b);
        m_boards.emplace(it.key(), std::move(b));
    }
    m_dirty = false;
    return errors.size() == before;
}

bool Leaderboard::load(const std::string& path, std::vector<std::string>& errors) {
    std::string text;
    bool exists = false;
    if (!readFile(path, text, &exists)) {
        if (!exists) return true;
        errors.push_back(path + ": can't read it");
        return false;
    }
    return fromJson(text, errors);
}

bool Leaderboard::save(const std::string& path, std::string* error) {
    if (!writeFileAtomic(path, toJson(), error)) return false;
    m_dirty = false;
    return true;
}

bool Leaderboard::load(storage::Store& store, std::vector<std::string>& errors, bool* exists) {
    const std::optional<std::string> text = store.get("server", "leaderboards");
    if (exists) *exists = text.has_value();
    if (!text) {
        if (!store.lastError().empty()) errors.push_back("leaderboards: can't read them from the store: " + store.lastError());
        return false;
    }
    return fromJson(*text, errors);
}

bool Leaderboard::save(storage::Store& store, std::string* error) {
    if (!store.put("server", "leaderboards", toJson())) {
        if (error) *error = "leaderboards: " + store.lastError();
        return false;
    }
    m_dirty = false;
    return true;
}

// ---- messages

namespace {

template <typename Stream> void score(Stream& s, int32_t& v) {
    uint32_t bits = static_cast<uint32_t>(v);
    s.bits(bits, 32);
    v = static_cast<int32_t>(bits);
}
template <typename Stream> void serialize(Stream& s, LeaderboardSubmit& m) {
    s.string(m.board, Leaderboard::kMaxBoardName);
    score(s, m.score);
}
template <typename Stream> void serialize(Stream& s, LeaderboardQuery& m) {
    s.string(m.board, Leaderboard::kMaxBoardName);
    s.integer(m.count, 1, kMaxLeaderboardReply);
}
template <typename Stream> void serialize(Stream& s, LeaderboardReply& m) {
    s.string(m.board, Leaderboard::kMaxBoardName);
    s.string(m.error, kMaxLeaderboardError);
    uint32_t n = static_cast<uint32_t>(m.entries.size());
    s.integer(n, 0, kMaxLeaderboardReply);
    if constexpr (Stream::kReading) {
        if (!s.ok()) return;
        m.entries.resize(n);
    }
    for (Leaderboard::Entry& e : m.entries) {
        s.string(e.name, Leaderboard::kMaxPlayerName);
        score(s, e.score);
        uint32_t lo = static_cast<uint32_t>(e.time), hi = static_cast<uint32_t>(e.time >> 32);
        s.bits(lo, 32);
        s.bits(hi, 32);
        e.time = (static_cast<uint64_t>(hi) << 32) | lo;
    }
}

template <typename T> std::vector<uint8_t> write(T m) {
    std::vector<uint8_t> out;
    {
        net::WriteStream s(out);
        serialize(s, m);
    }
    return out;
}
template <typename T> std::optional<T> read(const std::vector<uint8_t>& payload) {
    T m;
    net::ReadStream s(payload.data(), payload.size());
    serialize(s, m);
    if (!s.ok()) return std::nullopt;
    return m;
}

} // namespace

std::vector<uint8_t> encode(const LeaderboardSubmit& m) { return write(m); }
std::vector<uint8_t> encode(const LeaderboardQuery& m) { return write(m); }
std::vector<uint8_t> encode(const LeaderboardReply& m) {
    LeaderboardReply capped = m;
    if (capped.entries.size() > kMaxLeaderboardReply) capped.entries.resize(kMaxLeaderboardReply);
    return write(capped);
}
std::optional<LeaderboardSubmit> decodeLeaderboardSubmit(const std::vector<uint8_t>& payload) { return read<LeaderboardSubmit>(payload); }
std::optional<LeaderboardQuery> decodeLeaderboardQuery(const std::vector<uint8_t>& payload) { return read<LeaderboardQuery>(payload); }
std::optional<LeaderboardReply> decodeLeaderboardReply(const std::vector<uint8_t>& payload) { return read<LeaderboardReply>(payload); }

} // namespace kke::server
