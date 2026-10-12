#include <gtest/gtest.h>

#include <vector>

#include "platform/config.hpp"
#include "textwipe.hpp"

// The slide starts with everything old (the line at the right edge), rests, eases to the left edge, rests and eases back.
TEST(TextWipe, SweepStartsAtTheRightAndReachesTheLeft) {
    EXPECT_FLOAT_EQ(textwipe::SweepPosition(0.0), 640.0f);
    EXPECT_FLOAT_EQ(textwipe::SweepPosition(0.5), 640.0f);
    EXPECT_NEAR(textwipe::SweepPosition(0.8 + 2.0), 320.0f, 0.5f);
    EXPECT_FLOAT_EQ(textwipe::SweepPosition(0.8 + 4.0 + 0.4), 0.0f);
    EXPECT_NEAR(textwipe::SweepPosition(0.8 + 4.0 + 0.8 + 2.0), 320.0f, 0.5f);
}

TEST(TextWipe, SweepRepeatsAndNeverLeavesTheScreen) {
    const double cycle = 2.0 * (4.0 + 0.8);
    for (double t = 0.0; t < 3 * cycle; t += 0.05) {
        const float x = textwipe::SweepPosition(t);
        EXPECT_GE(x, 0.0f);
        EXPECT_LE(x, 640.0f);
        EXPECT_NEAR(x, textwipe::SweepPosition(t + cycle), 0.01f);
    }
}

// Off, a place that draws both looks draws only the runtime one, as the game does.
TEST(TextWipe, OffDrawsOnlyTheRuntimeLook) {
    ASSERT_FALSE(textwipe::Active());
    std::vector<bool> drawn;
    textwipe::Both([&](bool runtime) { drawn.push_back(runtime); });
    ASSERT_EQ(drawn.size(), 1u);
    EXPECT_TRUE(drawn[0]);
}

// Untuned text is drawn as the code lays it out: every dial is at its identity.
TEST(TextWipe, DefaultTuningChangesNothing) {
    const textwipe::Tuning tuning;
    EXPECT_FLOAT_EQ(tuning.size, 1.0f);
    EXPECT_FLOAT_EQ(tuning.xscale, 1.0f);
    EXPECT_FLOAT_EQ(tuning.yscale, 1.0f);
    EXPECT_FLOAT_EQ(tuning.letter, 0.0f);
    EXPECT_FLOAT_EQ(tuning.word, 0.0f);
    EXPECT_FLOAT_EQ(tuning.dx, 0.0f);
    EXPECT_FLOAT_EQ(tuning.dy, 0.0f);
    EXPECT_FLOAT_EQ(tuning.fit, 1.0f);
}

// The tool is a developer's, behind game.text_wipe: off (the default) its keys do nothing and every kind of text keeps
// the locked-in fit, whatever tuning a file holds.
TEST(TextWipe, OffByDefaultAndTextKeepsTheLockedFits) {
    ASSERT_FALSE(ConfigGet().text_wipe);
    textwipe::Update();
    EXPECT_FALSE(textwipe::Active());
    for (int k = 0; k < static_cast<int>(textwipe::Kind::Count); k++) {
        const auto             kind = static_cast<textwipe::Kind>(k);
        const textwipe::Tuning locked = textwipe::Locked(kind);
        const textwipe::Tuning &now = textwipe::Get(kind);
        EXPECT_FLOAT_EQ(now.size, locked.size);
        EXPECT_FLOAT_EQ(now.xscale, locked.xscale);
        EXPECT_FLOAT_EQ(now.yscale, locked.yscale);
        EXPECT_FLOAT_EQ(now.dx, locked.dx);
        EXPECT_FLOAT_EQ(now.dy, locked.dy);
        EXPECT_FLOAT_EQ(now.lock, locked.lock);
    }
}
