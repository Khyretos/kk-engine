#include "kke/BenchmarkReport.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace kke {

namespace {
std::string fmt(double v) {
    char buf[64];
    if (std::isfinite(v) && std::fabs(v - std::round(v)) < 1e-9 && std::fabs(v) < 1e15) std::snprintf(buf, sizeof(buf), "%.0f", v);
    else std::snprintf(buf, sizeof(buf), "%.3f", v);
    return buf;
}
std::string sanitizeForFileName(std::string s) {
    for (char& c : s) {
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
        if (!ok) c = '-';
    }
    return s.empty() ? std::string("unknown") : s;
}
} // namespace

double percentile(std::vector<double> values, double p) {
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    p = std::clamp(p, 0.0, 100.0);
    size_t rank = static_cast<size_t>(std::ceil(p / 100.0 * static_cast<double>(values.size())));
    return values[std::min(values.size() - 1, rank == 0 ? 0 : rank - 1)];
}

std::string BenchmarkReport::toJson() const {
    nlohmann::ordered_json j;
    j["benchmark"] = name;
    j["format_version"] = 1;
    auto kv = [](const std::vector<KeyValue>& list) {
        nlohmann::ordered_json o = nlohmann::ordered_json::object();
        for (const auto& [k, v] : list) o[k] = v;
        return o;
    };
    j["system"] = kv(system);
    j["config"] = kv(config);
    nlohmann::ordered_json r = nlohmann::ordered_json::object();
    for (const auto& [k, v] : results) r[k] = std::isfinite(v) ? nlohmann::ordered_json(v) : nlohmann::ordered_json(nullptr);
    j["results"] = r;
    j["sample_columns"] = sampleColumns;
    j["samples"] = samples;
    j["notes"] = notes;
    return j.dump(2);
}

std::string BenchmarkReport::toText() const {
    std::ostringstream out;
    out << "=== KKE benchmark: " << name << " ===\n\n";
    auto section = [&](const char* title, const std::vector<KeyValue>& list) {
        out << title << "\n";
        size_t width = 0;
        for (const auto& [k, v] : list) width = std::max(width, k.size());
        for (const auto& [k, v] : list) out << "  " << k << std::string(width - k.size() + 2, ' ') << v << "\n";
        out << "\n";
    };
    section("System", system);
    section("Config", config);
    std::vector<KeyValue> res;
    for (const auto& [k, v] : results) res.emplace_back(k, fmt(v));
    section("Results", res);
    if (!samples.empty()) {
        out << "Samples\n  ";
        for (const auto& c : sampleColumns) out << c << "\t";
        out << "\n";
        for (const auto& row : samples) {
            out << "  ";
            for (double v : row) out << fmt(v) << "\t";
            out << "\n";
        }
        out << "\n";
    }
    for (const auto& n : notes) out << "Note: " << n << "\n";
    return out.str();
}

std::string BenchmarkReport::writeFiles(const std::string& dir, const std::string& stamp, const std::string& host) const {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    std::string base = (std::filesystem::path(dir) / (sanitizeForFileName(name) + "_" + sanitizeForFileName(stamp) + "_" +
                                                      sanitizeForFileName(host))).string();
    std::ofstream json(base + ".json"), text(base + ".txt");
    if (!json || !text) return {};
    json << toJson() << "\n";
    text << toText();
    return (json && text) ? base : std::string();
}

std::string timestampForFileName() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", &tm);
    return buf;
}

} // namespace kke
