// nlohmann/json before the game headers, whose one-letter macros it cannot stand.
#include "area_card.hpp"
#include "textwipe.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include "caption_text.hpp"
#include "gfx/gfx.hpp"
#include "localize.hpp"
#include "localize_texture.hpp"
#include "platform/imagefile.hpp"
#include "texture_port.hpp"

namespace areacard {

namespace {

struct Entry {
    const char *card;
    const char *english;
};

// The US card's words, which every language falls back to.
constexpr Entry kCards[] = {
    {"mt01",  "NORUNE VILLAGE"   },
    {"mt02",  "MATATAKI VILLAGE" },
    {"mt03",  "QUEENS"           },
    {"mt04",  "MUSKA RACKA"      },
    {"mt05",  "MOON FACTORY"     },
    {"mt101", "DIVINE BEAST CAVE"},
    {"mt102", "WISE OWL FOREST"  },
    {"mt103", "SHIPWRECK"        },
    {"mt104", "SUN&MOON TEMPLE"  },
    {"mt105", "MOON SEA"         },
    {"mt106", "GALLERY OF TIME"  },
    {"mt107", "DEMON SHAFT"      },
    {"mt14",  "BROWNBOO VILLAGE" },
    {"mt23",  "YELLOW DROPS"     },
    {"mt301", "DARK SHRINE"      },
    {"mt40",  "DARK HEAVEN CASTLE"},
};

// Where the lettering goes, as fractions of the card, and how it looks; read from the plate's json.
struct Layout {
    std::string       text;
    float             cap_top = 0.27f;
    float             cap_height = 0.42f;
    float             xsq = 0.56f;
    float             max_width = 0.9375f;
    bool              underline = true; // the brush stroke under the name
    float             underline_y = 0.7656f;
    float             underline_thick = 0.0547f;
    float             underline_pad_left = 0.0117f;
    float             underline_pad_right = 0.0169f;
    float             underline_bow = 0.0078f;
    float             underline_wave = 0.0420f; // height of the tilde in the ribbon, of the card's height
    bool              drips = false;
    caption::LineStyle style;
    float             edge_width = 0.0039f; // of the card's width
    float             glow_width = 0.013f;
};

std::optional<Layout> g_card;

bool Colour(const nlohmann::json &json, const char *key, unsigned char (&out)[3]) {
    if (!json.contains(key) || !json[key].is_array() || json[key].size() != 3) {
        return false;
    }
    for (int i = 0; i < 3; i++) {
        out[i] = static_cast<unsigned char>(std::clamp(json[key][i].get<int>(), 0, 255));
    }
    return true;
}

float Number(const nlohmann::json &json, const char *key, float fallback) {
    return json.contains(key) && json[key].is_number() ? json[key].get<float>() : fallback;
}

// Strokes in the card's own fractions become logical units by the card's drawn size.
struct Frame {
    float x0, y0, w, h;
    float X(float fx) const { return x0 + fx * w; }
    float Y(float fy) const { return y0 + fy * h; }
};

// The ribbon under the name. Its centre line is one gentle tilde (a sine wave) across the lettering's own width, so it
// stretches to fit any name; its thickness swells in the middle and tapers to fine points. The mesh is cut finely along
// its length (kSteps) so the curves are smooth, and shaded across it (light on top, dark below) so it reads as a ribbon
// rather than a flat bar. `grow` widens it all round (the outline); `feather` adds a soft outer edge fading to nothing,
// which stands in for anti-aliasing.
void Ribbon(std::vector<gfx::Vertex2D> &out, const Frame &frame, const Layout &layout, float left, float right, float grow,
            float feather, const unsigned char (&top_rgb)[3], const unsigned char (&mid_rgb)[3], const unsigned char (&low_rgb)[3],
            float fade) {
    constexpr int   kSteps = 96;
    constexpr float kPi = 3.14159265f;
    const float     k = std::clamp(fade, 0.0f, 1.0f);
    const float     aspect = frame.w / frame.h;
    const float     amplitude = std::max(layout.underline_wave, 0.0f);
    auto            smooth = [](float x) { x = std::clamp(x, 0.0f, 1.0f); return x * x * (3 - 2 * x); };
    auto            shade = [&](const unsigned char (&rgb)[3], float alpha) {
        return std::array<uint8_t, 4>{rgb[0], rgb[1], rgb[2], static_cast<uint8_t>(std::clamp(alpha * k, 0.0f, 1.0f) * 0x80 + 0.5f)};
    };
    auto        point = [&](int i, float at, const unsigned char (&rgb)[3], float alpha, float extra) {
        const float t = static_cast<float>(i) / kSteps;
        const float x = left - grow + (right - left + 2 * grow) * t;
        const float taper = smooth(t / 0.16f) * smooth((1 - t) / 0.16f);
        const float body = 0.62f + 0.38f * std::sin(kPi * t);
        const float half = layout.underline_thick * 0.5f * body * taper;
        const float cy = layout.underline_y - amplitude * std::sin(2 * kPi * t - 0.35f);
        const float y = cy + at * (half + (grow + extra) * aspect);
        const auto  c = shade(rgb, alpha);
        return gfx::Vertex2D{frame.X(x), frame.Y(y), 0.0f, 0.0f, 0.0f, {c[0], c[1], c[2], c[3]}};
    };
    auto band = [&](float at0, const unsigned char (&rgb0)[3], float a0, float e0, float at1, const unsigned char (&rgb1)[3], float a1,
                    float e1) {
        for (int i = 0; i < kSteps; i++) {
            out.push_back(point(i, at0, rgb0, a0, e0));
            out.push_back(point(i + 1, at0, rgb0, a0, e0));
            out.push_back(point(i + 1, at1, rgb1, a1, e1));
            out.push_back(point(i, at1, rgb1, a1, e1));
        }
    };
    if (feather > 0.0f) {
        // Soft rim: opaque at the edge, clear a feather's width outside it, on both sides.
        band(-1.0f, top_rgb, 0.0f, feather, -1.0f, top_rgb, 1.0f, 0.0f);
        band(1.0f, low_rgb, 1.0f, 0.0f, 1.0f, low_rgb, 0.0f, feather);
    }
    // Body: light edge, true colour at the middle of the upper half, dark underside.
    band(-1.0f, top_rgb, 1.0f, 0.0f, -0.15f, mid_rgb, 1.0f, 0.0f);
    band(-0.15f, mid_rgb, 1.0f, 0.0f, 1.0f, low_rgb, 1.0f, 0.0f);
}

void Mix(unsigned char (&out)[3], const unsigned char (&a)[3], const unsigned char (&b)[3], float t) {
    for (int i = 0; i < 3; i++) {
        out[i] = static_cast<unsigned char>(a[i] + (b[i] - a[i]) * t + 0.5f);
    }
}

} // namespace

std::vector<std::pair<std::string, std::string>> Strings() {
    std::vector<std::pair<std::string, std::string>> out;
    for (const Entry &entry : kCards) {
        out.emplace_back(std::string("area.") + entry.card, entry.english);
    }
    return out;
}

void Clear() {
    g_card.reset();
}

bool Prepare(const std::string &name, PortDecodedTexture &texture) {
    g_card.reset();
    const Entry *entry = nullptr;
    for (const Entry &candidate : kCards) {
        if (name == candidate.card) {
            entry = &candidate;
        }
    }
    if (entry == nullptr || !caption::Ready() || texture.width == 0 || texture.height == 0) {
        return false;
    }
    const std::filesystem::path plate = LocalizeFindFile("textures/plates/" + name + ".png");
    const std::filesystem::path sheet = LocalizeFindFile("textures/plates/" + name + ".json");
    if (plate.empty() || sheet.empty()) {
        return false;
    }
    Layout layout;
    try {
        std::ifstream stream(sheet);
        const nlohmann::json json = nlohmann::json::parse(stream);
        const nlohmann::json &lay = json.contains("layout") ? json["layout"] : json;
        layout.cap_top = Number(lay, "cap_top", layout.cap_top);
        layout.cap_height = Number(lay, "cap_height", layout.cap_height);
        layout.xsq = Number(lay, "xsq", layout.xsq);
        layout.max_width = Number(lay, "max_width", layout.max_width);
        layout.underline = Number(lay, "underline", 1.0f) != 0.0f;
        layout.underline_y = Number(lay, "underline_y", layout.underline_y);
        layout.underline_thick = Number(lay, "underline_thick", layout.underline_thick);
        layout.underline_pad_left = Number(lay, "underline_pad_left", layout.underline_pad_left);
        layout.underline_pad_right = Number(lay, "underline_pad_right", layout.underline_pad_right);
        layout.underline_bow = Number(lay, "underline_bow", layout.underline_bow);
        layout.underline_wave = Number(lay, "underline_wave", layout.underline_wave);
        const nlohmann::json &look = json.contains("style") ? json["style"] : json;
        Colour(look, "fill", layout.style.fill);
        Colour(look, "edge", layout.style.edge);
        Colour(look, "glow", layout.style.glow);
        layout.style.glow_amount = Number(look, "glow_amount", 0.0f);
        layout.edge_width = Number(look, "edge_width", layout.edge_width);
        layout.glow_width = Number(look, "glow_width", layout.glow_width);
        layout.drips = look.contains("drips") && look["drips"].is_number() && look["drips"].get<int>() != 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "areacard: cannot read %s: %s\n", sheet.string().c_str(), error.what());
        return false;
    }

