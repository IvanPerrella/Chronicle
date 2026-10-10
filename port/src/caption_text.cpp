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
#include "menuetc.hpp"
#include "gametext.hpp"
#include "platform/ttffont.hpp"
#include "texture_port.hpp"
#include "textwipe.hpp"

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

// stayframe as the disc has it, uploaded once more before its words are cleared, for the text wipe.
unsigned g_painted_texture = 0;
unsigned g_painted_palette = 0;

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

// The upper case of a letter the font draws as a capital: ASCII and the Latin-1 letters the PAL languages use.
char32_t Upper(char32_t ch) {
    if ((ch >= U'a' && ch <= U'z') || (ch >= 0xE0 && ch <= 0xFE && ch != 0xF7)) {
        return ch - 0x20;
    }
    return ch == 0x153 ? 0x152 : ch;
}

// What the help window calls the icon: its message's first line, the name in quotes ("Article", "Sauvegarder"),
// without the quotes. Empty where the window holds no such message.
std::string HelpTitle(int icon) {
    const MENU_ICON_INFO *info = GetMenuIconInfo(icon);
    if (info == nullptr || CommonMenuMes2.buff == nullptr) {
        return {};
    }
    const short *codes = CommonMenuMes2.GetTextLineDataTop(info->help_mes_no);
    if (codes == nullptr) {
        return {};
    }
    std::string text = GameTextDecode(codes);
    const size_t end = text.find_first_of("\n{");
    if (end != std::string::npos) {
        text.erase(end);
    }
    while (!text.empty() && (text.front() == '"' || text.front() == ' ')) {
        text.erase(text.begin());
    }
    while (!text.empty() && (text.back() == '"' || text.back() == ' ')) {
        text.pop_back();
    }
    return text;
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
    float          em = kEm * textwipe::Get(textwipe::Kind::Caption).size;
    float          squeeze = 1.0f; // the share of its width a condensed caption keeps
    float          width = 0.0f;
};

float Natural(const std::u32string &text, float em, float scale_x) {
    const textwipe::Tuning &tune = textwipe::Get(textwipe::Kind::Caption);
    const int em_px = std::max(4, static_cast<int>(em * scale_x + 0.5f));
    float     total = 0.0f;
    for (char32_t ch : text) {
        ttffont::Glyph glyph;
        if (ch == U' ') {
            total += em * 0.32f + tune.word;
            continue;
        }
        if (!ttffont::GetGlyph(ch, em_px, glyph)) {
            total += em * 0.5f;
            continue;
        }
        total += glyph.width / scale_x * tune.xscale + em * 0.1f + tune.letter;
    }
    return total;
}

