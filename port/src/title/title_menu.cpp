#include "title/title_menu.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <string>

#include "captionfont.hpp"
#include "gamepad.hpp"
#include "localize.hpp"
#include "snd.hpp"
#include "sound.hpp"
#include "ui_framework.hpp"

void TiPlayVolSE(int group, int no, int voice, float volume);

namespace TitleMenu {

namespace {

struct Row {
    const char *key;
    const char *english;
};

// Retail's words for its painted rows (start.img and its French, German, Italian and Spanish
// copies), then Exit.
constexpr Row kRows[kRowCount] = {
    {"title.menu.start",    "START"   },
    {"title.menu.continue", "CONTINUE"},
    {"title.menu.option",   "OPTION"  },
    {"title.menu.exit",     "EXIT"    },
};

// Where the letters stand on the PAL screen. Retail's painted capitals are 16 tall, from 263 across,
// with their tops about 29 apart from 306, and the trademark lines start at about 405: a fourth row
// fits above them with the rows 28 apart from 284, which also keeps them clear of the logo.
constexpr int   kTextLeft = 263;
constexpr int   kFirstTextTop = 284;
constexpr int   kRowStride = 28;
constexpr float kCapHeight = 16.0f;

// Each letter's weight and width are the painting's, made in the font (tools/font/README.md),
// so it is drawn as it is. The renderer sets letters 1.2 apart plus this; the English rows' words come out
// as long as the painting's.
constexpr float kStretch = 1.0f;
constexpr float kLetterGap = -0.1f;

// The font's kerning, made as strong as the painting's: its START tucks the A under the T by about 3 pixels,
// where the font kerns them by 1.6.
constexpr float kKernStrength = 1.9f;

// capfont's slot for the title font (UI::FontWeight::Title).
constexpr int kTitleFontSlot = 3;

// Retail's hand stands about 2 above its row's capitals.
constexpr float kHandAboveText = 2.0f;

// The rows' hit area: the 256 pixels retail's painted rows span, and each row's height around its
// letters.
constexpr int kRowLeft = 193;
constexpr int kRowWidth = 256;
constexpr int kRowAboveText = 6;

// The painting's letters (start.img): cream texels at alpha 0xFF, twice the GS's opaque 0x80. At
// retail's 128 the GS blends them at twice their strength, 2 x cream - backdrop, which over the dark
// backdrop burns to white, the cream left only at their soft edges. At retail's 32 they are half
// opaque. The font's rows take the same look: the selected one burnt white, the rest cream at half
// opacity.
constexpr u8 kCreamR = 226;
constexpr u8 kCreamG = 253;
constexpr u8 kCreamB = 210;
constexpr u8 kBurntR = 247;
constexpr u8 kBurntG = 250;
constexpr u8 kBurntB = 239;

// The painting's unselected letters read darker than half its cream: its strokes are softer than the
// font's, so less of each letter is at full strength. Three quarters of half, judged on a real GPU.
constexpr int kOtherRowsShare = 3;
constexpr int kOtherRowsOf = 4;

// Retail's letters sit in a soft black halo that fades as it reaches out from them.
constexpr float kHaloReach = 3.75f;
constexpr float kHaloStrength = 0.55f;

enum Choice { kYes = 0, kNo = 1 };

bool g_confirm_open = false;
int  g_confirm_choice = kNo;
bool g_confirm_chosen = false;

UI::Cursor &ConfirmCursor() {
    static UI::Cursor cursor;
    return cursor;
}

std::string ConfirmTitle() {
    return LocalizeText("title.exit.confirm_title", "\"Exit\"");
}

std::string ConfirmDescription() {
    return LocalizeText("title.exit.confirm_desc", "Return to desktop?");
}

void PlayCursorSound() {
    TiPlayVolSE(MIDI_PORT_UNK_D, 122, 24, 1.0f);
}

void PlayConfirmSound() {
    TiPlayVolSE(MIDI_PORT_SE_DEFAULT, 38, 20, 1.0f);
}

void CloseConfirm() {
    g_confirm_open = false;
    PlayCursorSound();
}

} // namespace

float HandY(int row) {
    return static_cast<float>(kFirstTextTop + row * kRowStride) - kHandAboveText;
}

int RowAt(float x, float y) {
    const float top = static_cast<float>(kFirstTextTop - kRowAboveText);
    if (x < kRowLeft || x >= kRowLeft + kRowWidth || y < top || y >= top + kRowCount * kRowStride) {
        return -1;
    }
    return static_cast<int>((y - top) / kRowStride);
}

namespace {

// The caption renderer sets a line's first letter with its ink at the line's left, but the painting was
// typeset: each word starts at the same pen position, so a flat letter (E) stands a little right of a
// round one (C, O). This is how far the first letter of text sits right of where START's S does, in
// logical pixels, from the two letters' left side bearings in the title font.
float BearingFromS(const std::string &text) {
    float h[4], first[4], s[4];
    const auto ch = static_cast<unsigned char>(text.empty() ? 'S' : text[0]);
    if (ch >= 0x80 || !capfont::GetGlyphBounds(U'H', h, kTitleFontSlot) ||
        !capfont::GetGlyphBounds(ch, first, kTitleFontSlot) || !capfont::GetGlyphBounds(U'S', s, kTitleFontSlot)) {
        return 0.0f;
    }
    return (first[0] - s[0]) * kCapHeight / (h[3] - h[1]) * kStretch;
}

} // namespace

void Draw(int selected, int selected_alpha, int other_alpha) {
    UI::TextStyle style;
    style.align = UI::Alignment::Left;
    style.weight = UI::FontWeight::Title;
    style.scale = kCapHeight / 14.5f;
    style.xscale = kStretch;
    style.letter = kLetterGap;
    style.kern = kKernStrength;
    style.use_custom_rgb = true;
    style.see_through = true;
    // Label puts the capitals' top 3 below its y, less half what the scale adds to their height.
    const int cap_offset = static_cast<int>(3.0f - (kCapHeight - 14.5f) * 0.5f + 0.5f);

    for (int row = 0; row < kRowCount; ++row) {
        const bool chosen = row == selected;
        style.fill_r = chosen ? kBurntR : kCreamR;
        style.fill_g = chosen ? kBurntG : kCreamG;
        style.fill_b = chosen ? kBurntB : kCreamB;
        style.drop_shadow = false;
        style.halo = kHaloReach;
        style.halo_strength = kHaloStrength;
        const std::string text = LocalizeText(kRows[row].key, kRows[row].english);
        UI::Label label(text, style);
        label.Draw(kTextLeft + static_cast<int>(std::lround(BearingFromS(text))), kFirstTextTop + row * kRowStride - cap_offset,
                   // Retail's alphas on the painting's doubled texels (above).
                   chosen ? selected_alpha : std::min(other_alpha * 2 * kOtherRowsShare / kOtherRowsOf, 0x80));
    }
}

bool ConfirmOpen() {
    return g_confirm_open;
}

void OpenConfirm() {
    g_confirm_open = true;
    g_confirm_chosen = false;
    g_confirm_choice = kNo;
    const UI::Rect no = UI::ConfirmModal::ButtonRect(ConfirmTitle(), ConfirmDescription(), kNo);
    ConfirmCursor().SnapTo(static_cast<float>(no.x - 34), static_cast<float>(no.y + 3));
    PlayConfirmSound();
}

void StepConfirm(int pointer_hover, bool pointer_click, bool pointer_back) {
    if (g_confirm_chosen) {
        return;
    }
    if (pointer_hover >= 0 && pointer_hover != g_confirm_choice) {
        g_confirm_choice = pointer_hover;
        PlayCursorSound();
    }
    if (GamePad.Down(PAD_UP | PAD_DOWN)) {
        g_confirm_choice = 1 - g_confirm_choice;
        PlayCursorSound();
    } else if (GamePad.Down(PAD_CROSS) || GamePad.Down(PAD_START) || pointer_click) {
        if (g_confirm_choice == kYes) {
            g_confirm_chosen = true;
            PlayConfirmSound();
            SDL_Event event{};
            event.type = SDL_EVENT_QUIT;
            SDL_PushEvent(&event);
        } else {
            CloseConfirm();
        }
    } else if (GamePad.Down(PAD_CIRCLE) || pointer_back) {
        CloseConfirm();
    }
}

int ConfirmHit(float x, float y) {
    return UI::ConfirmModal::Hit(ConfirmTitle(), ConfirmDescription(), x, y);
}

void DrawConfirm(int alpha) {
    if (!g_confirm_open) {
        return;
    }
    UI::ConfirmModal::Draw(ConfirmTitle(), ConfirmDescription(), LocalizeText("battlemenu.quit.yes", "YES"),
                           LocalizeText("battlemenu.quit.no", "NO"), g_confirm_choice, ConfirmCursor(), alpha,
                           UI::WindowPreset::DarkTranslucent, g_confirm_chosen ? kYes : -1, g_confirm_chosen);
}

std::vector<std::pair<std::string, std::string>> Strings() {
    std::vector<std::pair<std::string, std::string>> out;
    for (const Row &row : kRows) {
        out.emplace_back(row.key, row.english);
    }
    out.emplace_back("title.exit.confirm_title", "\"Exit\"");
    out.emplace_back("title.exit.confirm_desc", "Return to desktop?");
    return out;
}

} // namespace TitleMenu
