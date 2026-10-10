#include <gtest/gtest.h>

#include <vector>

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
