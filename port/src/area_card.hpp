#pragma once

#include <string>
#include <utility>
#include <vector>

struct PortDecodedTexture;

// The area name cards of the loading screen (mt01 Norune, mt02 Matataki...): the game paints each area's name into one
// picture per language, which is blurry on a screen it was never drawn for. With a plate for the card (the backdrop
// without its lettering, one picture for every language, from tools/textpack: `build.py --only plates`) and the
// font, the card is the plate and its name is lettering drawn at the screen's own resolution: a brush underline, an
// outline and, on some cards, a glow, in the colours and places `textures/plates/<card>.json` gives (the plate's
// own, sampled from the disc's card). The words are `area.<card>` in the language's JSON. Without a plate or the
// font, the loading screen is as it was (the language's picture from `textures/<language>/<card>.png`, or the disc's).
namespace areacard {

/// Key and English of every card's name, as localize.hpp's LocalizeText takes them.
std::vector<std::pair<std::string, std::string>> Strings();

/// Forgets the card the loading screen showed.
void Clear();

/// Makes `name`'s card the plate where there is one: replaces the picture of `texture` and remembers the lettering to
/// draw over it. False (the texture untouched) where there is no plate, no layout, no font or no name for it.
bool Prepare(const std::string &name, PortDecodedTexture &texture);

/// Whether a card is prepared: its picture is a plate and Draw has lettering to put on it.
bool Prepared();

/// Draws the lettering of the prepared card over its picture, drawn with its corners at (x0, y0) and (x1, y1) in the
/// logical units of gfx::Draw2D, faded to `fade` (0 to 1).
void Draw(float x0, float y0, float x1, float y1, float fade);

} // namespace areacard
