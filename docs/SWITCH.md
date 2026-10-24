# Nintendo Switch

Chronicle can be built as a homebrew `.nro` for a Switch running custom firmware. It uses devkitPro's devkitA64 and libnx, SDL3 with the Switch Vulkan surface, and Mesa NVK for Vulkan 1.3. Supply your own Dark Cloud data extracted from a disc; game data stays outside Git.

## Build

The Switch toolchain image is built with Docker and contains pinned devkitPro, Mesa/NVK, SDL3, glslang and Rust tooling. A Linux host or WSL with Docker is required.

```sh
./build.sh linux-x64-release
port/build/pc-release/dcdata extract "rom/pal/Dark Cloud (PAL).iso" data
port/build/pc-release/dcdata pack data
./build.sh switch
```

On Windows, run `build.bat switch` from a WSL-enabled system with Docker available to WSL. The output is `port/build/switch/darkcloud.nro`. The first container image build takes a while because it builds Mesa's NVK driver and SDL3. `BUILDER` selects an OCI-compatible container runtime; Docker is used by default. Run `port/switch/build.sh shell` to open the build image.

The first launch compiles Chronicle's Vulkan pipelines with NVK during startup and saves an application pipeline cache in `save/pipeline_cache.bin`. Later launches reuse compatible entries; a missing, incompatible or incomplete cache is rebuilt. Compilation may lengthen the first startup, and internal NVK shaders may compile when their features are first used.

## Install

Copy these files to `sdmc:/switch/chronicle/`:

- `darkcloud.nro` from `port/build/switch/`
- `lang/` and `glyphs/` from `port/build/switch/`
- `data.zip` from the packing step

`data.zip` contains every extracted file, including the normalized copies and language list. It is a standard stored ZIP and stays outside Git. An existing extracted `data/` directory also works; when both are installed, `data.zip` takes precedence. `--data` still names the logical `data/` path beside the archive.

Start Chronicle from the Homebrew Menu with a game's title override by holding R while starting a game. Launching from the album applet leaves too little memory for the game. Runtime logs are written to `darkcloud.log` beside the `.nro`; `save/` stores settings, saves and the Vulkan pipeline cache created at startup.

The Homebrew Menu does not pass arguments to the application. Put whitespace-separated arguments in `args.txt` beside the `.nro`; supported options are listed in [PC port documentation](PC.md).

With asynchronous rendering, Chronicle places the game and render threads on separate cores from the process's allowed CPU mask and logs their actual masks and priorities. Add `--switch-core-affinity auto` to `args.txt` to use libnx's normal scheduling for comparison. This setting has no effect when the render thread is off or the process has fewer than two available cores.

## Platform support

