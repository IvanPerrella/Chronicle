# Text wipe: old picture text against runtime text

A developer tool for the text the port draws at runtime (from `font.ttf` and the language's JSON) instead of
as the game's pictures of words. It puts a line down the screen: the **old look** (the game's bitmaps and
painted words) on its left, the **new look** (runtime text) on its right.

| Key | Does |
|---|---|
| **F9** | Turns the wipe on and off. Turning it on starts a slide: everything old, then the line eases left across the screen revealing the new text, rests, and slides back. |
| **F10** | Steps the mode while on: slide, hold all old, hold all new. |

It works on any scene that draws one of the places below, and needs `font.ttf` (the build ships it in `lang/`);
without it both looks are the old one. The bottom of the screen labels the two sides. It is
a view only: nothing is saved, and with it off the game draws exactly what it did.

## Places that draw both looks

| Place | Old look | New look | Code |
|---|---|---|---|
| Message text: dialogue, menus, help windows, item and shop text, every message the game lays out | the 14x20 bitmap characters of `gaiji` | `font.ttf` characters at the screen's resolution | `ClsMes::Draw` in `clsmes.cpp` (`text_pass`) |
| Menu captions beside the main menu's icons (ITEM, WEAPON, GO TO DUNGEON...), town and dungeon | the words painted into `stayframe` (kept from before `BlankPainted` cleared them) | outlined `font.ttf` text | `DrawMainMenuIcon` in `menu_draw.cpp`, `caption_text.cpp` |
| Area name cards on the loading screen (Norune, Matataki...) | the disc's card with the name painted in | the text-free plate with the name drawn over it | `nowload.cpp`, `area_card.cpp` |

Not wiped, because their old look is not drawn alongside: the FPS badge (`gameloop.cpp`, `overlay.cpp`; its
old look is the 5x7 bitmap font, recorded into a separate overlay list), the Options screen's rows (they
are message text, so they are covered by the first row), and per-language pictures swapped for others
(`localize_texture.cpp`, `LocalizeTexture`), which replace a picture rather than draw text.

## How a place joins

```cpp
textwipe::Both([&](bool runtime) {
    if (runtime) { /* draw the text */ } else { /* draw what the game drew */ }
});
```

Off, `Both` calls the function once with `runtime = true`. On, it calls it with `false` clipped to the line's
left (`gfx::SetDrawClip`) and with `true` clipped to its right, so the draws must go out inside the call (not
into a packet flushed later). Keep the old look's data when the new one replaces it (as `BlankPainted` keeps
`stayframe`).
