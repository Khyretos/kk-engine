#include "kke/Accessibility.h"
#include "kke/EngineSettings.h"
#include "kke/Subtitles.h"

#include <gtest/gtest.h>

// Accessibility (docs/ACCESSIBILITY.md): colour correction for the colour-blind,
// the screen reader hook, and the captions' timing.

using kke::Accessibility;
using kke::Subtitles;

TEST(Accessibility, OffAndZeroStrengthLeaveColoursAlone) {
    for (const char* mode : { "off", "", "rainbow" })
        EXPECT_EQ(Accessibility::colorMatrix(mode, 1.0f), glm::mat3(1.0f)) << mode;
    EXPECT_EQ(Accessibility::colorMatrix("deuteranopia", 0.0f), glm::mat3(1.0f));
    const auto rows = Accessibility::colorMatrixRows("off", 1.0f);
    ASSERT_EQ(rows.size(), 3u);
    EXPECT_EQ(rows[0], glm::vec4(1, 0, 0, 0.0f)); // w = 0: the shader skips it
    EXPECT_EQ(rows[1], glm::vec4(0, 1, 0, 0.0f));
    EXPECT_EQ(rows[2], glm::vec4(0, 0, 1, 0.0f));
}

TEST(Accessibility, GreysStayGreyInEveryMode) {
    for (const char* mode : { "protanopia", "deuteranopia", "tritanopia" })
        for (float g : { 0.0f, 0.18f, 0.5f, 1.0f }) {
            const glm::vec3 out = Accessibility::correct(glm::vec3(g), mode, 1.0f);
            EXPECT_NEAR(out.x, g, 2e-3f) << mode << " " << g;
            EXPECT_NEAR(out.y, g, 2e-3f) << mode << " " << g;
            EXPECT_NEAR(out.z, g, 2e-3f) << mode << " " << g;
        }
}

TEST(Accessibility, ARedForADeuteranIsMovedIntoGreenAndBlue) {
    // Worked by hand: the simulated red is (0.367322, 0.280085, -0.011820), the error
    // (0.632678, -0.280085, 0.011820); 0.7 of the red error and all of the others go to green and blue.
    const glm::vec3 out = Accessibility::correct(glm::vec3(1.0f, 0.0f, 0.0f), "deuteranopia", 1.0f);
    EXPECT_NEAR(out.x, 1.0f, 1e-4f);
    EXPECT_NEAR(out.y, 0.16289f, 1e-3f);
    EXPECT_NEAR(out.z, 0.45469f, 1e-3f);
    // Half the strength, half the shift.
    const glm::vec3 half = Accessibility::correct(glm::vec3(1.0f, 0.0f, 0.0f), "deuteranopia", 0.5f);
    EXPECT_NEAR(half.y, 0.081445f, 1e-3f);
    EXPECT_NEAR(half.z, 0.227347f, 1e-3f);
}

TEST(Accessibility, ABlueForATritanIsMovedIntoRedAndGreen) {
    const glm::vec3 out = Accessibility::correct(glm::vec3(0.0f, 0.0f, 1.0f), "tritanopia", 1.0f);
    EXPECT_GT(out.x, 0.05f);
    EXPECT_GT(out.y, 0.05f);
    EXPECT_NEAR(out.z, 1.0f, 0.5f); // still bright blue (it is not removed)
    const glm::vec3 same = Accessibility::correct(glm::vec3(1.0f, 0.0f, 0.0f), "protanopia", 1.0f);
    EXPECT_NEAR(same.x, 1.0f, 1e-3f);
    EXPECT_GT(same.y + same.z, 0.1f);
}

TEST(Accessibility, TheShaderRowsMatchTheMatrix) {
    const auto rows = Accessibility::colorMatrixRows("deuteranopia", 1.0f);
    ASSERT_EQ(rows.size(), 3u);
    EXPECT_EQ(rows[0].w, 1.0f); // on
    const glm::vec3 c(0.8f, 0.3f, 0.1f);
    const glm::vec3 viaRows(glm::dot(glm::vec3(rows[0]), c), glm::dot(glm::vec3(rows[1]), c), glm::dot(glm::vec3(rows[2]), c));
    const glm::vec3 viaMatrix = Accessibility::colorMatrix("deuteranopia", 1.0f) * c;
    EXPECT_NEAR(viaRows.x, viaMatrix.x, 1e-5f);
    EXPECT_NEAR(viaRows.y, viaMatrix.y, 1e-5f);
    EXPECT_NEAR(viaRows.z, viaMatrix.z, 1e-5f);
    EXPECT_EQ(Accessibility::colorMatrixRows("deuteranopia", 0.0f)[0].w, 0.0f); // strength 0 is off too
}

TEST(Accessibility, SettingsSetTheOptionsAndListenersHearRealChanges) {
    Accessibility a;
    int heard = 0;
    a.onChange([&] { ++heard; });
    kke::EngineSettings s;
    s.accessibility.reduceMotion = true;
    s.accessibility.colorBlind = "tritanopia";
    s.accessibility.colorBlindStrength = 0.4f;
    a.apply(s);
    EXPECT_EQ(heard, 1);
    EXPECT_TRUE(a.reduceMotion());
    EXPECT_FALSE(a.highContrast());
    EXPECT_FALSE(a.screenReader());
    EXPECT_EQ(a.options().colorBlind, "tritanopia");
    EXPECT_FLOAT_EQ(a.options().colorBlindStrength, 0.4f);
    a.apply(s); // nothing changed
    EXPECT_EQ(heard, 1);
    s.accessibility.highContrast = true;
    a.apply(s);
    EXPECT_EQ(heard, 2);
    EXPECT_TRUE(a.highContrast());
}

