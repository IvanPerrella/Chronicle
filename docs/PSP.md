# PSP port

`PLATFORM=PSP` builds the game's code for the PSP-2000 and later (PSP-3000, Go,
Street) with the pspdev toolchain (GCC 15, newlib, SDL3), as `EBOOT.PBP`. The
PSP-1000 is not a target: its 24 MB for programs is less than the game's own
27 MB arena. The mechanism is the PC port's (`docs/PC.md`); this document
lists what differs.

## Building

```sh
port/psp.sh            # port/build/psp/EBOOT.PBP, lang/ and glyphs/ beside it
CLEAN=1 port/psp.sh    # from scratch
```

The build runs in the `pspdev/pspdev` container (Docker), which has the
toolchain and SDL3; Python is added to it for the override script.

## Installing

Copy `EBOOT.PBP`, `lang/` and `glyphs/` to `ms0:/PSP/GAME/Chronicle/` on the
memory stick, with the extracted game data (`dcdata extract`, see
`docs/PC.md`, "Game data") as `data/` beside them. Saves and `config.json`
go to `save/` there. There is no first-run extraction on the PSP.

## What differs from the PC port

- **Types.** 32-bit MIPS has no `__int128` and a 32-bit `long`: `port.h`
  makes `u_long128` a 16-byte block and `u_long` the PS2's 64-bit type, as
  on Windows. newlib's `int32_t` is `long`, so `std::min` and its kin name
  their type where they mix it with `int`.
- **Missing returns.** GCC has no `-fno-strict-return`: falling off the end of
  a non-void function is taken as unreachable, and the code runs on into the
  next function. The 19 functions in `ps2/src` that do it end in
  `PORT_RETURN(value)`, which the PS2 build expands to nothing.
- **Memory.** No virtual memory: the arenas are carved from
  `GlobalDataBuffer`'s own storage in retail's order, at retail's sizes
  (`port/src/dataset.cpp`), instead of host blocks at four times the size.
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
- Discord, the desktop icon and the text export are left out.
