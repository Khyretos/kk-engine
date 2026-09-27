#include "Progress.h"

#include <algorithm>

namespace climb_race {

const Progress::Record* Progress::record(const std::string& id) const {
    const auto it = m_records.find(id);
    return it == m_records.end() ? nullptr : &it->second;
}

Progress::Result Progress::finish(const std::vector<Mountain>& tour, const Mountain& m, float seconds) {
    Result out;
    if (m.id.empty() || m.id == "random" || seconds <= 0.0f) return out;
    size_t index = tour.size();
    for (size_t i = 0; i < tour.size(); ++i)
        if (tour[i].id == m.id) index = i;
    const bool nextWasOpen = index + 1 < tour.size() && isOpen(tour, index + 1);
    Record& r = m_records[m.id];
    out.newBest = r.best <= 0.0f || seconds < r.best;
    if (out.newBest) r.best = seconds;
    ++r.finishes;
    out.medal = medalFor(m, seconds);
    if (out.medal >= 0 && (r.medal < 0 || out.medal < r.medal)) {
        out.betterMedal = true;
        r.medal = out.medal;
    }
    if (index + 1 < tour.size() && !nextWasOpen) out.opened = tour[index + 1].id;
    return out;
}

bool Progress::isOpen(const std::vector<Mountain>& tour, size_t index) const {
    if (openAll || index == 0) return true;
    if (index >= tour.size()) return false;
    const Record* before = record(tour[index - 1].id);
    return before && before->finishes > 0;
}

nlohmann::json Progress::save() const {
    nlohmann::json mountains = nlohmann::json::object();
    for (const auto& [id, r] : m_records) {
        nlohmann::json j = { { "best", r.best }, { "finishes", r.finishes } };
        if (r.medal >= 0) j["medal"] = medalName(r.medal);
        mountains[id] = std::move(j);
    }
    return { { "mountains", std::move(mountains) } };
}

void Progress::load(const nlohmann::json& j) {
    m_records.clear();
    const auto mountains = j.find("mountains");
    if (mountains == j.end() || !mountains->is_object()) return;
    for (const auto& [id, v] : mountains->items()) {
        if (!v.is_object()) continue;
        Record r;
        if (const auto b = v.find("best"); b != v.end() && b->is_number()) r.best = std::max(0.0f, b->get<float>());
        if (const auto f = v.find("finishes"); f != v.end() && f->is_number_integer()) r.finishes = std::max(0, f->get<int>());
        if (const auto m = v.find("medal"); m != v.end() && m->is_string())
            for (int i = 0; i < 3; ++i)
                if (m->get<std::string>() == medalName(i)) r.medal = i;
        if (r.best > 0.0f && r.finishes == 0) r.finishes = 1;
        m_records[id] = r;
    }
}

} // namespace climb_race
