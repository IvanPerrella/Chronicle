# Enhanced dungeon renderer: implementation and validation

The [lighting plan](ENHANCED_LIGHTING_PLAN.md) records the rendering direction and NVIDIA references. Enhanced rendering is opt-in. Shipwreck uses the cool, damp treatment from `Dark Cloud_ Gunny in the Fog.png`; Gallery of Time uses violet fire and depth haze. These are separate scene settings, not a global colour grade.

## Rendering and material assets

The enhanced path shades world geometry in linear light into RGBA16F, applies depth/height fog, local fire scattering, bloom and tone mapping, then draws the HUD into an SDR target. Canonical game feedback remains separate. Display-sized render-target twins keep attachments compatible with the internal DLSS resolution and preserve the enhanced scene for inventory captures.

Vulkan ray queries provide contact AO, directional and local-light shadows, rough reflections and a bounded diffuse bounce. Fire positions and flicker come from `CFireOmni`. Enhanced dungeon lighting omits the corresponding projected floor-light overlays. Rough upward Shipwreck wood does not trace a coherent mirror reflection; eligible stone and exposed metal retain secondary reflection rays.

The executable-relative `assets/materials` package contains 702 original RGBA8 PNGs, including audit region masks, with 316 exact texture records and 120 content variants across 40 reused texture names. Runtime loads 476 normal/ORM images after deduplication. Physical properties distinguish wood, metal, cloth, leather, skin, stone, grass, moss and other surfaces; interface/effect regions explicitly exclude physical response. Mixed atlases use per-texel normal and ORM maps. ORM stores ambient occlusion, perceptual roughness, metalness and wet-film coverage; normal alpha controls physical response coverage and detail strength.

Texture fingerprints select the correct material maps when names are reused by different dungeon or boss atlases. Unknown fingerprints are reported as unresolved rather than silently inheriting another variant. The audited resource union includes all front/back Shipwreck and Gallery enemy placement sets, environment packs and boss variants, the six playable character sets and 111 existing weapon texture names. This is an offline coverage audit; runtime fixtures exercise a subset, not every possible floor and object combination. Eight offline/runtime RGBA spot checks matched byte-for-byte.

Maps are reproducible with `python tools/materials/generate_material_maps.py`; `--check` validates deterministic output, PNG integrity, dimensions, references, variants and targeted material properties. The final package digest is `924b21282bdd8afe08605859b84f64eb9969c9661de438890499ca4e30f0a69d`. Assets are original procedural detail and independent material annotations, not distributed retail textures. See [asset contracts and provenance](../assets/materials/README.md).

## Shipwreck glint and deck correction

The user-supplied `glint.rdc` capture identifies the white patch at pixel `(1802, 1839)` in HDR image 198, draw 3227, primitive 3. It is present before atmosphere and tone mapping. The triangle has upward normals, world height 10, and interpolated UV approximately `(0.142952, 0.584398)` in atlas `d03b07`.

That UV lies on a heavily corroded riveted plate. Its previous ORM map described almost bare metal: roughness about 0.417, metalness about 0.922, with regular brushed normal bands. The corrected oxide/paint finish retains metal semantics but uses roughness about 0.875 and metalness about 0.102 at the marked UV, with shallow irregular corrosion detail. Exposed coins retain a worn conductive finish; the green hatch receives the corroded finish, and surrounding effect regions exclude physical shading. The main atlas profile is Metal; the separate boss variant remains Stone. Generator checks protect these UV-specific properties.

A CPU reconstruction using the captured geometry, textures and light constants closely reproduced the bright directional specular term. The corrected finish predicts roughly sevenfold lower direct radiance and no longer qualifies that corroded region for a mirror-reflection query. This is supporting analysis, not an exact GPU before/after result. Attempts to reproduce the captured hatch camera in a newly generated dungeon were obstructed by neighboring geometry; those screenshots are not accepted as visual proof of the exact hatch correction.

A separate integration regression made damp-wood albedo absorption follow sparse wet-film alpha. About 64% of deck texels then bypassed absorption, exaggerating pale painted fibres. The shared primary/ray-hit evaluator now applies moist-substrate absorption to eligible upward wood independently of wet-film coverage, reduced by metalness. Wet-film roughness, local sheen and ray eligibility retain their own controls.

Matched Shipwreck surface-detail off/full captures showed that the broad pale deck streaks persisted without normal mapping. The subsequent same-view substrate correction darkened those fibres while retaining rough, non-mirror boards. Left/right camera rotations also showed the corrected deck without a broad white patch; the specific hatch from the RenderDoc capture was not visible in those views. Local evidence is in `.kazarov/materials-shipwreck-detail-{off,full}-800.png`, `materials-shipwreck-substrate-800.png` and `shipwreck-yaw-{left,right}-536.png`.

