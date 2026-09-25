#include "kke/BenchmarkReport.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

namespace {
kke::BenchmarkReport sample() {
    kke::BenchmarkReport r;
    r.name = "physics";
    r.system = { { "os", "Linux" }, { "cpu", "Test CPU 9000" } };
    r.config = { { "ticks", "1200" }, { "threads", "4" } };
    r.sampleColumns = { "t_s", "fps", "step_avg_ms" };
    r.samples = { { 1, 60, 1.5 }, { 2, 58.5, 2.25 } };
    r.results = { { "step_avg_ms", 1.875 }, { "frames", 118 } };
    r.notes = { "hello" };
    return r;
}
} // namespace

TEST(BenchmarkReport, JsonHasEverySectionInOrder) {
    auto j = nlohmann::json::parse(sample().toJson());
    EXPECT_EQ(j["benchmark"], "physics");
    EXPECT_EQ(j["system"]["cpu"], "Test CPU 9000");
    EXPECT_EQ(j["config"]["threads"], "4");
    EXPECT_DOUBLE_EQ(j["results"]["step_avg_ms"].get<double>(), 1.875);
    EXPECT_EQ(j["sample_columns"].size(), 3u);
    EXPECT_EQ(j["samples"].size(), 2u);
    EXPECT_DOUBLE_EQ(j["samples"][1][2].get<double>(), 2.25);
    EXPECT_EQ(j["notes"][0], "hello");
}

TEST(BenchmarkReport, NonFiniteResultsBecomeNull) {
    auto r = sample();
    r.results.push_back({ "bad", std::nan("") });
    auto j = nlohmann::json::parse(r.toJson());
    EXPECT_TRUE(j["results"]["bad"].is_null());
}

TEST(BenchmarkReport, TextIsReadable) {
    std::string t = sample().toText();
    EXPECT_NE(t.find("=== KKE benchmark: physics ==="), std::string::npos);
    EXPECT_NE(t.find("Test CPU 9000"), std::string::npos);
    EXPECT_NE(t.find("step_avg_ms"), std::string::npos);
    EXPECT_NE(t.find("1.875"), std::string::npos);
    EXPECT_NE(t.find("118"), std::string::npos);   // integers printed without decimals
    EXPECT_EQ(t.find("118.000"), std::string::npos);
    EXPECT_NE(t.find("Note: hello"), std::string::npos);
}

TEST(BenchmarkReport, WritesBothFilesWithSafeNames) {
    auto dir = std::filesystem::temp_directory_path() / "kke_bench_test";
    std::filesystem::remove_all(dir);
    std::string base = sample().writeFiles(dir.string(), "20260101_120000", "my host/../x");
    ASSERT_FALSE(base.empty());
    EXPECT_TRUE(std::filesystem::exists(base + ".json"));
    EXPECT_TRUE(std::filesystem::exists(base + ".txt"));
    EXPECT_EQ(std::filesystem::path(base).parent_path(), dir); // host name can't escape the folder
    std::filesystem::remove_all(dir);
}

TEST(BenchmarkReport, Percentile) {
    EXPECT_DOUBLE_EQ(kke::percentile({}, 50), 0.0);
    EXPECT_DOUBLE_EQ(kke::percentile({ 5, 1, 3, 2, 4 }, 50), 3.0);
    EXPECT_DOUBLE_EQ(kke::percentile({ 5, 1, 3, 2, 4 }, 100), 5.0);
    EXPECT_DOUBLE_EQ(kke::percentile({ 5, 1, 3, 2, 4 }, 0), 1.0);
    std::vector<double> v(100);
    for (int i = 0; i < 100; ++i) v[i] = i + 1;
    EXPECT_DOUBLE_EQ(kke::percentile(v, 95), 95.0);
}

TEST(BenchmarkReport, TimestampShape) {
    std::string s = kke::timestampForFileName();
    ASSERT_EQ(s.size(), 15u);
    EXPECT_EQ(s[8], '_');
}
