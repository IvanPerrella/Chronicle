# PSP port

`PLATFORM=PSP` builds the game's code for the PSP with the pspdev toolchain
(GCC 15, newlib), as `EBOOT.PBP`, for every model: the PSP-1000 and the
PSP-2000, 3000, Go and Street. The mechanism is the PC port's (`docs/PC.md`);
this document lists what differs.

## Building

```sh
port/psp.sh            # port/build/psp/EBOOT.PBP, lang/ and glyphs/ beside it
CLEAN=1 port/psp.sh    # from scratch
```

The build runs in the `pspdev/pspdev` container (Docker), which has the
toolchain; Python is added to it for the override script. It is `MinSizeRel`
with `--gc-sections`, except the game's own code, which keeps `-O2`.
`port/build/psp/port/darkcloud.map` says where the memory goes.

## Installing

Copy `EBOOT.PBP`, `lang/` and `glyphs/` to `ms0:/PSP/GAME/Chronicle/` on the
memory stick, with the extracted game data (`dcdata extract`, see
`docs/PC.md`, "Game data") as `data/` beside them. Saves and `config.json`
go to `save/` there. There is no first-run extraction on the PSP.

## Controls

| PSP | DualShock 2 |
|---|---|
| analog nub | left stick |
| d-pad, ×, ○, □, △, Start | the same |
| L, R | L1, R1 (they also turn the camera) |
| Select with L | L2: the camera behind the character |
| Select with R | R2: first-person look |
| Select, pressed and let go alone | Select: switch character |

There is no right stick, L3 or R3 (debug functions only); HOME's quit stops the
game as closing the window does.

## Memory

A PSP-1000 gives a program 24 MB, a PSP-2000 or later 52 MB; retail Dark Cloud
carves up to 27 MB of arenas from its `GlobalDataBuffer`.

- **The image is 7.6 MB** (code 3.5 MB, data 0.8 MB, statics 3.3 MB). There is
  no SDL: `platform_psp/` reads the buttons with `sceCtrl` and plays sound
  through `sceAudio`'s sample-rate-converting channel at the mixer's 48 kHz.
- **No `GlobalDataBuffer` storage.** `port.h` turns the name into a pointer
  nothing follows, and the arenas are carved in retail's order from a pool
  (`ArenaPoolCarve`, `tools/kazarov/src/cpp/platform/arena.cpp`): as much of
  the heap as the rest of the program can spare, up to retail's 27 MB. On a
  PSP-1000, where that is less, the 4 MB of volatile memory joins it, and the
  system is kept from sleeping, which would wipe that memory.
- **The arena report.** Every clear of the arenas (each mode's start) logs
  how much of each arena the last mode filled, and the pool's high-water mark
  (`DC_ARENA_REPORT=1` on the PC). A PSP-1000's pool is smaller than what
  retail carves for a town or a dungeon, so those modes stop with "the pool
  has no ... left" until the arenas are capped at what the game fills, which
  this report measures.

## What differs from the PC port

- **Types.** 32-bit MIPS has no `__int128` and a 32-bit `long`: `port.h`
  makes `u_long128` a 16-byte block and `u_long` the PS2's 64-bit type, as
  on Windows. newlib's `int32_t` is `long`, so `std::min` and its kin name
  their type where they mix it with `int`.
- **Missing returns.** GCC has no `-fno-strict-return`: falling off the end of
  a non-void function is taken as unreachable, and the code runs on into the
  next function. The 19 functions in `ps2/src` that do it end in
  `PORT_RETURN(value)`, which the PS2 build expands to nothing.
- **Renderer.** `tools/kazarov/src/cpp/gfx_psp/renderer.cpp` implements
  `gfx.hpp` on the GE in place of Vulkan. Draws go to the GE as they are
  made; a tick's display list is the GE list, and there is no interpolation.
  Colour stays in GS units (`GU_FRAGMENT_2X`, the doubled-alpha blend
  factors). The GE lights and fogs meshes itself, from matrices derived from
  the draw's `mvp`. Render targets live in the 600 KiB of VRAM past the two
  framebuffers and the depth buffer.
- **Not done yet.** Stencil (shadow volumes), depth reads (the lens flare),
  user clip planes and screen- and world-space texture coordinates; textures
  past 512 texels are kept at half size.
- Discord, the desktop icon, the text export, key rebinding and rumble are
  left out.
