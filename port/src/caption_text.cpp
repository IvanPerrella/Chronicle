#include "caption_text.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>

#include "draw2d_port.hpp"
#include "gfx/gfx.hpp"
#include "localize.hpp"
#include "menu_draw.hpp"
#include "platform/ttffont.hpp"
#include "texture_port.hpp"

namespace caption {

namespace {

struct Entry {
    const char *key;
    const char *english;
};

// By icon number, the order of MenuIcon in menu_draw.cpp.
constexpr Entry kEntries[kIcons] = {
    {"menu.caption.item",           "ITEM"              },
    {"menu.caption.weapon",         "WEAPON"            },
    {"menu.caption.allies",         "ALLIES"            },
    {"menu.caption.georama_parts",  "GEORAMA PARTS"     },
    {"menu.caption.option",         "OPTION"            },
    {"menu.caption.save",           "SAVE"              },
    {"menu.caption.manuals",        "MANUALS"           },
    {"menu.caption.leave_dungeon",  "LEAVE DUNGEON"     },
    {"menu.caption.other_area",     "GO TO OTHER AREA"  },
    {"menu.caption.go_dungeon",     "GO TO DUNGEON"     },
    {"menu.caption.move_remove",    "MOVE/REMOVE"       },
    {"menu.caption.georama_analysis", "GEORAMA ANALYSIS"},
    {"menu.caption.go_outside",     "GO OUTSIDE"        },
    {"menu.caption.config_assembly", "CONFIG/ASSEMBLY"  },
};

// The font's em, in logical pixels, a caption is set at: the painted ones were about 21 pixels from the
// top of their capitals to the bottom, in a 30-pixel cell.
constexpr float kEm = 25.0f;
// The outline's width in logical pixels, and the least a caption shrinks to before it is condensed.
constexpr float kOutlineWidth = 1.6f;
constexpr float kLeastShrink = 0.62f;

bool FontReady() {
    static bool tried = false;
    if (!tried) {
        tried = true;
        if (!ttffont::Loaded()) {
            std::filesystem::path file = LocalizeFindFile("font.ttf");
            if (!file.empty()) {
                ttffont::Load(file);
            }
        }
    }
    return ttffont::Loaded();
}

std::u32string Decode(std::string_view in) {
    std::u32string out;
    for (size_t i = 0; i < in.size();) {
        const unsigned char c = static_cast<unsigned char>(in[i]);
        const int           extra = c < 0x80 ? 0 : (c >> 5) == 6 ? 1 : (c >> 4) == 14 ? 2 : (c >> 3) == 30 ? 3 : -1;
        if (extra < 0 || i + static_cast<size_t>(extra) >= in.size()) {
            out.push_back(U'?');
            i++;
            continue;
        }
        char32_t ch = extra == 0 ? c : (c & (0x3F >> extra));
        for (int k = 1; k <= extra; k++) {
            ch = (ch << 6) | (static_cast<unsigned char>(in[i + k]) & 0x3F);
        }
        out.push_back(ch);
        i += static_cast<size_t>(extra) + 1;
    }
    return out;
}

// What a caption looks like: where each letter's pen sits, in logical pixels from the caption's left, and
// the em it is laid out at.
struct Layout {
    std::u32string text;
    float          em = kEm;
    float          squeeze = 1.0f; // the share of its width a condensed caption keeps
    float          width = 0.0f;
};

float Natural(const std::u32string &text, float em, float scale_x) {
    const int em_px = std::max(4, static_cast<int>(em * scale_x + 0.5f));
    float     total = 0.0f;
    for (char32_t ch : text) {
        ttffont::Glyph glyph;
        if (ch == U' ') {
            total += em * 0.32f;
            continue;
        }
        if (!ttffont::GetGlyph(ch, em_px, glyph)) {
            total += em * 0.5f;
            continue;
        }
        total += glyph.advance / scale_x;
    }
    return total;
}

Layout Lay(std::string_view utf8, float box_width) {
    Layout layout;
    layout.text = Decode(utf8);
    const float scale_x = gfx::GetUiMapping(gfx::CurrentRenderTarget()).scale_x;
    float       natural = Natural(layout.text, layout.em, scale_x);
    if (box_width > 0 && natural > box_width) {
        layout.em = kEm * std::max(kLeastShrink, box_width / natural);
        natural = Natural(layout.text, layout.em, scale_x);
        if (natural > box_width) {
            layout.squeeze = box_width / natural;
            natural = box_width;
        }
    }
    layout.width = natural;
    return layout;
}

void Quad(std::vector<gfx::Vertex2D> &out, const ttffont::Glyph &glyph, float x, float y, float width, float height,
          const unsigned char (&rgb)[3], int alpha) {
    // The shader modulates by 255/128, so half the colour is what shows it.
    const u_char r = static_cast<u_char>(rgb[0] / 2), g = static_cast<u_char>(rgb[1] / 2), b = static_cast<u_char>(rgb[2] / 2);
    const u_char a = static_cast<u_char>(std::clamp(alpha, 0, 0x80));
    out.push_back(draw2d::Vertex(x, y, 0.0f, glyph.u0, glyph.v0, r, g, b, a));
    out.push_back(draw2d::Vertex(x + width, y, 0.0f, glyph.u1, glyph.v0, r, g, b, a));
    out.push_back(draw2d::Vertex(x + width, y + height, 0.0f, glyph.u1, glyph.v1, r, g, b, a));
    out.push_back(draw2d::Vertex(x, y + height, 0.0f, glyph.u0, glyph.v1, r, g, b, a));
}

} // namespace

std::vector<std::pair<std::string, std::string>> Strings() {
    std::vector<std::pair<std::string, std::string>> out;
    for (const Entry &entry : kEntries) {
        out.emplace_back(entry.key, entry.english);
    }
    return out;
}

bool Ready() {
    return FontReady();
}

float Width(std::string_view utf8, float em) {
    if (!FontReady()) {
        return 0.0f;
    }
    const float scale_x = gfx::GetUiMapping(gfx::CurrentRenderTarget()).scale_x;
    return Natural(Decode(utf8), em, scale_x);
}

void BlankPainted(const char *name, PortDecodedTexture &texture) {
    if (name == nullptr || std::string_view(name) != "stayframe" || texture.width == 0 || texture.height == 0 ||
        texture.levels.empty() || !FontReady()) {
        return;
    }
    if (texture.format == gfx::TextureFormat::Index8) {
        // The palette's own transparent entry may not exist, so the picture becomes straight RGBA first.
        std::vector<std::vector<uint8_t>> levels;
        for (const std::vector<uint8_t> &level : texture.levels) {
            std::vector<uint8_t> rgba(level.size() * 4);
            for (size_t i = 0; i < level.size(); i++) {
                const uint32_t colour = texture.palette[level[i]];
                rgba[i * 4 + 0] = static_cast<uint8_t>(colour & 0xFF);
                rgba[i * 4 + 1] = static_cast<uint8_t>((colour >> 8) & 0xFF);
                rgba[i * 4 + 2] = static_cast<uint8_t>((colour >> 16) & 0xFF);
                rgba[i * 4 + 3] = static_cast<uint8_t>(colour >> 24);
            }
            levels.push_back(std::move(rgba));
        }
        texture.levels = std::move(levels);
        texture.format = gfx::TextureFormat::Rgba8;
        texture.four_bit = false;
    }
    if (texture.format != gfx::TextureFormat::Rgba8) {
        return;
    }
    for (size_t level = 0; level < texture.levels.size(); level++) {
        const int w = std::max<int>(1, static_cast<int>(texture.width >> level));
        const int h = std::max<int>(1, static_cast<int>(texture.height >> level));
        if (texture.levels[level].size() < static_cast<size_t>(w) * h * 4) {
            continue;
        }
        for (int icon = 0; icon < kIcons; icon++) {
            const MENU_ICON_INFO *info = GetMenuIconInfo(icon);
            if (info == nullptr) {
                continue;
            }
            // Cells round outward, with a pixel to spare for the outline's soft edge.
            const int left = std::max(0, (info->label_u >> level) - 1);
            const int top = std::max(0, (info->label_v >> level) - 1);
            const int right = std::min(w, ((info->label_u + info->label_width + (1 << level) - 1) >> level) + 1);
            const int bottom = std::min(h, ((info->label_v + info->label_height + (1 << level) - 1) >> level) + 1);
            for (int y = top; y < bottom; y++) {
                for (int x = left; x < right; x++) {
                    uint8_t *pixel = &texture.levels[level][(static_cast<size_t>(y) * w + x) * 4];
                    pixel[0] = pixel[1] = pixel[2] = pixel[3] = 0;
                }
            }
        }
    }
    texture.has_alpha = true;
}

bool Draw(int icon, int x, int y, int box_width, int alpha) {
    if (icon < 0 || icon >= kIcons || !FontReady()) {
        return false;
    }
    const std::string text = LocalizeText(kEntries[icon].key, kEntries[icon].english);
    const Layout      layout = Lay(text, static_cast<float>(box_width));

    const gfx::LogicalMapping mapping = gfx::GetUiMapping(gfx::CurrentRenderTarget());
    const float               rows = draw2d::RowScale();
    const ttffont::Metrics    metrics = ttffont::GetMetrics();
    const int                 em_px = std::max(4, static_cast<int>(layout.em * mapping.scale_x + 0.5f));
    const int                 outline_px = std::max(1, static_cast<int>(kOutlineWidth * mapping.scale_x + 0.5f));

    // The line sits in the middle of the 30-pixel cell, on the baseline the font's own proportions give.
    constexpr float kCell = 30.0f;
    const float     base = static_cast<float>(y) + (kCell - layout.em * (metrics.ascent + metrics.descent)) / 2 +
                       layout.em * metrics.ascent;

    auto snap = [](float value, float scale, float offset) { return (std::round(value * scale + offset) - offset) / scale; };

    std::vector<gfx::Vertex2D> edges, fills;
    float                      pen = static_cast<float>(x);
    for (char32_t ch : layout.text) {
        ttffont::Glyph fill;
        if (ch == U' ') {
            pen += layout.em * 0.32f * layout.squeeze;
            continue;
        }
        if (!ttffont::GetGlyph(ch, em_px, fill)) {
            pen += layout.em * 0.5f * layout.squeeze;
            continue;
        }
        ttffont::Glyph edge;
        const bool     has_edge = ttffont::GetOutlinedGlyph(ch, em_px, outline_px, edge);
        auto           place = [&](const ttffont::Glyph &glyph, std::vector<gfx::Vertex2D> &out, const unsigned char (&rgb)[3]) {
            if (glyph.width <= 0 || glyph.height <= 0) {
                return;
            }
            const float width = glyph.width / mapping.scale_x * layout.squeeze;
            const float height = glyph.height / mapping.scale_y * rows;
            float       gx = pen + glyph.x_offset / mapping.scale_x * layout.squeeze;
            float       gy = base * rows + glyph.y_offset / mapping.scale_y;
            gx = snap(gx, mapping.scale_x, mapping.offset_x);
            gy = snap(gy, mapping.scale_y, mapping.offset_y);
            Quad(out, glyph, gx, gy, width, height, rgb, alpha);
        };
        if (has_edge) {
            place(edge, edges, kOutline);
        }
        place(fill, fills, kFill);
        pen += fill.advance / mapping.scale_x * layout.squeeze;
    }
    if (!fills.empty()) {
        const gfx::TextureBinding binding = ttffont::Binding();
        if (!edges.empty()) {
            gfx::Draw2D(gfx::Primitive::Quads, edges, binding, draw2d::SpriteState());
        }
        gfx::Draw2D(gfx::Primitive::Quads, fills, binding, draw2d::SpriteState());
        draw2d::RestoreTestZbuf();
    }
    return true;
}

} // namespace caption