The Switch build supplies guarded arena memory, cooperative context switching, compatibility flags for newlib and logging through nxlink. Its Vulkan driver is [Mesa NVK from Dan's Switch port](https://github.com/danfromtico/mesa-switch), pinned to commit `0ff52079ca9e9e837ed27eb95a8d239f439a1864` (Mesa 26.2.4) and built as a loaderless static library with a direct libnx Horizon backend. The WSI presents scanout-backed images with the GPU completion fence, avoiding a CPU copy on the present path. The output target is 1280 × 720 at 60 Hz in handheld mode and 1920 × 1080 at 60 Hz when docked. The game fills those 16:9 targets, cropping the 4:3 image vertically as needed. The game has not been qualified for gameplay or performance on physical Switch hardware; these are output targets, not measured frame rates.

To send an existing build through the Homebrew Menu's netloader, use `port/switch/run.sh`. Set `SWITCH_ADDRESS` when the console cannot be discovered by broadcast.

## Display

Switch selects its rendering scale from the current console mode: 1.5 for handheld and 2.25 for docked, relative to the game's 480-pixel logical height. Docking and undocking update the scale and output size. The Switch build does not offer resolution or render-scale controls; saved desktop settings and `args.txt` cannot override its output target. Motion interpolation is disabled, while the game's 60 Hz tick rate remains unchanged.

## Emulator testing

Use Eden for Switch runtime testing, loading the freshly built `port/build/switch/darkcloud.nro` rather than an older copy in its game list. For the missing-data check, leave its emulated SD card without game data, verify the installation instructions and `Press + to exit.` appear, then press the controller's mapped + button and verify that Eden returns to its game list. This check passed in Eden v0.2.1 on 2026-10-23. The keyboard binding was M; a temporary toggle binding allowed automated input to span a polling frame and was restored after the test.

All Switch builds try NVK's normal 4 GiB contiguous shader address reservation first and retry at 1 GiB only if the first reservation runs out of device address space. This allows the renderer to start under Eden's smaller virtual-address allocator without lowering the reservation on hardware where 4 GiB succeeds.

The Mesa 26.2.4 NVK migration built a fresh NRO on 2026-10-23, but a bounded launch through Eden nightly `de7e6e01d4`'s `eden-cli.exe` exited with Windows status `0xC0000409` before writing stdout or stderr or producing a render capture. The attempt used an isolated temporary config and did not validate runtime rendering.

The fresh Mesa 26.2.4 NRO was also tested with Ryubing Canary 1.3.351 (Windows x64 package SHA-256 `FD3C0D247A664C1FB670D559E32A5A3AEBB8ADED7A0C33FB95F369F84D829EB0`; package: [Ryubing Canary 1.3.351](https://starforgejo.dev/archive/-/packages/generic/ryubing/canary-1.3.351)). The PAL ISO was extracted with `dcdata extract "rom/pal/Dark Cloud (PAL).iso" "<isolated-trial>/ryubing-data/sdcard/data"`, and Ryubing loaded the NRO with the extracted data. A later rendered-menu check showed the correctly colored language selection screen and confirmed the ZIP-backed texture provenance. This validates asset loading and the menu render path only; no gameplay, Temple scene, or frame-rate result was established. Earlier in the same emulator investigation, Chronicle logged `gfx: vkCreateDevice failed (-2)` and Ryubing logged a fixed-buffer mapping error before rendering. That failed run remains diagnostic evidence for that build and configuration, not a current gameplay result. Emulator config, SD data and logs stayed under isolated trial directories.

## Performance work

The Switch integration has not been measured on physical hardware. Establish a repeatable baseline on physical consoles before attributing slow scenes to the CPU, GPU or storage. Use the same PAL data, save, scripted inputs, area and camera path for each run, with the fixed mode-specific resolution. Record frame times, late presents, CPU and GPU timings, memory use and game ticks. Check screenshots against the existing rendering baseline after every optimization. The separate-core setting needs a physical comparison against `--switch-core-affinity auto`; libnx otherwise allows its worker threads on every process core.

| Console mode | Repeatable scene runs | Capture and timing checks |
|---|---|---|
| Handheld, 720p | Town water and refraction; dungeon shadows and effects; skinned character and cloth; material animation; area transitions, followed by a long run and shutdown | Canonical screenshots; cold and warm pipeline-cache runs; separate-core and `--switch-core-affinity auto` runs in async mode, then render-thread-off runs; native frame times, late presents, CPU/GPU timings and unchanged 60 Hz game ticks |
| Docked, 1080p | The same save, inputs, areas and camera paths as handheld | The same screenshot, cache, core-affinity and timing checks, including dock and undock during a run |

Keep the console mode and every other quality setting fixed while timing one change at a time. Use the [Eden downloads page](https://eden-emu.dev/downloads/) only as a rough reminder to evaluate profile-guided builds: emulator speed on a desktop GPU does not predict native Switch performance. Profile-guided optimization would need representative training runs on a console and a reproducible cross-build before it can be adopted.

The shared Kazarov renderer already has cached camera rebasing, separate bone palettes, shared storage-buffer records, deduplicated uploads and descriptor bindings, cached display-list rewrites, full-cover grab optimization, and device-local host-visible transient memory when supported. Chronicle caches compatible strip runs for static MDT visuals while animated remakes invalidate their runs and upload new vertex positions. Strip order and draw state are preserved. Chronicle's animated MDTs still use CPU skinning; Kazarov bone palettes serve prog0 draws. `DC_PRESENT_STATS=1` reports draw counts and canonical and display render times at exit. Kazarov's Tracy CPU and Vulkan GPU zones are available behind `KZ_PORT_TRACY`, but that instrumentation is not enabled in the Switch build. A profiling build should first establish whether CPU submission, GPU work, synchronization or memory pressure limits each scene.

The build runs glslang on the host and embeds the resulting SPIR-V. NVK compiles GPU binaries as Chronicle creates its application pipelines at startup, then saves a driver-specific cache. The cache UUID includes the Mesa build, chipset and compiler flags; a desktop Vulkan cache is not a substitute for a Switch NVK cache. The first startup needs time for compilation, while compatible subsequent launches can reuse the saved application pipelines. Internal NVK meta pipelines can still compile when first used.

The next performance steps are driven by the scene matrix:

1. Capture CPU submission time, GPU time, present latency, draw counts and memory pressure at both target resolutions. Determine which scenes actually miss the 16.7 ms frame budget.
2. Measure cold startup compilation and warm startup cache reuse on a console. Confirm the cache is rewritten after a driver update and identify any internal NVK pipeline compilation during gameplay.
3. If the CPU limits frames, profile display-list submission, draw calls, animated MDT skinning and archive access. Apply batching or caching only where the trace shows a gain, preserving draw order and gameplay. Compare pinned and automatic core placement.
4. If the GPU limits frames, inspect overdraw, refraction, shadows, texture bandwidth and blend-heavy effects at both fixed resolutions. Change shaders or passes only with image comparisons and timing gains. Treat 1080p as its own target rather than assuming a 720p optimization scales.
5. Once representative console traces exist, evaluate profile-guided optimization and link-time optimization in a reproducible Switch build. Check startup, frame times and binary size against the same baseline.

Packed `data.zip` replaces thousands of loose-file directory lookups with one indexed archive; the supplied `dcdata pack` command stores entries without compression. Compare it with loose data on hardware before crediting it with a frame-time gain.

The proposed CT frustum-culling work was skipped; no new CT frustum path is in scope. GPU skinning and multi-draw indirect submission have not started. Treat them as profiling-dependent follow-ups, and consider them only where measurements identify a bottleneck. For each change, rerun the same scene matrix and validate water and refraction, shadows, cloth and material animation, transitions, unchanged tick counts and asynchronous shutdown.