Layout Lay(std::string_view utf8, float box_width) {
    Layout layout;
    layout.text = Decode(utf8);
    for (char32_t &ch : layout.text) {
        ch = Upper(ch);
    }
    const float scale_x = gfx::GetUiMapping(gfx::CurrentRenderTarget()).scale_x;
    float       natural = Natural(layout.text, layout.em, scale_x);
    if (box_width > 0 && natural > box_width) {
        layout.em = kEm * textwipe::Get(textwipe::Kind::Caption).size * std::max(kLeastShrink, box_width / natural);
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

void BlankPainted(const char *name, PortDecodedTexture &texture) {
    if (name != nullptr && std::string_view(name) == "stayframe") {
        std::fprintf(stderr, "textwipe: stayframe loaded %ux%u format %d levels %zu font %d\n", texture.width, texture.height,
                     static_cast<int>(texture.format), texture.levels.size(), FontReady() ? 1 : 0);
    }
    if (name == nullptr || std::string_view(name) != "stayframe" || texture.width == 0 || texture.height == 0 ||
        texture.levels.empty() || !FontReady()) {
        return;
    }
    for (unsigned *key : {&g_painted_texture, &g_painted_palette}) {
        if (*key != 0 && PortKeyLive(*key)) {
            PortReleaseKey(*key);
        }
        *key = 0;
    }
    g_painted_texture = PortCreateTexture(texture, PortTextureOwner::Other, &g_painted_palette);
    std::fprintf(stderr, "textwipe: stayframe kept as key %u palette %u\n", g_painted_texture, g_painted_palette);
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

bool PaintedKeys(unsigned &texture_key, unsigned &palette_key) {
    if (g_painted_texture == 0 || !PortKeyLive(g_painted_texture)) {
        return false;
    }
    texture_key = g_painted_texture;
    palette_key = g_painted_palette;
    return true;
}

bool Draw(int icon, int x, int y, int box_width, int alpha) {
    if (icon < 0 || icon >= kIcons || !FontReady()) {
        return false;
    }
    // A translator's JSON string wins; else the name the help window gives the icon, so the two always agree;
    // else the English.
    std::string text = LocalizeText(kEntries[icon].key, "");
    if (text.empty()) {
        text = HelpTitle(icon);
    }
    if (text.empty()) {
        text = kEntries[icon].english;
    }
    const Layout      layout = Lay(text, static_cast<float>(box_width));
    const textwipe::Tuning &tune = textwipe::Get(textwipe::Kind::Caption);
    alpha = static_cast<int>(static_cast<float>(alpha) * tune.opacity);

    const gfx::LogicalMapping mapping = gfx::GetUiMapping(gfx::CurrentRenderTarget());
    const float               rows = draw2d::RowScale();
    const ttffont::Metrics    metrics = ttffont::GetMetrics();
    const int                 em_px = std::max(4, static_cast<int>(layout.em * mapping.scale_x + 0.5f));
    const int                 outline_px = std::max(1, static_cast<int>(kOutlineWidth * mapping.scale_x + 0.5f));

    // The line sits in the middle of the 30-pixel cell, on the baseline the font's own proportions give.
    constexpr float kCell = 30.0f;
    const float     base = static_cast<float>(y) + (kCell - layout.em * (metrics.ascent + metrics.descent)) / 2 +
                       layout.em * metrics.ascent + tune.dy;

    auto snap = [](float value, float scale, float offset) { return (std::round(value * scale + offset) - offset) / scale; };

    std::vector<gfx::Vertex2D> edges, fills;
    float                      pen = static_cast<float>(x) + tune.dx;
    for (char32_t ch : layout.text) {
        ttffont::Glyph fill;
        if (ch == U' ') {
            pen += (layout.em * 0.32f + tune.word) * layout.squeeze;
            continue;
        }
        if (!ttffont::GetGlyph(ch, em_px, fill)) {
            pen += layout.em * 0.5f * layout.squeeze;
            continue;
        }
        ttffont::Glyph edge;
        const bool     has_edge = ttffont::GetOutlinedGlyph(ch, em_px, outline_px, edge);
        auto           place = [&](const ttffont::Glyph &glyph, std::vector<gfx::Vertex2D> &out, const unsigned char (&rgb)[3], float shift_x = 0.0f, float shift_y = 0.0f) {
            if (glyph.width <= 0 || glyph.height <= 0) {
                return;
            }
            const float width = glyph.width / mapping.scale_x * layout.squeeze * tune.xscale;
            const float height = glyph.height / mapping.scale_y * rows * tune.yscale;
            float       gx = pen + glyph.x_offset / mapping.scale_x * layout.squeeze;
            float       gy = base * rows + glyph.y_offset / mapping.scale_y * tune.yscale;
            gx = snap(gx, mapping.scale_x, mapping.offset_x);
            gy = snap(gy, mapping.scale_y, mapping.offset_y);
            Quad(out, glyph, gx + shift_x, gy + shift_y * rows, width, height, rgb, alpha);
        };
        if (has_edge) {
            place(edge, edges, kOutline);
        }
        if (tune.weight > 0.0f) {
            const float half = tune.weight / 2;
            place(fill, fills, kFill, -half, 0.0f);
            place(fill, fills, kFill, half, 0.0f);
            place(fill, fills, kFill, 0.0f, -half);
            place(fill, fills, kFill, 0.0f, half);
        }
        place(fill, fills, kFill);
        pen += (fill.width / mapping.scale_x * tune.xscale + layout.em * 0.1f + tune.letter) * layout.squeeze;
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

std::vector<LetterWidth> LetterWidths(std::string_view utf8) {
    std::vector<LetterWidth> out;
    if (!FontReady()) {
        return out;
    }
    constexpr int kEmPx = 64;
    for (char32_t ch : Decode(utf8)) {
        ttffont::Glyph glyph;
        if (ch == U' ') {
            out.push_back({kEmPx * 0.3f, true});
        } else if (ttffont::GetGlyph(ch, kEmPx, glyph) && glyph.width > 0) {
            out.push_back({static_cast<float>(glyph.width), false});
        }
    }
    return out;
}

bool DrawLine(std::string_view utf8, float center_x, float cap_top, float cap_height, float xsq, float max_width,
              const LineStyle &style, float fade, float rows, LineBounds *bounds) {
    if (utf8.empty() || cap_height <= 0.0f || !FontReady()) {
        return false;
    }
    const gfx::LogicalMapping mapping = gfx::GetUiMapping(gfx::CurrentRenderTarget());
    const ttffont::Metrics    metrics = ttffont::GetMetrics();
    const std::u32string      text = Decode(utf8);
    float                     em = cap_height / std::max(0.2f, metrics.cap);
    int                       em_px = std::clamp(static_cast<int>(em * mapping.scale_x + 0.5f), 4, 512);
    float                     ink_bottom = 0.0f; // where the letters' ink ends below the baseline, in logical rows
    if (style.ink_fit) {
        // Size the em so the drawn letters' own tops and bottoms span cap_height, then lower the baseline so their
        // bottoms stand at cap_top + cap_height. Twice: the second measure is of the glyphs at the size that is drawn.
        for (int pass = 0; pass < 2; pass++) {
            float top = 1e9f, bottom = -1e9f;
            for (char32_t ch : text) {
                ttffont::Glyph glyph;
                if (ch != U' ' && ttffont::GetGlyph(ch, em_px, glyph) && glyph.width > 0) {
                    top = std::min(top, static_cast<float>(glyph.y_offset));
                    bottom = std::max(bottom, static_cast<float>(glyph.y_offset + glyph.height));
                }
            }
            const float ink = (bottom - top) / mapping.scale_y;
            if (ink <= 0.0f) {
                break;
            }
            ink_bottom = bottom / mapping.scale_y;
            if (pass == 0) {
                em *= cap_height / ink;
                em_px = std::clamp(static_cast<int>(em * mapping.scale_x + 0.5f), 4, 512);
            }
        }
    }
    const float               gap = cap_height * 0.075f + style.letter;

    struct Letter {
        ttffont::Glyph fill;
        char32_t       ch;
        bool           blank;
    };
    std::vector<Letter> letters;
    float               natural = 0.0f;
    for (char32_t ch : text) {
        Letter letter = {};
        letter.ch = ch;
        if (ch == U' ') {
            letter.blank = true;
            natural += cap_height * 0.32f + style.word;
        } else if (ttffont::GetGlyph(ch, em_px, letter.fill) && letter.fill.width > 0) {
            natural += letter.fill.width / mapping.scale_x * style.xscale + gap;
        } else {
            continue;
        }
        letters.push_back(letter);
    }
    if (letters.empty()) {
        return false;
    }
    natural -= gap;
    float squeeze = xsq;
    if (max_width > 0.0f && natural * squeeze > max_width) {
        squeeze = max_width / natural;
    }
    const float width = natural * squeeze;
    const float left = center_x + style.dx - width / 2;

    const float baseline = (cap_top + cap_height + style.dy) * rows - ink_bottom;
    auto        snap = [](float value, float scale, float offset) { return (std::round(value * scale + offset) - offset) / scale; };
    const int   edge_px = std::clamp(static_cast<int>(style.edge_width * mapping.scale_x + 0.5f), 0, 60);
    const int   glow_px = style.glow_amount > 0.0f ? std::clamp(static_cast<int>(style.glow_width * mapping.scale_x + 0.5f), 0, 60) : 0;

    auto quad = [&](std::vector<gfx::Vertex2D> &out, const ttffont::Glyph &glyph, const ttffont::Glyph &fill, float pen,
                    const unsigned char (&rgb)[3], int alpha, float shift_x = 0.0f, float shift_y = 0.0f) {
        if (glyph.width <= 0 || glyph.height <= 0) {
            return;
        }
        const float w = glyph.width / mapping.scale_x * squeeze * style.xscale;
        const float h = glyph.height / mapping.scale_y * rows * style.yscale;
        float       x = pen + (glyph.x_offset - fill.x_offset) / mapping.scale_x * squeeze * style.xscale;
        float       y = baseline + glyph.y_offset / mapping.scale_y * style.yscale;
        x = snap(x, mapping.scale_x, mapping.offset_x) + shift_x;
        y = snap(y, mapping.scale_y, mapping.offset_y) + shift_y * rows;
        const float k = std::clamp(fade, 0.0f, 1.0f) * style.opacity * 0.5f;
        const u_char r = static_cast<u_char>(rgb[0] * k), g = static_cast<u_char>(rgb[1] * k), b = static_cast<u_char>(rgb[2] * k);
        const u_char a = static_cast<u_char>(std::clamp(alpha, 0, 0x80));
        out.push_back(draw2d::Vertex(x, y, 0.0f, glyph.u0, glyph.v0, r, g, b, a));
        out.push_back(draw2d::Vertex(x + w, y, 0.0f, glyph.u1, glyph.v0, r, g, b, a));
        out.push_back(draw2d::Vertex(x + w, y + h, 0.0f, glyph.u1, glyph.v1, r, g, b, a));
        out.push_back(draw2d::Vertex(x, y + h, 0.0f, glyph.u0, glyph.v1, r, g, b, a));
    };

    size_t drawn = 0;
    for (const Letter &letter : letters) {
        drawn += letter.blank ? 0 : 1;
    }
    const bool                 use_slots = style.slots.size() == drawn;
    size_t                     slot = 0;
    std::vector<gfx::Vertex2D> glows, edges, fills;
    float                      pen = left;
    for (const Letter &letter : letters) {
        if (letter.blank) {
            pen += (cap_height * 0.32f + style.word) * squeeze;
            continue;
        }
        if (use_slots) {
            // Into the slot the painted letter stood in: its left edge, and the width its ink had.
            const std::array<float, 2> &place = style.slots[slot++];
            const float                 ink = letter.fill.width / mapping.scale_x * style.xscale;
            squeeze = std::clamp((place[1] - place[0]) / std::max(ink, 0.01f), 0.55f, 1.9f);
            pen = place[0];
        }
        ttffont::Glyph extra;
        if (glow_px > 0 && ttffont::GetOutlinedGlyph(letter.ch, em_px, glow_px, extra)) {
            quad(glows, extra, letter.fill, pen, style.glow, static_cast<int>(0x80 * std::min(1.0f, style.glow_amount) * 0.55f));
        }
        if (edge_px > 0 && ttffont::GetOutlinedGlyph(letter.ch, em_px, edge_px, extra)) {
            quad(edges, extra, letter.fill, pen, style.edge, 0x80);
        }
        if (style.weight > 0.0f) {
            const float half = style.weight / 2;
            quad(fills, letter.fill, letter.fill, pen, style.fill, 0x80, -half, 0.0f);
            quad(fills, letter.fill, letter.fill, pen, style.fill, 0x80, half, 0.0f);
            quad(fills, letter.fill, letter.fill, pen, style.fill, 0x80, 0.0f, -half);
            quad(fills, letter.fill, letter.fill, pen, style.fill, 0x80, 0.0f, half);
        }
        quad(fills, letter.fill, letter.fill, pen, style.fill, 0x80);
        pen += (letter.fill.width / mapping.scale_x * style.xscale + gap) * squeeze;
    }
    const gfx::TextureBinding binding = ttffont::Binding();
    for (std::vector<gfx::Vertex2D> *layer : {&glows, &edges, &fills}) {
        if (!layer->empty()) {
            gfx::Draw2D(gfx::Primitive::Quads, *layer, binding, draw2d::SpriteState());
        }
    }
    draw2d::RestoreTestZbuf();
    if (bounds != nullptr) {
        bounds->left = left;
        bounds->right = left + width;
    }
    return true;
}

} // namespace caption
