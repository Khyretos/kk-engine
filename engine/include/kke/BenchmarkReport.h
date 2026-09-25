#pragma once

#include <string>
#include <utility>
#include <vector>

namespace kke {

// One benchmark run, written to disk as both .json (for tools and for an
// AI to analyze precisely) and .txt (for a person to read), so results
// from different machines and builds can be compared directly instead of
// scraped from log lines. Pure data + formatting; unit-tested.
//
// Conventions: every value a person might compare carries its unit in
// the key name ("step_avg_ms", "peak_rss_mb"); sample rows are one per
// reporting window (usually one second).
struct BenchmarkReport {
    using KeyValue = std::pair<std::string, std::string>;

    std::string name;                        // e.g. "physics" — also the file name prefix
    std::vector<KeyValue> system;            // collectSystemInfo()
    std::vector<KeyValue> config;            // what was run: ticks, threads, scene...
    std::vector<std::string> sampleColumns;  // column names for `samples`
    std::vector<std::vector<double>> samples;
    std::vector<std::pair<std::string, double>> results; // the headline numbers
    std::vector<std::string> notes;

    std::string toJson() const;
    std::string toText() const;

    // Writes <dir>/<name>_<stamp>_<host>.json and .txt (creating <dir>).
    // Returns the path without extension, or "" on failure.
    std::string writeFiles(const std::string& dir, const std::string& stamp, const std::string& host) const;
};

// p in [0,100]; nearest-rank percentile of `values` (0 if empty).
double percentile(std::vector<double> values, double p);

// Machine description for reports: OS, CPU model and cores, RAM, and —
// when given a Vulkan physical device — GPU name, type, driver and API
// version. Also build type and compiler. Never throws; unknown fields
// say "unknown".
std::vector<BenchmarkReport::KeyValue> collectSystemInfo(void* vkPhysicalDevice = nullptr);

// Peak resident memory of this process so far, in MB (0 if unknown).
double peakResidentMemoryMb();

// "20260925_231108" in local time, for file names.
std::string timestampForFileName();
std::string hostNameForFileName();

} // namespace kke
