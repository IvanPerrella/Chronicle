#include <SDL3/SDL.h>
#include <gtest/gtest.h>

#include <chrono>
#include <cstring>
#include <thread>

#include "draw3d.hpp"
#include "gameloop.hpp"
#include "gfx/displaylist.hpp"
#include "gfx_fixture.hpp"
#include "menu_draw.hpp"
#include "menu_inventory.hpp"
#include "platform/clock.hpp"
#include "renderinfo.hpp"
#include "texture.hpp"
#include "texture_port.hpp"

TEST(ChroniclePresentation, FullSizePictureFillsWidescreenWithoutSamplingPastArtwork) {
    dc::test::GfxFixture fixture(1280, 720, 1.0f, dc::test::GfxOptions{.offscreen = true});
    PortDecodedTexture   image;
    image.width = 640;
    image.height = 448;
    image.levels.emplace_back(640 * 448 * 4, 255);
    for (unsigned y = 0; y < image.height; ++y) {
        for (unsigned x = 0; x < image.width; ++x) {
            image.levels[0][(y * image.width + x) * 4] = static_cast<uint8_t>(y / 2);
            image.levels[0][(y * image.width + x) * 4 + 1] = static_cast<uint8_t>(x / 3);
        }
    }
    const unsigned key = PortCreateTexture(image, PortTextureOwner::Manager, nullptr);
    ASSERT_NE(key, 0u);
    CTexture texture;
    texture.tex0 = SCE_GS_SET_TEX0(key, 10, 0, 10, 9, 1, 0, 0, 0, 0, 0, 0);
    fixture.Frame({0, 0, 0, 128}, [&] { DrawFullSizePicture(&texture, 0, 0, 128); });
    EXPECT_TRUE(fixture.PixelNear(80, 360, 112, 13, 255));
    EXPECT_TRUE(fixture.PixelNear(1200, 360, 112, 200, 255));
    EXPECT_TRUE(fixture.PixelNear(640, 360, 112, 106, 255));
    EXPECT_TRUE(fixture.PixelNear(640, 684, 212, 106, 255));
    PortReleaseKey(key);
}

TEST(ChroniclePresentation, ClearedVisualBlocksAreRejectedBetweenSweeps) {
    alignas(16) unsigned int block[16] = {};
    Draw3DRegisterVisual(block, false);
    ASSERT_NE(Draw3DFindVisual(block), nullptr);
    block[8] = 0;
    EXPECT_EQ(Draw3DFindVisual(block), nullptr);
}

TEST(ChroniclePresentation, AsyncPublishesAndStopsTheWorker) {
    dc::test::GfxFixture fixture(64, 48, 1.0f,
                                 dc::test::GfxOptions{.offscreen = true, .render_thread = gfx::RenderThreadMode::Async});
    ClockSetUnbounded(false);
    ClockSetTickRate(60);
    ClockReset();
    GameSetPresentSettings({});
    const auto before = GamePresentStatistics().display_frames;
    {
        gfx::RenderLease lease;
        gfx::BeginRecording();
        const uint8_t color[] = {20, 40, 60, 128};
        gfx::Clear(true, color, true, 0);
        GameRenderTick(gfx::EndRecording());
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (GamePresentStatistics().display_frames == before && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    GameStopPresentation();
    EXPECT_GT(GamePresentStatistics().display_frames, before);
    const auto stopped = GamePresentStatistics().display_frames;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_EQ(GamePresentStatistics().display_frames, stopped);
}

TEST(ChroniclePresentation, AdjacentMatchingStripsBatchWithoutReorderingMaterials) {
    dc::test::GfxFixture fixture(64, 48, 1.0f, dc::test::GfxOptions{.offscreen = true});
    Draw3DVisual         visual;
    visual.immediate = true;
    visual.vertices.resize(3);
    visual.indices = {0, 1, 2, 0, 1, 2, 0, 1, 2, 0, 1, 2};
    for (uint32_t first = 0; first < 12; first += 3) {
        Draw3DStrip strip;
        strip.first_index = first;
        strip.index_count = 3;
        if (first == 6) {
            strip.diffuse[0] = 0.5f;
        }
        visual.strips.push_back(strip);
    }
    RenderInfo info;
    std::memset(&info, 0, sizeof(info));
    float model[4][4] = {};
    for (int axis = 0; axis < 4; ++axis) {
        model[axis][axis] = 1;
        info.view_scaled[axis][axis] = 1;
    }
    info.scale[0] = info.scale[1] = 1;
    gfx::BeginRecording();
    Draw3DDrawVisual(visual, model, info, 0);
    const auto list = gfx::EndRecording();
    EXPECT_EQ(gfx::ListStats(*list).mesh_draws, 3u);
    std::vector<uint32_t> counts;
    for (const auto &entry : list->entries) {
        if (const auto *mesh = std::get_if<gfx::detail::MeshEntry>(&entry)) {
            counts.push_back(mesh->index_count);
        }
    }
    EXPECT_EQ(counts, (std::vector<uint32_t>{6, 3, 3}));
}
