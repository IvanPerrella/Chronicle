#pragma once

#include <functional>

// A developer tool for the text the port draws at runtime instead of as pictures: a line down the screen
// with the old look on its left (the text as the game paints it: bitmaps and the pictures of words) and
// the new on its right (text from font.ttf and the language's JSON). Every place that has both looks draws
// both through Both(), each cut to its side of the line (gfx::SetDrawClip); with the tool off, Both() draws only the
// runtime text, as the game does. See docs/TEXTWIPE.md for the list of places.
//
// Keys: F9 turns the tool on and off, F10 steps its mode: the line sweeps across (a slide), then holds at the
// right (all old), at the left (all new), then OVERLAY: both looks drawn over each other, the old in red and the
// new in cyan, so a misfit shows as a coloured fringe.
//
// Tuning (while on): the new text can be dialled onto the old with F11 (next set of text: messages, captions, area cards),
// F12 (next value; with Shift the previous), - and = (change the value; hold for more, Shift for ten times the step,
// Ctrl for a tenth) and F8 (reset the set). A panel lists the values. They are saved at once to
// `<save folder>/lang/textwipe_tuning.json`, are read at the next start, and apply to the game as it is played, wipe or no
// wipe, so a good fit can be kept; the defaults change nothing.
namespace textwipe {

enum class Mode {
    Sweep,
    ShowBefore,
    ShowAfter,
    Overlay,
};

/// The three kinds of runtime text that can be dialled.
enum class Kind {
    Message, // dialogue, menus, help windows (clsmes.cpp)
    Caption, // the menu captions beside the icons (caption_text.cpp)
    Card,    // the area name cards (area_card.cpp)
    Options, // the Options screen's text, which is message text drawn by options::Draw (its own fit)
    Count,
};

/// How a kind of runtime text is adjusted from the fit the code gives it. The identity changes nothing.
struct Tuning {
    float size = 1.0f;     // everything, times
    float xscale = 1.0f;   // side-to-side size of the letters, times
    float yscale = 1.0f;   // up-and-down size of the letters, times
    float letter = 0.0f;   // space added between letters, in logical pixels
    float word = 0.0f;     // space added between words, in logical pixels
    float dx = 0.0f;       // moves the text right, in logical pixels
    float dy = 0.0f;       // moves the text down, in logical pixels
    float fit = 1.0f;      // messages: 1 fits each word to the game's cells as the bitmaps are, 0 leaves the letters as spaced
    float lock = 0.0f;     // messages: 0 flows the letters as the font spaces them, 1 centres every letter in the game's own cell for it, as the bitmaps are, so a line cannot drift
    float align = 0.5f;    // messages with fit off: where a word sits in the cells the game gave it, 0 left to 1 right
    float weight = 0.0f;   // bolder: the letters are laid down again this far apart, in logical pixels
    float opacity = 1.0f;  // how solid the text is
    float shadow = 1.0f;   // messages: how strong the drop shadow is, times
};

/// The settings of a kind: the locked-in fit the code ships (Locked), changed by what the dials saved.
const Tuning &Get(Kind kind);

/// The fit that has been dialled in and locked as the code's own, which F8 returns a set to. The identity for a
/// kind that has not been tuned.
Tuning Locked(Kind kind);

/// Which message settings the text drawn now takes: Message (dialogue and every other window) unless a screen
/// that has its own fit says so with one of these for as long as it draws.
Kind MessageKind();

class MessageContext {
public:
    explicit MessageContext(Kind kind);
    ~MessageContext();
    MessageContext(const MessageContext &) = delete;
    MessageContext &operator=(const MessageContext &) = delete;

private:
    Kind previous_;
};

/// Where the sweep's line is, in logical x from 0 to 640, `seconds` after the tool was turned on: it eases out
/// to the right edge, eases back to the left, and rests a moment at each end.
float SweepPosition(double seconds);

/// Reads the keys and moves the line. Once per logic tick, and once per loading-screen frame (a place that
/// draws more than once per frame must see one line, so only this moves it).
void Update();

/// Whether the tool is on, so places that have two looks should draw both.
bool Active();

/// Whether it is on in overlay mode, where both looks are drawn in full over each other.
bool Overlaying();

/// The line's logical x now.
float Divider();

/// Draws the two looks: draw(false) is the old, to the line's left, and draw(true) the new, to its right. Off,
/// or where the line is at an edge, only the side that shows is drawn.
void Both(const std::function<void(bool runtime)> &draw);

/// The line itself and its two labels, on top of the frame. Does nothing while off.
void DrawDivider();

} // namespace textwipe
