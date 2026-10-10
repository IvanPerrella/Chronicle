#pragma once

#include <filesystem>
#include <vector>

#include "gfx/gfx.hpp"

// A TrueType font for the game's message text, drawn at the screen's own resolution instead of
// from the 14x20 bitmaps of gaiji.img. Host side: plain types only.
namespace ttffont {

// Reads the font file; false (with a message on stderr) for one that is missing or not TrueType.
// Loading again replaces the font.
bool Load(const std::filesystem::path &file);

bool Loaded();

// One glyph in the atlas. u and v are texel coordinates of the bitmap in the atlas; the bitmap is
// width x height pixels, its top-left x_offset pixels right of the pen and y_offset pixels below
// the baseline (negative above it).
struct Glyph {
    float u0, v0, u1, v1;
    float x_offset, y_offset;
    float width, height;
    float advance = 0; // how far the pen moves on after the letter, in pixels
};

// The glyph of ch at an em of em_px pixels, rasterized into the atlas when first asked for. False
// where the font has no glyph for ch, so the caller draws what it drew before, or the atlas is full.
bool GetGlyph(char32_t ch, int em_px, Glyph &out);

// The same glyph with its shape grown by outline_px pixels (0 to 64) on every side: the stroke of a
// caption's outline, white with the grown coverage as alpha. Drawn behind GetGlyph's at the same pen, it
// gives each letter a real outline. x_offset, y_offset, width and height describe the grown bitmap.
bool GetOutlinedGlyph(char32_t ch, int em_px, int outline_px, Glyph &out);

// The glyph of ch at an em of em_px pixels as plain coverage (alpha, w x h, 0 to 255) with its top-left
// corner x0, y0 pixels from the pen on the baseline, y down; what GetGlyph puts in the atlas, and what
// the tests look at. False where GetGlyph would say so.
bool RenderGlyph(char32_t ch, int em_px, std::vector<unsigned char> &alpha, int &w, int &h, int &x0, int &y0);

// A second font for lettering that wants a lighter stroke (the community font's thin variant), loaded beside the first. Glyphs come
// from it between UseVariant(true) and UseVariant(false), when it is loaded; the atlas keeps both.
bool LoadVariant(const std::filesystem::path &file);
bool VariantLoaded();
void UseVariant(bool variant);

// An RGBA image of the caller's own (w x h, r g b a bytes) put in the atlas beside the glyphs, for lettering that is not one
// colour: draw it with the atlas's Binding like a glyph, through out's u and v. x_offset and y_offset are 0. False where the
// atlas cannot take it.
bool AddImage(const std::vector<unsigned char> &rgba, int w, int h, Glyph &out);

// Changes each time the atlas starts again, which forgets every glyph and image in it: a caller that keeps AddImage's results
// makes them again when it differs.
unsigned Generation();

// The atlas, to draw GetGlyph's coordinates from.
gfx::TextureBinding Binding();

// The font's own proportions, as fractions of an em.
struct Metrics {
    float ascent;
    float descent; // positive, below the baseline
    float advance; // of the digit 0
    float cap;     // height of a capital letter
};

Metrics GetMetrics();

} // namespace ttffont
