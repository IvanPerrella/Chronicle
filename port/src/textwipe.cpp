#include "textwipe.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "gfx/gfx.hpp"
#include "platform/input.hpp"
#include "platform/overlay.hpp"
#include "platform/paths.hpp"

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
    case Mode::Overlay:
        return "OVERLAY";
    }
    return "";
}

std::array<Tuning, static_cast<size_t>(Kind::Count)> g_tuning;
bool                                                 g_tuning_loaded = false;
int                                                  g_kind = 0;
int                                                  g_value = 0;

constexpr const char *kKindNames[] = {"message", "caption", "card", "options"};
constexpr const char *kKindLabels[] = {"MESSAGES", "MENU CAPTIONS", "AREA CARDS", "OPTIONS SCREEN"};

Kind g_context = Kind::Message;

struct Value {
    const char *key;
    const char *label;
    float Tuning::*field;
    float           step;
    float           low;
    float           high;
};

constexpr Value kValues[] = {
    {"size",    "SIZE",           &Tuning::size,    0.01f, 0.2f,   3.0f },
    {"xscale",  "WIDTH",          &Tuning::xscale,  0.01f, 0.2f,   3.0f },
    {"yscale",  "HEIGHT",         &Tuning::yscale,  0.01f, 0.2f,   3.0f },
    {"letter",  "LETTER SPACING", &Tuning::letter,  0.01f, -10.0f, 10.0f},
    {"word",    "WORD SPACING",   &Tuning::word,    0.01f, -20.0f, 40.0f},
    {"dx",      "MOVE RIGHT",     &Tuning::dx,      0.01f, -40.0f, 40.0f},
    {"dy",      "MOVE DOWN",      &Tuning::dy,      0.01f, -40.0f, 40.0f},
    {"weight",  "WEIGHT (BOLDER)", &Tuning::weight, 0.01f, 0.0f,   3.0f },
    {"opacity", "OPACITY",        &Tuning::opacity, 0.01f, 0.0f,   1.0f },
    {"shadow",  "SHADOW",         &Tuning::shadow,  0.01f, 0.0f,   3.0f },
    {"align",   "ALIGN IN CELLS", &Tuning::align,   0.01f, 0.0f,   1.0f },
    {"lock",    "CELL LOCK (NO DRIFT)", &Tuning::lock, 0.01f, 0.0f, 1.0f},
    {"fit",     "FIT TO CELLS",   &Tuning::fit,     1.0f,  0.0f,   1.0f },
};
constexpr int kValueCount = static_cast<int>(std::size(kValues));

std::filesystem::path TuningFile() {
    return PathsSaveRoot() / "lang" / "textwipe_tuning.json";
}

void SaveTuning() {
    nlohmann::json json = nlohmann::json::object();
    for (size_t k = 0; k < g_tuning.size(); k++) {
        const Tuning locked = Locked(static_cast<Kind>(k));
        for (const Value &value : kValues) {
            const float now = std::round(g_tuning[k].*(value.field) * 1000.0f) / 1000.0f;
            if (std::fabs(now - locked.*(value.field)) > 0.0005f) {
                json[kKindNames[k]][value.key] = now;
            }
        }
    }
    std::error_code error;
    std::filesystem::create_directories(TuningFile().parent_path(), error);
    std::ofstream out(TuningFile());
    if (out) {
        out << json.dump(2) << "\n";
    }
}

void LoadTuning() {
    g_tuning_loaded = true;
    for (size_t k = 0; k < g_tuning.size(); k++) {
        g_tuning[k] = Locked(static_cast<Kind>(k));
    }
    std::ifstream in(TuningFile());
    if (!in) {
        return;
    }
    try {
        const nlohmann::json json = nlohmann::json::parse(in);
        for (size_t k = 0; k < g_tuning.size(); k++) {
            if (!json.contains(kKindNames[k])) {
                continue;
            }
            const nlohmann::json &entry = json[kKindNames[k]];
            for (const Value &value : kValues) {
                if (entry.contains(value.key) && entry[value.key].is_number()) {
                    g_tuning[k].*(value.field) = std::clamp(entry[value.key].get<float>(), value.low, value.high);
                }
            }
        }
    } catch (const std::exception &error) {
        std::fprintf(stderr, "textwipe: cannot read %s: %s\n", TuningFile().string().c_str(), error.what());
    }
}

// One press of a key, then repeats while it is held (after a short wait).
struct Repeater {
    bool                                  down = false;
    int                                   fired = 0;
    std::chrono::steady_clock::time_point next;
    // How many steps this press is worth: one, then more the longer it is held.
    int Fire(bool held) {
        const auto now = std::chrono::steady_clock::now();
        if (!held) {
            down = false;
            fired = 0;
            return 0;
        }
        if (!down) {
            down = true;
            fired = 1;
            next = now + std::chrono::milliseconds(400);
            return 1;
        }
        if (now >= next) {
            next = now + std::chrono::milliseconds(50);
            fired++;
            return fired > 40 ? 10 : fired > 15 ? 3 : 1;
        }
        return 0;
    }
};

Repeater g_minus;
Repeater g_plus;

