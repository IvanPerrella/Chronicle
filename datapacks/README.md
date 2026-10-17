# Datapacks

Put each pack in its own folder here and enable it in `datapacks.json`:

```json
{
  "schema_version": 1,
  "load_order": ["base-art", "my-art"]
}
```

Later folders override earlier ones. Asset names ignore ASCII case. The game
looks for this directory beside the selected `data/` directory, including when
`--data` or `DC_DATA` selects a different location.

Copy the desired files from `data/textures/` into the same relative paths inside
your pack. For example, `data/textures/img/example.img/0.png` becomes
`datapacks/my-art/textures/img/example.img/0.png`. The `texture.json` in each
texture folder lists the PNGs, dimensions, palette format and mip levels. It can
also be overridden at its corresponding path. Packs need only contain changed
files; the remaining assets come from extracted data.

Keep PNG dimensions unchanged. Indexed textures must remain non-interlaced
indexed PNGs with the original palette size and index assignments; changing a
palette entry recolours every pixel using that index. Mips share the base PNG's
palette. True-colour textures accept ordinary PNGs. PNG alpha uses 0–255; the
loader converts it to the game's alpha units. Texture buffers retain their
original sizes, so JSON overrides must preserve their layout.

Extracted PNGs and installed packs are ignored by Git. The shipped configuration
enables no packs.