TEST(Accessibility, AnnouncementsGoToTheHookOnlyWhenTheScreenReaderIsOn) {
    Accessibility a;
    std::vector<std::pair<std::string, bool>> spoken;
    a.setAnnouncer([&](const std::string& t, bool interrupt) { spoken.emplace_back(t, interrupt); });
    EXPECT_FALSE(a.announce("Play")); // off: kept, not spoken
    EXPECT_TRUE(spoken.empty());
    Accessibility::Options o;
    o.screenReader = true;
    a.set(o);
    EXPECT_TRUE(a.announce("Settings", true));
    EXPECT_TRUE(a.announce("Volume 80 percent"));
    ASSERT_EQ(spoken.size(), 2u);
    EXPECT_EQ(spoken[0].first, "Settings");
    EXPECT_TRUE(spoken[0].second);
    EXPECT_FALSE(spoken[1].second);
    EXPECT_FALSE(a.announce("")); // nothing to say
    for (int i = 0; i < 30; ++i) a.announce("line " + std::to_string(i));
    const auto recent = a.recent();
    EXPECT_EQ(recent.size(), 16u);
    EXPECT_EQ(recent.back(), "line 29");
    // Without a hook there is nothing to speak to.
    Accessibility b;
    Accessibility::Options on;
    on.screenReader = true;
    b.set(on);
    EXPECT_FALSE(b.announce("hello"));
    EXPECT_EQ(b.recent().size(), 1u);
}

TEST(Accessibility, SystemSpeechIsThereOrNullButNeverBroken) {
    EXPECT_NO_THROW({ const auto speech = Accessibility::systemSpeech(); (void)speech; });
}

TEST(Subtitles, ReadingTimeCountsCharactersNotBytes) {
    EXPECT_FLOAT_EQ(Subtitles::readingTime("Hi"), 1.5f);                // the minimum
    EXPECT_NEAR(Subtitles::readingTime(std::string(34, 'a')), 3.2f, 1e-4f);
    std::string accented;
    for (int i = 0; i < 17; ++i) accented += "\xC3\xA9"; // 17 characters, 34 bytes
    EXPECT_NEAR(Subtitles::readingTime(accented), 2.2f, 1e-4f);
    EXPECT_FLOAT_EQ(Subtitles::readingTime(std::string(500, 'a')), 12.0f); // the maximum
}

TEST(Subtitles, LinesComeAndGoWithTime) {
    Subtitles s;
    EXPECT_EQ(s.say(""), 0);
    Subtitles::Options o;
    o.speaker = "Mira";
    o.duration = 2.0f;
    const int a = s.say("Over here!", o);
    ASSERT_GT(a, 0);
    ASSERT_EQ(s.lines().size(), 1u);
    EXPECT_EQ(s.lines()[0].speaker, "Mira");
    EXPECT_EQ(s.lines()[0].text, "Over here!");
    s.update(1.0f);
    EXPECT_EQ(s.lines().size(), 1u);
    s.update(1.5f);
    EXPECT_TRUE(s.lines().empty());
}

TEST(Subtitles, TheSameCaptionIsRenewedNotRepeated) {
    Subtitles s;
    Subtitles::Options o;
    o.duration = 2.0f;
    const int a = s.say("Watch out!", o);
    s.update(1.5f);
    const int b = s.say("Watch out!", o);
    EXPECT_EQ(a, b);
    EXPECT_EQ(s.lines().size(), 1u);
    s.update(1.0f);
    EXPECT_EQ(s.lines().size(), 1u); // renewed: 1 s of 2 s gone, not 2.5
    o.speaker = "Kai";
    EXPECT_NE(s.say("Watch out!", o), a); // another speaker: another line
    EXPECT_EQ(s.lines().size(), 2u);
}

TEST(Subtitles, OnlyTheNewestLinesShow) {
    Subtitles s;
    s.setMaxLines(2);
    for (int i = 0; i < 4; ++i) s.say("line " + std::to_string(i));
    const auto lines = s.lines();
    ASSERT_EQ(lines.size(), 2u);
    EXPECT_EQ(lines[0].text, "line 2");
    EXPECT_EQ(lines[1].text, "line 3");
    s.setMaxLines(99);
    EXPECT_EQ(s.maxLines(), 8);
}

TEST(Subtitles, OffMeansNoCaptions) {
    Subtitles s;
    s.say("hello");
    s.setEnabled(false);
    EXPECT_TRUE(s.lines().empty());
    EXPECT_EQ(s.say("ignored"), 0);
    s.setEnabled(true);
    EXPECT_GT(s.say("back"), 0);
}

TEST(Subtitles, TheRevisionChangesOnlyWhenTheLinesDo) {
    Subtitles s;
    const unsigned r0 = s.revision();
    s.update(1.0f); // nothing on screen
    EXPECT_EQ(s.revision(), r0);
    const int id = s.say("hello");
    EXPECT_NE(s.revision(), r0);
    const unsigned r1 = s.revision();
    s.update(0.1f); // still there
    EXPECT_EQ(s.revision(), r1);
    s.remove(id);
    EXPECT_NE(s.revision(), r1);
    const unsigned r2 = s.revision();
    s.remove(id);
    s.clear();
    EXPECT_EQ(s.revision(), r2); // nothing to remove or clear
}