    int                  width = 0;
    int                  height = 0;
    std::vector<uint8_t> rgba;
    if (!imagefile::LoadPng(plate, width, height, rgba)) {
        std::fprintf(stderr, "areacard: cannot read picture %s\n", plate.string().c_str());
        return false;
    }
    std::vector<std::vector<uint8_t>> levels;
    for (size_t level = 0; level < std::max<size_t>(1, texture.levels.size()); level++) {
        const int w = std::max<int>(1, static_cast<int>(texture.width >> level));
        const int h = std::max<int>(1, static_cast<int>(texture.height >> level));
        levels.push_back(LocalizeResample(rgba.data(), width, height, w, h));
    }
    texture.levels = std::move(levels);
    texture.format = gfx::TextureFormat::Rgba8;
    texture.has_alpha = false;
    texture.four_bit = false;
    layout.text = LocalizeText(std::string("area.") + name, entry->english);
    g_card = std::move(layout);
    return true;
}

bool Prepared() {
    return g_card.has_value();
}

void Draw(float x0, float y0, float x1, float y1, float fade) {
    if (!g_card || fade <= 0.0f) {
        return;
    }
    Layout        &layout = *g_card;
    const Frame    frame = {x0, y0, x1 - x0, y1 - y0};
    caption::LineStyle style = layout.style;
    style.edge_width = layout.edge_width * frame.w;
    style.glow_width = layout.glow_width * frame.w;

    // The letters first; the underline is placed from where they ended, and lies clear of them below.
    const textwipe::Tuning &tune = textwipe::Get(textwipe::Kind::Card);
    style.letter = tune.letter;
    style.word = tune.word;
    style.xscale = tune.xscale;
    style.yscale = tune.yscale;
    style.dx = tune.dx;
    style.dy = tune.dy;
    style.weight = tune.weight;
    style.opacity = tune.opacity;
    const float cap_top = frame.Y(layout.cap_top);
    const float cap_height = layout.cap_height * frame.h * tune.size;
    caption::LineBounds span;
    if (!caption::DrawLine(layout.text, frame.X(0.5f), cap_top, cap_height, layout.xsq, layout.max_width * frame.w, style,
                           fade, 1.0f, &span)) {
        return;
    }
    const float left = (span.left - frame.x0) / frame.w - layout.underline_pad_left;
    const float right = (span.right - frame.x0) / frame.w + layout.underline_pad_right;
    const float grow = layout.edge_width;
    std::vector<gfx::Vertex2D> outline, body;
    if (layout.underline) {
        // Outline (with a feathered rim) first, then the shaded body inside it.
        const float feather = std::max(frame.w * 0.0022f, 0.9f) / frame.w;
        unsigned char light[3], dark[3], white[3] = {255, 255, 255}, black[3] = {0, 0, 0};
        Mix(light, style.fill, white, 0.45f);
        Mix(dark, style.fill, black, 0.30f);
        Ribbon(outline, frame, layout, left, right, grow, feather, style.edge, style.edge, style.edge, fade);
        Ribbon(body, frame, layout, left, right, 0.0f, 0.0f, light, style.fill, dark, fade);
    }
    if (layout.drips) {
        // Hanging drops under the underline (Dark Heaven Castle).
        const float k = std::clamp(fade, 0.0f, 1.0f);
        for (int pass = 0; pass < 2; pass++) {
            const unsigned char (&rgb)[3] = pass == 0 ? style.edge : style.fill;
            const float          fat = pass == 0 ? grow : 0.0f;
            const u_char         r = static_cast<u_char>(rgb[0] * k), g = static_cast<u_char>(rgb[1] * k), b = static_cast<u_char>(rgb[2] * k);
            std::vector<gfx::Vertex2D> &out = pass == 0 ? outline : body;
            for (int drop = 0; drop < 7; drop++) {
                const float x = (span.left - frame.x0) / frame.w + (static_cast<float>(drop) + 0.5f) * (span.right - span.left) / frame.w / 7;
                const float y = layout.underline_y + 0.0195f;
                const float length = 0.0898f * (0.7f + 0.3f * static_cast<float>((drop * 7) % 3) / 2);
                const float half = 0.0059f + fat;
                auto        vertex = [&](float px, float py) { return gfx::Vertex2D{frame.X(px), frame.Y(py), 0.0f, 0.0f, 0.0f, {r, g, b, 0x80}}; };
                out.push_back(vertex(x - half, y - fat));
                out.push_back(vertex(x + half, y - fat));
                out.push_back(vertex(x, y + length + fat));
                out.push_back(vertex(x, y + length + fat));
            }
        }
    }
    gfx::DrawState blended;
    blended.blend = true;
    gfx::Draw2D(gfx::Primitive::Quads, outline, {}, blended);
    gfx::Draw2D(gfx::Primitive::Quads, body, {}, blended);
}

} // namespace areacard
