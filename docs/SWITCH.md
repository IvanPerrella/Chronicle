# Nintendo Switch

Chronicle can be built as a homebrew `.nro` for a Switch running custom firmware. It uses devkitPro's devkitA64 and libnx, SDL3 with the Switch Vulkan surface, and Mesa NVK for Vulkan 1.3. Supply your own Dark Cloud data extracted from a disc; game data stays outside Git.

## Build

The Switch toolchain image is built with Docker and contains pinned devkitPro, Mesa/NVK, SDL3, glslang and Rust tooling. A Linux host or WSL with Docker is required.

```sh
./build.sh linux-x64-release
port/build/pc-release/dcdata extract "rom/pal/Dark Cloud (PAL).iso" data
./build.sh switch
```

On Windows, run `build.bat switch` from a WSL-enabled system with Docker available to WSL. The output is `port/build/switch/darkcloud.nro`. The first container image build takes a while because it builds Mesa's NVK driver and SDL3. `BUILDER` selects an OCI-compatible container runtime; Docker is used by default. Run `port/switch/build.sh shell` to open the build image.

## Install

Copy these files to `sdmc:/switch/chronicle/`:

- `darkcloud.nro` from `port/build/switch/`
- `lang/` and `glyphs/` from `port/build/switch/`
- `data/` from the extraction step

Start Chronicle from the Homebrew Menu with a game's title override by holding R while starting a game. Launching from the album applet leaves too little memory for the game. Runtime logs are written to `darkcloud.log` beside the `.nro`; `save/` stores settings, saves and the Vulkan pipeline cache.

The Homebrew Menu does not pass arguments to the application. Put whitespace-separated arguments in `args.txt` beside the `.nro`; supported options are listed in [PC port documentation](PC.md).

## Platform support

The Switch build supplies guarded arena memory, cooperative context switching, compatibility flags for newlib and logging through nxlink. Its renderer uses Vulkan 1.3 and renders at 1.5 pixels per game texel for the handheld display. The game has not been qualified on physical Switch hardware in this integration.

To send an existing build through the Homebrew Menu's netloader, use `port/switch/run.sh`. Set `SWITCH_ADDRESS` when the console cannot be discovered by broadcast.

## Render scale

By default the renderer uses the active display's pixel height divided by the game's 480-pixel logical height. This preserves the current automatic handheld and docked resolutions. To choose a fixed scale, add `render_scale` under `video` in `save/config.json`; valid values are 0.5 through 2.0. For example, `1.0` renders at the game's logical resolution. The setting is read at startup; remove the key to return to automatic scaling. The Options screen does not expose this setting on Switch.

## Performance work

The Switch integration has not been measured on physical hardware, so there are no performance claims or chosen quality trade-offs yet. The first step is to establish a repeatable baseline on physical consoles. Run the same PAL data, save, scripted inputs, area and camera path at each render scale in handheld and docked modes. Record native frame times and dropped or late presents, and capture both canonical tick images and interpolated display images. Check that game ticks remain at 60 per second while comparing GPU and CPU timings; interpolated display frames must not be counted as extra game ticks.

Use a small scene set that covers a town view with water and refraction, dungeon shadows and effects, a skinned character with cloth, material animation, and area transitions. Include a long run through transitions followed by shutdown to exercise asynchronous rendering and resource cleanup. Compare the canonical captures against the baseline for each change, and compare interpolated captures separately because they include display-only motion. Keep render scale and every other quality setting fixed while timing one change at a time.

The shared Kazarov renderer already batches compatible draws, uses dynamic state for several pipeline changes, and supports skinned meshes; the port also records interpolation data for compatible draws. `DC_PRESENT_STATS=1` reports draw counts and canonical and display render times at exit. Kazarov's Tracy CPU and Vulkan GPU zones are available behind `KZ_PORT_TRACY`, but that instrumentation is not enabled in the Switch build. A profiling build should first establish whether CPU submission, GPU work, synchronization or memory pressure limits each scene.

MDT-remake work, GPU skinning, additional culling and multi-draw indirect submission remain profiling-dependent follow-ups. Consider them only where the Switch measurements identify a bottleneck, then rerun the same timing and capture matrix. In particular, validate water and refraction, shadows, cloth and material animation, transitions, unchanged tick counts, interpolated frames and asynchronous shutdown after each renderer change. Preserve the current handheld and docked defaults unless measurements support a documented change.