## Atmosphere and presentation checks

Fog uses a stable collision-derived 20-by-20 deck-height field instead of Toan's changing ground height. Scene changes invalidate the field. Shipwreck culling accounts for cell bounds so room geometry arrives before haze exposes it. Earlier natural doorway captures at frames 590, 592 and 596 showed continuous haze across the room switch; this traversal was not repeated for the latest material-only correction.

Atmosphere colour, density, distance and local-light limits are read from the complete scene settings block. Gallery's local lights use the actual purple fire positions and flicker, with sufficient radius to reach the floor. A current Gallery smoke run (`--jump dungeon:5`) showed violet stone, the white-core purple flame and separate cloth/stone surfaces, and exited successfully. The tested floor remained fairly matte; a corridor view matching the user's reference composition and reflection balance has not been validated. Dungeon ID 6 is Demon Shaft and is not Gallery evidence.

The hook intro holds black until its camera is ready to reveal Toan. Current clean Release captures at frames 175, 300 and 420 with `tools/windows/shipwreck-capture.input` showed black, Toan ascending on the hook under the fade, and subsequent gameplay respectively; all runs exited successfully. The earlier floor-selection-only captures without the confirm input are not intro evidence.

Inventory uses the captured enhanced SDR scene rather than reverting to canonical lighting. Atmospheric Lighting, Bloom, Ray Tracing, dungeon fog/lamps, Surface Detail, DLSS quality and Frame Generation settings apply at runtime through the shared configuration-change path. Resolution and lighting changes reset history at a safe frame boundary. Earlier live-setting and presentation tests passed; validation of the latest build is recorded below when available.

## Windows, DLSS and dungeon audio

Windows builds use Docker through `build.bat` and `run.bat`, with parallel build jobs. Debug and Release builds passed during the integration; the latest material fixes also compile in the clean Release. All three enhanced fragment variants compile and pass SPIR-V validation. Temporary camera-placement and draw-trace fixtures have been removed from production source. No PS2 source changes are required.

The merged renderer's `vkQueueSubmit2` device-loss regression came from full-display colour attachments paired with DLSS-sized depth attachments. Restoring correctly sized target twins, routing and lifetime handling resolved that mismatch. A normal windowed `run.bat` test with 3840-by-2160 output, DLSS Quality input 2560-by-1440, RT and Frame Generation completed 800 application frames successfully. Streamline reported 1,775 real frames, 3,519 shown and 1,744 generated (`.kazarov/merge-windowed-full-800.log`). These are SDK totals, not direct monitor scanout measurements.

Streamline uses native Vulkan device creation followed by SDK registration. DLSS receives depth, jitter and motion vectors; Reflex and the display-frame index are integrated. Frame Generation requests the compatible presentation mode while preserving the saved V-Sync preference. Ordinary screenshot readback captures real application frames, not generated intermediate frames.

Direct `--jump dungeon` entry bypassed the loader step that requests dungeon BGM. The native jump path now sets `EdEventInfo.map_jump_bgm_play` before `MapJump`, matching the regular loader. The corrected Shipwreck trace loads `d03a_a.sq` and starts port 0, sequence 0, volume 88, alongside ambient audio. `.kazarov/audio-directjump-fixed.log` and its WAV verify sequence and mixer output. WAV capture bypasses the SDL playback device, so it does not prove sound through the user's speakers.

The final clean Release was also exercised directly in a normal-audio windowed 800-tick run (`.kazarov/final-normal-audio-windowed-800.log/.png`), exiting successfully at 4K with RT and DLSS Quality. Streamline reported 1,348 real frames, 2,669 shown and 1,321 generated. It loaded 476 material images and 316 records, reported no Vulkan error and no audio-device fallback. The screenshot shows stable Shipwreck gameplay; it is not the captured hatch angle. Physical speaker output and a fresh live inventory/HDR-toggle sequence were not independently verified in this final run.

## Remaining limits

Ray structures contain geometry submitted by the game, not an independently loaded whole dungeon. Off-list geometry may be absent from reflections or shadows. Current-pose character cloth is included. The bounded diffuse bounce is not converged global illumination. Procedural normal maps approximate material structure rather than reproducing original sculpted detail. Visual similarity to both reference images remains an artistic comparison; the capture-specific hatch runtime comparison and Gallery corridor/reflection match remain unverified.
