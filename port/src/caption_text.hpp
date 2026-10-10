#pragma once

#include <array>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

struct PortDecodedTexture;

// The menus' captions, the words next to the main menu's icons (ITEM, WEAPON, GO TO DUNGEON...). The
// game paints them into stayframe, one picture per language. With the community font loaded they are text
// instead: drawn from font.ttf at the screen's own resolution, a cream fill over a blue outline, the words
// the game's own help window gives the icon (its quoted title), in capitals, so the two always agree in every
// language; a language's JSON can override one with menu.caption.<name> (docs/LOCALIZATION.md). Without the font the painted words stay as they were.
namespace caption {

// The colours of the painted English captions, which every language's text is drawn in (the French and
// Italian pictures were painted in a purple, which is not what the game used).
constexpr unsigned char kFill[3] = {232, 226, 183};
constexpr unsigned char kOutline[3] = {1, 0, 201};

// How many icons the menus have, and the stayframe the captions are blanked in.
constexpr int kIcons = 14;

/// Key and English of every caption: the JSON override's key, and what is drawn when the help window has no title.
std::vector<std::pair<std::string, std::string>> Strings();

/// Whether captions are drawn as text: the font is there.
bool Ready();

/// Clears the painted words from stayframe's cells, so the text can stand alone. Does nothing for another
/// texture, or without the font. The texture may be the disc's palette picture or a replacement of any size.
void BlankPainted(const char *name, PortDecodedTexture &texture);

/// The renderer keys (as PortCreateTexture gives them) of stayframe as it was before BlankPainted cleared the words: the
/// painted captions, kept for the text wipe (textwipe.hpp) to show beside the text. False where none was kept.
bool PaintedKeys(unsigned &texture_key, unsigned &palette_key);

/// Draws icon's caption with its box's top-left at (x, y) on the 640-wide logical screen, `box_width` wide
/// at most (longer text is shrunk and then condensed to fit), at alpha (0x80 solid). False where there is
/// no font, and the painted caption is drawn instead.
bool Draw(int icon, int x, int y, int box_width, int alpha);

/// A line of lettering drawn the way a caption is, for pictures of text that are text at runtime (the area name cards):
/// all sizes are in the units the draw call is in (the logical screen of gfx::Draw2D).
/// The colours of a picture of lettering, for lettering that is not one colour: each texel holds the colour of the nearest
/// pixel of the painted letters, so letters drawn over it carry the painted texture (grain, shading) as the painted ones do.
struct FillField {
    int                        width = 0;
    int                        height = 0;
    std::vector<unsigned char> rgb; // width * height * 3
    unsigned                   id = 0; // told apart from other fields for caching
};

struct LineStyle {
    unsigned char fill[3] = {255, 255, 255};
    unsigned char edge[3] = {0, 0, 0};
    unsigned char glow[3] = {0, 0, 0};
    float         edge_width = 1.0f; // the outline, in logical units
    float         glow_width = 0.0f; // how far the glow reaches
    float         glow_amount = 0.0f; // 0 none, 1 as strong as the lettering
    // The text wipe's dials (textwipe.hpp), 1 and 0 where untuned: letter and word space added, side-to-side and
    // up-and-down size, and a move, in logical units.
    float         letter = 0.0f, word = 0.0f, xscale = 1.0f, yscale = 1.0f, dx = 0.0f, dy = 0.0f;
    float         weight = 0.0f, opacity = 1.0f; // bolder (a spread, in logical units), and how solid
    // cap_top and cap_height name the box the letters' own ink fills (their tops and bottoms as drawn), not the font's
    // capital height, which is what the plate's painted lettering was measured as.
    bool          ink_fit = false;
    // Fill each letter from this field, which covers the logical rectangle (field_x, field_y, field_w, field_h), instead of
    // with the one colour fill.
    const FillField *field = nullptr;
    float            field_x = 0.0f, field_y = 0.0f, field_w = 1.0f, field_h = 1.0f;
    // Where each letter's ink goes, left and right in logical units, one per drawn letter (spaces excluded), in order; each
    // glyph is drawn into its slot. Empty to lay the line out by the font. Used when the painted lettering has been split.
    std::vector<std::array<float, 2>> slots;
};

struct LineBounds {
    float left = 0.0f;
    float right = 0.0f;
};

/// Draws `utf8` (as given: capitals are the caller's choice) centred on center_x, its capitals from cap_top down for
/// cap_height. The letters are the font's, squeezed to xsq of their width (less if the line would pass max_width),
/// spaced by their ink and a twelfth of a capital. `fade` (0 to 1) darkens the whole line toward black, as the card's
/// fade does. `rows` converts logical rows to the target's (1 where the caller is already in them). False without a
/// font. bounds gets where the lettering lies.
/// What the letters of utf8 measure at one size: the width of each drawn character's ink and whether it is a space (which has
/// the width of one), in the order DrawLine lays them out. Empty without the font. For cutting a picture of the same words into letters.
struct LetterWidth {
    float width;
    bool  blank;
};
std::vector<LetterWidth> LetterWidths(std::string_view utf8);

bool DrawLine(std::string_view utf8, float center_x, float cap_top, float cap_height, float xsq, float max_width,
              const LineStyle &style, float fade, float rows, LineBounds *bounds);

} // namespace caption
