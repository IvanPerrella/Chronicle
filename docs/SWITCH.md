# Nintendo Switch

Chronicle can be built as a homebrew `.nro` for a Switch running custom firmware. It uses devkitPro's devkitA64 and libnx, SDL3 with the Switch Vulkan surface, and Mesa NVK for Vulkan 1.3. Supply your own Dark Cloud data extracted from a disc; game data stays outside Git.

## Build

The Switch toolchain image is built with Docker and contains pinned devkitPro, Mesa/NVK, SDL3, glslang and Rust tooling. A Linux host or WSL with Docker is required.

```sh
./build.sh linux-x64-release
port/build/pc-release/dcdata extract "rom/pal/Dark Cloud (PAL).iso" data
./build.sh switch
```

On Windows, run build.bat switch with WSL installed and Docker available to WSL. The output is `port/build/switch/darkcloud.nro`. The first container image build takes a while because it builds Mesa's NVK driver and SDL3. `BUILDER` selects an OCI-compatible container runtime; Docker is used by default. Run `port/switch/build.sh shell` to open the build image.

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
