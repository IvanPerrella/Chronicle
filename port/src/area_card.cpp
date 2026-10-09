// nlohmann/json before the game headers, whose one-letter macros it cannot stand.
#include "area_card.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
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
    float             underline_y = 0.7656f;
    float             underline_thick = 0.0547f;
    float             underline_pad_left = 0.0117f;
    float             underline_pad_right = 0.0169f;
    float             underline_bow = 0.0078f;
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

void Stroke(std::vector<gfx::Vertex2D> &out, const Frame &frame, const Layout &layout, float left, float right, float grow,
            const unsigned char (&rgb)[3], float fade) {
    // The brush underline: a slim stroke, heaviest toward the left, bowed a little downward in the middle.
    constexpr int kSteps = 48;
    const float   k = std::clamp(fade, 0.0f, 1.0f);
    const u_char  r = static_cast<u_char>(rgb[0] * k), g = static_cast<u_char>(rgb[1] * k), b = static_cast<u_char>(rgb[2] * k);
    auto          point = [&](int i, float side) {
        const float t = static_cast<float>(i) / kSteps;
        const float x = left - grow + (right - left + 2 * grow) * t;
        const float thick = layout.underline_thick * (0.55f + 0.45f * std::sin(3.14159265f * std::clamp(t * 0.85f + 0.05f, 0.0f, 1.0f)));
        const float y = layout.underline_y + layout.underline_bow * std::sin(3.14159265f * t) + side * (thick / 2 + grow * frame.w / frame.h);
        return gfx::Vertex2D{frame.X(x), frame.Y(y), 0.0f, 0.0f, 0.0f, {r, g, b, 0x80}};
    };
    for (int i = 0; i < kSteps; i++) {
        out.push_back(point(i, -1));
        out.push_back(point(i + 1, -1));
        out.push_back(point(i + 1, 1));
        out.push_back(point(i, 1));
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
        layout.underline_y = Number(lay, "underline_y", layout.underline_y);
        layout.underline_thick = Number(lay, "underline_thick", layout.underline_thick);
        layout.underline_pad_left = Number(lay, "underline_pad_left", layout.underline_pad_left);
        layout.underline_pad_right = Number(lay, "underline_pad_right", layout.underline_pad_right);
        layout.underline_bow = Number(lay, "underline_bow", layout.underline_bow);
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
    const float cap_top = frame.Y(layout.cap_top);
    const float cap_height = layout.cap_height * frame.h;
    caption::LineBounds span;
    if (!caption::DrawLine(layout.text, frame.X(0.5f), cap_top, cap_height, layout.xsq, layout.max_width * frame.w, style,
                           fade, 1.0f, &span)) {
        return;
    }
    const float left = (span.left - frame.x0) / frame.w - layout.underline_pad_left;
    const float right = (span.right - frame.x0) / frame.w + layout.underline_pad_right;
    const float grow = layout.edge_width;
    std::vector<gfx::Vertex2D> outline, body;
    Stroke(outline, frame, layout, left, right, grow, style.edge, fade);
    Stroke(body, frame, layout, left, right, 0.0f, style.fill, fade);
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
