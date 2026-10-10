#include "textwipe.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "gfx/gfx.hpp"
#include "platform/input.hpp"
#include "platform/overlay.hpp"

namespace textwipe {

namespace {

constexpr double kSweepSeconds = 4.0; // one way
constexpr double kRestSeconds = 0.8;  // at each end

bool                                   g_on = false;
Mode                                   g_mode = Mode::Sweep;
std::chrono::steady_clock::time_point  g_started;
float                                  g_divider = gfx::kLogicalWidth;

float Smooth(double t) {
    t = std::clamp(t, 0.0, 1.0);
    return static_cast<float>(t * t * (3.0 - 2.0 * t));
}

const char *ModeName() {
    switch (g_mode) {
    case Mode::Sweep:
        return "SWEEP";
    case Mode::ShowBefore:
        return "ALL BEFORE";
    case Mode::ShowAfter:
        return "ALL AFTER";
    }
    return "";
}

} // namespace

float SweepPosition(double seconds) {
    // Rest at the right (all old), slide left to reveal the new, rest, slide back.
    const double cycle = 2.0 * (kSweepSeconds + kRestSeconds);
    double       t = std::fmod(std::max(0.0, seconds), cycle);
    if (t < kRestSeconds) {
        return gfx::kLogicalWidth;
    }
    t -= kRestSeconds;
    if (t < kSweepSeconds) {
        return gfx::kLogicalWidth * (1.0f - Smooth(t / kSweepSeconds));
    }
    t -= kSweepSeconds;
    if (t < kRestSeconds) {
        return 0.0f;
    }
    t -= kRestSeconds;
    return gfx::kLogicalWidth * Smooth(t / kSweepSeconds);
}

void Update() {
    if (InputDevKeyPressed("F9")) {
        g_on = !g_on;
        g_started = std::chrono::steady_clock::now();
        g_mode = Mode::Sweep;
        std::fprintf(stderr, "textwipe: %s\n", g_on ? "on (F10 steps the mode)" : "off");
    }
    if (InputDevKeyPressed("F10") && g_on) {
        g_mode = g_mode == Mode::Sweep ? Mode::ShowBefore : g_mode == Mode::ShowBefore ? Mode::ShowAfter : Mode::Sweep;
        g_started = std::chrono::steady_clock::now();
        std::fprintf(stderr, "textwipe: mode %s\n", ModeName());
    }
    if (!g_on) {
        return;
    }
    switch (g_mode) {
    case Mode::Sweep:
        g_divider = SweepPosition(std::chrono::duration<double>(std::chrono::steady_clock::now() - g_started).count());
        break;
    case Mode::ShowBefore:
        g_divider = gfx::kLogicalWidth;
        break;
    case Mode::ShowAfter:
        g_divider = 0.0f;
        break;
    }
}

bool Active() {
    return g_on;
}

float Divider() {
    return g_divider;
}

void Both(const std::function<void(bool runtime)> &draw) {
    if (!g_on) {
        draw(true);
        return;
    }
    const float edge = std::clamp(g_divider, 0.0f, gfx::kLogicalWidth);
    // A clip touching the logical frame's edge reaches the window's edge past it (gfx's scissors do), so a wide window is split too.
    if (edge > 0.0f) {
        const gfx::LogicalRect left = {0.0f, 0.0f, edge, gfx::kLogicalHeight};
        gfx::SetDrawClip(&left);
        draw(false);
    }
    if (edge < gfx::kLogicalWidth) {
        const gfx::LogicalRect right = {edge, 0.0f, gfx::kLogicalWidth - edge, gfx::kLogicalHeight};
        gfx::SetDrawClip(&right);
        draw(true);
    }
    gfx::SetDrawClip(nullptr);
}

void DrawDivider() {
    if (!g_on || !(gfx::Recording() || gfx::InFrame())) {
        return;
    }
    const gfx::LogicalMapping mapping = gfx::GetUiMapping(gfx::kMainTarget);
    const int                 pixel = OverlayPixelSize(gfx::GetLogicalMapping(gfx::kMainTarget));
    const float               x = std::clamp(g_divider, 0.0f, gfx::kLogicalWidth);
    const float               half = 1.0f / std::max(0.5f, mapping.scale_x) * static_cast<float>(pixel);
    auto                      vertex = [](float vx, float vy, unsigned char grey) {
        return gfx::Vertex2D{vx, vy, 0.0f, 0.0f, 0.0f, {grey, grey, grey, 0x80}};
    };
    gfx::DrawState state;
    state.blend = true;
    std::vector<gfx::Vertex2D> line;
    for (const auto &[w, grey] : {std::pair{2.5f, static_cast<unsigned char>(0)}, std::pair{1.0f, static_cast<unsigned char>(0x7F)}}) {
        const float h = half * w;
        line.push_back(vertex(x - h, 0.0f, grey));
        line.push_back(vertex(x + h, 0.0f, grey));
        line.push_back(vertex(x + h, gfx::kLogicalHeight, grey));
        line.push_back(vertex(x - h, gfx::kLogicalHeight, grey));
    }
    gfx::Draw2D(gfx::Primitive::Quads, line, {}, state);

    // The labels sit either side of the line near the bottom, clear of the FPS badge at the top left.
    const std::string before = "BEFORE: BAKED PICTURE";
    const std::string after = std::string("AFTER: RUNTIME TEXT (") + ModeName() + ")";
    const int         y = mapping.pixel_height - (kOverlayGlyphHeight + 4 * kOverlayPadding) * pixel;
    const int         gap = 6 * pixel;
    const int         line_px = static_cast<int>(std::lround(x * mapping.scale_x + mapping.offset_x));
    const int         before_width = static_cast<int>(before.size()) * kOverlayAdvance * pixel;
    OverlayDrawText(before, mapping, pixel, std::max(gap, line_px - gap - before_width), y);
    OverlayDrawText(after, mapping, pixel, std::min(line_px + gap, static_cast<int>(mapping.pixel_width) - gap - static_cast<int>(after.size()) * kOverlayAdvance * pixel), y);
}

} // namespace textwipe
