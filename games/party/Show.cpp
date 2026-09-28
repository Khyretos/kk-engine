#include "Show.h"

#include <algorithm>
#include <numeric>

namespace party {

std::vector<int> placesOf(const std::vector<RoundResult>& results) {
    const size_t n = results.size();
    // A sort key per bean: group (0 finished, 1 standing, 2 out), then
    // the value inside the group (smaller is better).
    struct Key {
        int group = 1;
        float value = 0.0f;
    };
    std::vector<Key> keys(n);
    for (size_t i = 0; i < n; ++i) {
        const RoundResult& r = results[i];
        if (r.finished) keys[i] = { 0, static_cast<float>(r.finishOrder) };
        else if (r.out) keys[i] = { 2, -static_cast<float>(r.outOrder) };
        else keys[i] = { 1, -r.score };
    }
    std::vector<size_t> order(n);
    std::iota(order.begin(), order.end(), size_t{ 0 });
    auto better = [&keys](size_t a, size_t b) {
        if (keys[a].group != keys[b].group) return keys[a].group < keys[b].group;
        return keys[a].value < keys[b].value;
    };
    std::stable_sort(order.begin(), order.end(), better);
    std::vector<int> places(n, 0);
    for (size_t k = 0; k < n; ++k) {
        const size_t i = order[k];
        // Equal to the one before: the same place.
        if (k > 0 && !better(order[k - 1], i) && !better(i, order[k - 1])) places[i] = places[order[k - 1]];
        else places[i] = static_cast<int>(k);
    }
    return places;
}

int pointsFor(int place, int count) {
    static constexpr int kPoints[] = { 10, 8, 6, 5, 4, 3, 2, 1 };
    if (place < 0 || place >= count || place >= static_cast<int>(sizeof(kPoints) / sizeof(kPoints[0]))) return 0;
    return kPoints[place];
}

void Show::start(const std::vector<std::string>& all, int roundCount, int beans, uint32_t seed, bool shuffle) {
    pool = all;
    playlist = all;
    if (shuffle) {
        Rng rng(seed);
        // Fisher-Yates with our own generator: the same order everywhere.
        for (size_t i = playlist.size(); i > 1; --i) std::swap(playlist[i - 1], playlist[static_cast<size_t>(rng.below(static_cast<int>(i)))]);
    }
    // More rounds than games: the playlist goes round again.
    rounds = std::max(1, roundCount);
    round = 0;
    points.assign(static_cast<size_t>(std::max(0, beans)), 0);
}

const std::string& Show::current() const {
    static const std::string none;
    if (playlist.empty()) return none;
    return playlist[static_cast<size_t>(round) % playlist.size()];
}

std::vector<std::string> Show::candidates(int count, uint32_t seed) const {
    std::vector<std::string> fresh, rest;
    const std::string last = round > 0 ? playlist[static_cast<size_t>(round - 1) % playlist.size()] : std::string();
    for (const std::string& g : pool) {
        bool played = false;
        for (int r = 0; r < round && r < static_cast<int>(playlist.size()); ++r) played = played || playlist[static_cast<size_t>(r)] == g;
        (played ? rest : fresh).push_back(g);
    }
    Rng rng(seed);
    auto shuffle = [&rng](std::vector<std::string>& v) {
        for (size_t i = v.size(); i > 1; --i) std::swap(v[i - 1], v[static_cast<size_t>(rng.below(static_cast<int>(i)))]);
    };
    shuffle(fresh);
    shuffle(rest);
    // The one just played goes last: only picked when there's nothing else.
    std::stable_partition(rest.begin(), rest.end(), [&last](const std::string& g) { return g != last; });
    std::vector<std::string> out = fresh;
    for (const std::string& g : rest)
        if (g != last || pool.size() <= 1) out.push_back(g);
    if (out.size() > static_cast<size_t>(std::max(0, count))) out.resize(static_cast<size_t>(std::max(0, count)));
    return out;
}

void Show::choose(const std::string& game) {
    if (game.empty()) return;
    // The playlist holds every round's game from here on (voting picks each one).
    if (playlist.size() <= static_cast<size_t>(round)) {
        const std::vector<std::string> base = playlist.empty() ? pool : playlist;
        while (playlist.size() <= static_cast<size_t>(round) && !base.empty()) playlist.push_back(base[playlist.size() % base.size()]);
    }
    if (static_cast<size_t>(round) < playlist.size()) playlist[static_cast<size_t>(round)] = game;
}

int tally(const std::vector<int>& votes, int choices, uint32_t seed) {
    if (choices <= 0) return -1;
    std::vector<int> count(static_cast<size_t>(choices), 0);
    for (int v : votes)
        if (v >= 0 && v < choices) ++count[static_cast<size_t>(v)];
    const int most = *std::max_element(count.begin(), count.end());
    std::vector<int> best;
    for (int c = 0; c < choices; ++c)
        if (count[static_cast<size_t>(c)] == most) best.push_back(c);
    Rng rng(seed);
    return best[static_cast<size_t>(rng.below(static_cast<int>(best.size())))];
}

std::vector<int> Show::score(const std::vector<RoundResult>& results) {
    const std::vector<int> places = placesOf(results);
    std::vector<int> got(results.size(), 0);
    const int count = static_cast<int>(results.size());
    for (size_t i = 0; i < results.size(); ++i) {
        got[i] = pointsFor(places[i], count);
        if (i < points.size()) points[i] += got[i];
    }
    return got;
}

std::vector<int> Show::standings() const {
    std::vector<int> order(points.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [this](int a, int b) { return points[static_cast<size_t>(a)] > points[static_cast<size_t>(b)]; });
    return order;
}

std::vector<int> Show::standingPlaces() const {
    const std::vector<int> order = standings();
    std::vector<int> places(points.size(), 0);
    for (size_t k = 0; k < order.size(); ++k) {
        const size_t i = static_cast<size_t>(order[k]);
        if (k > 0 && points[static_cast<size_t>(order[k - 1])] == points[i]) places[i] = places[static_cast<size_t>(order[k - 1])];
        else places[i] = static_cast<int>(k);
    }
    return places;
}

} // namespace party
