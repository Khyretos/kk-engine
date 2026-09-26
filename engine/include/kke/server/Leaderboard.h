#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace kke::server {

// The leaderboard role (docs/SERVER_HOSTING.md): named boards, each a
// player's best score, kept in saveDir/leaderboards.json. Higher is
// better unless a board is set lower-is-better (race times).
//
// Games talk to it with game events (kEventLeaderboard* below): a query
// gets a reply to that player only; a submit from a client counts only
// when the server allows client scores (otherwise the server's own code,
// e.g. a server script, submits).
class Leaderboard {
public:
    struct Entry {
        std::string name;
        int32_t score = 0;
        uint64_t time = 0; // seconds since 1970, when the score was set
    };

    static constexpr size_t kMaxBoards = 64;
    static constexpr size_t kMaxEntries = 1000; // per board; the worst drop off
    static constexpr size_t kMaxBoardName = 32;
    static constexpr size_t kMaxPlayerName = 24;

    // Board names: 1-32 of a-z 0-9 _ - . (they end up in a file and logs).
    static bool validBoardName(const std::string& board);

    // The player's best on this board; true when it improved (or is new).
    // false for a bad board or player name, or a new board past kMaxBoards.
    bool submit(const std::string& board, const std::string& player, int32_t score, uint64_t time);
    std::vector<Entry> top(const std::string& board, size_t count) const;
    // 1-based rank, if the player is on the board.
    std::optional<size_t> rank(const std::string& board, const std::string& player) const;
    void setLowerIsBetter(const std::string& board, bool lower);
    bool reset(const std::string& board);
    std::vector<std::string> boards() const;
    bool dirty() const { return m_dirty; }

    std::string toJson() const;
    bool fromJson(const std::string& text, std::vector<std::string>& errors);
    bool load(const std::string& path, std::vector<std::string>& errors);
    bool save(const std::string& path, std::string* error = nullptr); // clears dirty

private:
    struct Board {
        bool lowerIsBetter = false;
        std::vector<Entry> entries; // sorted best first
    };
    bool better(const Board& b, int32_t x, int32_t y) const { return b.lowerIsBetter ? x < y : x > y; }
    void sort(Board& b) const;
    std::map<std::string, Board> m_boards;
    bool m_dirty = false;
};

// ---- messages, as game events (GameEventMsg kinds; payloads below)
constexpr uint16_t kEventLeaderboardSubmit = 0xFE10; // client -> server: board, score
constexpr uint16_t kEventLeaderboardQuery = 0xFE11;  // client -> server: board, count
constexpr uint16_t kEventLeaderboardReply = 0xFE12;  // server -> that client: board, entries, error
constexpr size_t kMaxLeaderboardReply = 10; // entries per reply: keeps it in one event (kMaxEventBytes)
constexpr size_t kMaxLeaderboardError = 60;

struct LeaderboardSubmit {
    std::string board;
    int32_t score = 0;
};
struct LeaderboardQuery {
    std::string board;
    uint16_t count = 10;
};
struct LeaderboardReply {
    std::string board;
    std::vector<Leaderboard::Entry> entries;
    std::string error; // "" = fine; otherwise why there are no entries
};

std::vector<uint8_t> encode(const LeaderboardSubmit& m);
std::vector<uint8_t> encode(const LeaderboardQuery& m);
std::vector<uint8_t> encode(const LeaderboardReply& m);
std::optional<LeaderboardSubmit> decodeLeaderboardSubmit(const std::vector<uint8_t>& payload);
std::optional<LeaderboardQuery> decodeLeaderboardQuery(const std::vector<uint8_t>& payload);
std::optional<LeaderboardReply> decodeLeaderboardReply(const std::vector<uint8_t>& payload);

} // namespace kke::server
