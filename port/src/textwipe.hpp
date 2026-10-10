#pragma once

#include <functional>

// A developer tool for the text the port draws at runtime instead of as pictures: a line down the screen
// with the old look on its left (the text as the game paints it: bitmaps and the pictures of words) and
// the new on its right (text from font.ttf and the language's JSON). Every place that has both looks draws
// both through Both(), each cut to its side of the line (gfx::SetDrawClip); with the tool off, Both() draws only the
// runtime text, as the game does. See docs/TEXTWIPE.md for the list of places.
//
// Keys: F9 turns the tool on and off, F10 steps its mode: the line sweeps across (a slide), then holds at the
// right (all old), then at the left (all new).
namespace textwipe {

enum class Mode {
    Sweep,
    ShowBefore,
    ShowAfter,
};

/// Where the sweep's line is, in logical x from 0 to 640, `seconds` after the tool was turned on: it eases out
/// to the right edge, eases back to the left, and rests a moment at each end.
float SweepPosition(double seconds);

/// Reads the keys and moves the line. Once per logic tick, and once per loading-screen frame (a place that
/// draws more than once per frame must see one line, so only this moves it).
void Update();

/// Whether the tool is on, so places that have two looks should draw both.
bool Active();

/// The line's logical x now.
float Divider();

/// Draws the two looks: draw(false) is the old, to the line's left, and draw(true) the new, to its right. Off,
/// or where the line is at an edge, only the side that shows is drawn.
void Both(const std::function<void(bool runtime)> &draw);

/// The line itself and its two labels, on top of the frame. Does nothing while off.
void DrawDivider();

} // namespace textwipe
