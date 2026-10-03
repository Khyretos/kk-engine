#include "kke/DataFile.h"
#include "kke/ParticleLibrary.h"

#include <gtest/gtest.h>

TEST(ParticleLibrary, BuiltInEffectsParse) {
    nlohmann::json data;
    std::string error;
    ASSERT_TRUE(kke::datafile::parse(kke::ParticleLibrary::builtInYaml(), kke::datafile::Format::Yaml, data, &error)) << error;
    EXPECT_TRUE(kke::ParticleLibrary::validate(data, &error)) << error;
    EXPECT_GE(data.size(), 24u);
    for (const char* name : { "fire", "campfire", "explosion", "dust", "sparks", "wood_chips", "glass_shatter", "rain", "snow", "confetti" })
        EXPECT_TRUE(data.contains(name)) << name;
    for (auto it = data.begin(); it != data.end(); ++it) EXPECT_FALSE(it.value().value("description", std::string()).empty()) << it.key();
}

TEST(ParticleLibrary, MistakesAreNamed) {
    std::string error;
    nlohmann::json bad = nlohmann::json::parse(R"({ "puff": { "layers": [ { "kind": "plasma", "count": 3 } ] } })");
    EXPECT_FALSE(kke::ParticleLibrary::validate(bad, &error));
    EXPECT_NE(error.find("puff"), std::string::npos);
    EXPECT_NE(error.find("plasma"), std::string::npos);
    bad = nlohmann::json::parse(R"({ "puff": { "layers": [ { "kind": "smoke" } ] } })");
    EXPECT_FALSE(kke::ParticleLibrary::validate(bad, &error));
    EXPECT_NE(error.find("count"), std::string::npos);
    nlohmann::json good = nlohmann::json::parse(R"({ "puff": { "layers": [ { "kind": "flake", "count": 3, "shape": "disc", "speed": 2 } ] } })");
    EXPECT_TRUE(kke::ParticleLibrary::validate(good, &error)) << error;
}
