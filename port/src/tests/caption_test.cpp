#include <gtest/gtest.h>

#include <set>
#include <string>

#include "caption_text.hpp"
#include "texture_port.hpp"

// The captions' look is the painted English captions', sampled from the disc's stayframe: a cream fill over a
// blue outline, the same in every language.
TEST(Caption, ColoursAreThePaintedEnglishOnes) {
    EXPECT_EQ(caption::kFill[0], 232);
    EXPECT_EQ(caption::kFill[1], 226);
    EXPECT_EQ(caption::kFill[2], 183);
    EXPECT_EQ(caption::kOutline[0], 1);
    EXPECT_EQ(caption::kOutline[1], 0);
    EXPECT_EQ(caption::kOutline[2], 201);
}

// One string per icon of the menu, each with its own key, for a translation to fill in.
TEST(Caption, EveryIconHasItsOwnString) {
    const auto         strings = caption::Strings();
    std::set<std::string> keys;
    ASSERT_EQ(strings.size(), static_cast<size_t>(caption::kIcons));
    for (const auto &[key, english] : strings) {
        EXPECT_TRUE(keys.insert(key).second) << key;
        EXPECT_EQ(key.rfind("menu.caption.", 0), 0U) << key;
        EXPECT_FALSE(english.empty()) << key;
    }
}

// Another texture's pixels are never touched.
TEST(Caption, BlankPaintedLeavesOtherTexturesAlone) {
    PortDecodedTexture texture;
    texture.width = 4;
    texture.height = 4;
    texture.format = gfx::TextureFormat::Rgba8;
    texture.levels.push_back(std::vector<uint8_t>(4 * 4 * 4, 200));
    caption::BlankPainted("other", texture);
    for (uint8_t byte : texture.levels[0]) {
        EXPECT_EQ(byte, 200);
    }
}
