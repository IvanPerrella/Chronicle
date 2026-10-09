#pragma once

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

/// Draws icon's caption with its box's top-left at (x, y) on the 640-wide logical screen, `box_width` wide
/// at most (longer text is shrunk and then condensed to fit), at alpha (0x80 solid). False where there is
/// no font, and the painted caption is drawn instead.
bool Draw(int icon, int x, int y, int box_width, int alpha);

} // namespace caption