void Adjust(int direction, int steps) {
    const bool   shift = InputDevKeyHeld("Left Shift") || InputDevKeyHeld("Right Shift");
    const bool   ctrl = InputDevKeyHeld("Left Ctrl") || InputDevKeyHeld("Right Ctrl");
    const Value &value = kValues[g_value];
    float       &field = g_tuning[static_cast<size_t>(g_kind)].*(value.field);
    if (value.field == &Tuning::fit) {
        field = direction > 0 ? 1.0f : 0.0f;
    } else {
        const float step = value.step * static_cast<float>(steps) * (shift ? 10.0f : ctrl ? 0.1f : 1.0f);
        const float grid = value.step * 0.1f;
        field = std::clamp(std::round((field + static_cast<float>(direction) * step) / grid) * grid, value.low, value.high);
    }
    SaveTuning();
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
        g_mode = g_mode == Mode::Sweep        ? Mode::ShowBefore
                 : g_mode == Mode::ShowBefore ? Mode::ShowAfter
                 : g_mode == Mode::ShowAfter  ? Mode::Overlay
                                              : Mode::Sweep;
        g_started = std::chrono::steady_clock::now();
        std::fprintf(stderr, "textwipe: mode %s\n", ModeName());
    }
    if (!g_on) {
        return;
    }
    if (InputDevKeyPressed("F11")) {
        g_kind = (g_kind + 1) % static_cast<int>(Kind::Count);
    }
    if (InputDevKeyPressed("F12")) {
        const bool back = InputDevKeyHeld("Left Shift") || InputDevKeyHeld("Right Shift");
        g_value = (g_value + (back ? kValueCount - 1 : 1)) % kValueCount;
    }
    if (InputDevKeyPressed("F8")) {
        g_tuning[static_cast<size_t>(g_kind)] = Locked(static_cast<Kind>(g_kind));
        SaveTuning();
    }
    if (const int steps = g_minus.Fire(InputDevKeyHeld("-")); steps > 0) {
        Adjust(-1, steps);
    }
    if (const int steps = g_plus.Fire(InputDevKeyHeld("=")); steps > 0) {
        Adjust(1, steps);
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
    case Mode::Overlay:
        g_divider = gfx::kLogicalWidth;
        break;
    }
}

Tuning Locked(Kind kind) {
    Tuning t;
    switch (kind) {
    case Kind::Message: // dialogue, locked in 2026-10-09 from Jeff's overlay
        t.size = 1.15f;
        t.xscale = 0.80f;
        t.yscale = 1.02f;
        t.dx = 0.35f;
        t.align = 1.0f;
        t.lock = 1.0f;
        break;
    case Kind::Options:
        t.size = 0.76f;
        t.yscale = 1.17f;
        t.dx = -1.10f;
        t.dy = -0.15f;
        t.lock = 1.0f;
        break;
    case Kind::Card: // fit on: the name fills the box the disc's painted one did (area_card.cpp), so no hand fit is needed
        break;
    case Kind::Caption:
    case Kind::Count:
        break;
    }
    return t;
}

Kind MessageKind() {
    return g_context;
}

MessageContext::MessageContext(Kind kind) : previous_(g_context) {
    g_context = kind;
}

MessageContext::~MessageContext() {
    g_context = previous_;
}

const Tuning &Get(Kind kind) {
    if (!g_tuning_loaded) {
        LoadTuning();
    }
    return g_tuning[static_cast<size_t>(kind)];
}

bool Overlaying() {
    return g_on && g_mode == Mode::Overlay;
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
    if (g_mode == Mode::Overlay) {
        // Both in full: the old in red, the new in cyan, so where they agree the two blend and where they do not a colour shows.
        static const float old_tint[4] = {1.0f, 0.25f, 0.25f, 0.75f};
        static const float new_tint[4] = {0.3f, 1.0f, 1.0f, 0.75f};
        gfx::SetDrawTint(&old_tint);
        draw(false);
        gfx::SetDrawTint(&new_tint);
        draw(true);
        gfx::SetDrawTint(nullptr);
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
    if (g_mode != Mode::Overlay) {
        gfx::Draw2D(gfx::Primitive::Quads, line, {}, state);
    }

    // The labels sit either side of the line near the bottom, clear of the FPS badge at the top left.
    const bool        overlay = g_mode == Mode::Overlay;
    const std::string before = overlay ? "RED: BAKED PICTURE" : "BEFORE: BAKED PICTURE";
    const std::string after = overlay ? std::string("CYAN: RUNTIME TEXT") : std::string("AFTER: RUNTIME TEXT (") + ModeName() + ")";
    const int         y = mapping.pixel_height - (kOverlayGlyphHeight + 4 * kOverlayPadding) * pixel;
    const int         gap = 6 * pixel;
    const int         line_px = static_cast<int>(std::lround(x * mapping.scale_x + mapping.offset_x));
    const int         before_width = static_cast<int>(before.size()) * kOverlayAdvance * pixel;
    OverlayDrawText(before, mapping, pixel, std::max(gap, line_px - gap - before_width), y);
    OverlayDrawText(after, mapping, pixel, std::min(line_px + gap, static_cast<int>(mapping.pixel_width) - gap - static_cast<int>(after.size()) * kOverlayAdvance * pixel), y);

    // The tuning panel, under the FPS badge.
    const Tuning &tuning = Get(static_cast<Kind>(g_kind));
    int           row_y = 16 * pixel;
    const int     row_step = (kOverlayGlyphHeight + 2 * kOverlayPadding + 1) * pixel;
    auto          line_of = [&](const std::string &text) {
        OverlayDrawText(text, mapping, pixel, 2 * pixel, row_y);
        row_y += row_step;
    };
    line_of(std::string("TUNING ") + kKindLabels[g_kind] + "  F11 SET  F12 VALUE  -/= CHANGE  F8 RESET");
    for (int v = 0; v < kValueCount; v++) {
        std::string number;
        if (kValues[v].field == &Tuning::fit) {
            number = tuning.fit >= 0.5f ? "ON" : "OFF";
        } else {
            char text[32];
            std::snprintf(text, sizeof text, "%.3f", tuning.*(kValues[v].field));
            number = text;
        }
        line_of(std::string(v == g_value ? "> " : "  ") + kValues[v].label + " " + number);
    }
}

} // namespace textwipe
