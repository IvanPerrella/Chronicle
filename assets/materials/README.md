These CC0 assets contain original procedural material detail and independently
authored atlas material annotations. The generator reads no game images. Retail
albedo textures, transparency and UV coordinates remain in the user's extracted
game data; no retail pixels are distributed here.

Run `python tools/materials/generate_material_maps.py` from the repository to
regenerate the PNGs and manifest. Add `--check` to verify byte-identical output
without writing files. Validation checks RGBA8 encoding, PNG CRCs and raster
lengths, every manifest reference, paired map dimensions, atlas mask dimensions,
variant fingerprints, and stale or unreferenced generated PNGs.
The current set contains 316 exact texture records, 120 content variants across
40 colliding names, and 702 RGBA8 PNGs, including region masks.

The version 2 manifest uses exact normalized texture names. Texture records carry
an explicit physical material or an interface/effect exclusion. There are no
prefix assignments or authored-coverage claims for a generic fallback. Mixed
atlases use composite normal and ORM maps plus an audit region mask. Rectangles
and polygons use a 256-unit UV coordinate extent, with later regions overriding
earlier regions. Region masks store the stable material semantic ID in red and
serve as audit artifacts; shaders consume the composite normal and ORM maps.

Normal RGB stores tangent-space normals. Normal alpha zero excludes bump detail
and physical specular response on interface, transparent-empty, or effect
regions; positive alpha stores physical detail strength. Smooth physical surfaces
may use neutral normal RGB `(0.5, 0.5, 1)` with positive alpha. ORM channels are
ambient occlusion, perceptual roughness, metalness, and wet coverage. Dry surfaces
have zero ORM alpha and retain physical normals. Scene and surface orientation eligibility
still gate wetness. Gallery wet coverage is authored for cracked floor atlases
and the rightmost quarter of its mixed brick/floor atlas. Its plaster, brick,
carpet and architectural trim remain dry.

Shipwreck atlas `d03b07` includes a riveted, corroded hatch plate used by both
floor and ceiling triangles; UV `(0.142952, 0.584398)` lies inside that plate.
Its oxide/paint finish retains the metal semantic but uses roughness 0.88,
metalness 0.10, and shallow irregular corrosion detail. The coin island retains
worn conductive metal, the green riveted hatch uses the corroded finish, and
surrounding liquid/effect islands exclude physical response. Generator checks
protect these UV-specific properties and reject one-axis brushed hatch detail.

Reused names with different content have exact variants keyed by FNV-1a64 over
little-endian u32 width, little-endian u32 height, then decoded row-major RGBA8
bytes. Texture entry registers the fingerprint after runtime preprocessing and
overwrites it when a registry key is reused; material unload clears registrations.
For a record with variants, lookup requires a registered matching fingerprint,
including for the main-dungeon image. Missing or unknown fingerprints are reported
as unresolved and do not inherit the top-level material.

The exact records cover the resource union of all front/back Shipwreck and
Gallery enemy placement tables, their environment packs and boss variants,
the six playable character resource sets, all 111 existing equippable weapon
texture names, and the inspected runtime interface/effect atlases. The runtime
fixtures verify a subset; the remaining coverage comes from offline resource
and material audits. Eight offline/runtime RGBA spot checks match byte-for-byte,
covering Shipwreck/Gallery environment atlases and enemies. Recolored variants
reuse physical maps when their material layout is unchanged. Changed carpet and
boss-room layouts have separate maps.

To extend coverage, inspect extracted textures locally and add exact records,
independent material-region annotations, and original procedural detail in the
generator. Keep retail pixels and inspection dumps outside Git, in `data/` or
`.kazarov/`; commit only the generator, metadata, and original generated maps.
For reused names, include every audited post-preprocessing content fingerprint
and select separate maps wherever the material layout changes. Regenerate, run
`--check`, and verify the affected runtime texture census and appearance.
