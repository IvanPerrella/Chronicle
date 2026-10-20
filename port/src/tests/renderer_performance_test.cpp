#include <SDL3/SDL.h>
#include <gtest/gtest.h>

#include <chrono>
#include <cstring>
#include <thread>

#include "draw3d.hpp"
#include "gameloop.hpp"
#include "gfx/displaylist.hpp"
#include "gfx_fixture.hpp"
#include "platform/clock.hpp"
#include "renderinfo.hpp"

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
