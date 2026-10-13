#!/usr/bin/env python3
"""Rebuild the original, tileable material maps shipped with the port.

Uses only Python's standard library; no downloaded textures or image packages.
The height fields are periodic, so their derived tangent-space normal maps tile.
"""

from __future__ import annotations

import json
import math
import pathlib
import struct
import zlib

ROOT = pathlib.Path(__file__).resolve().parents[2]
OUT = ROOT / "assets" / "materials"
SIZE = 128

# Roughness, metalness, height amplitude, frequency and characteristic.
PROFILES = {
    "wood_floor": (0.68, 0.0, 0.055, 16, "grain"),
    "wood_wall": (0.79, 0.0, 0.060, 12, "grain"),
    "cloth": (0.88, 0.0, 0.018, 42, "weave"),
    "leather": (0.83, 0.0, 0.027, 21, "pebble"),
    "skin": (0.92, 0.0, 0.008, 27, "pebble"),
    "metal": (0.38, 1.0, 0.012, 34, "brushed"),
    "stone": (0.89, 0.0, 0.044, 11, "stone"),
    "prop": (0.78, 0.0, 0.026, 18, "pebble"),
    "flat": (0.95, 0.0, 0.0, 1, "flat"),
}


def png(path: pathlib.Path, pixels: bytes) -> None:
    def chunk(kind: bytes, payload: bytes) -> bytes:
        data = kind + payload
        return struct.pack(">I", len(payload)) + data + struct.pack(">I", zlib.crc32(data))

    rows = b"".join(b"\x00" + pixels[y * SIZE * 4:(y + 1) * SIZE * 4] for y in range(SIZE))
    path.write_bytes(
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">2I5B", SIZE, SIZE, 8, 6, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(rows, 9))
        + chunk(b"IEND", b"")
    )


def height(x: int, y: int, frequency: int, kind: str) -> float:
    u, v = x / SIZE, y / SIZE
    tau = 2 * math.pi
    if kind == "grain":
        # Long fine wood fibres and staggered rings, no high-contrast random noise.
        return (0.70 * math.sin(tau * (frequency * v + 0.22 * math.sin(tau * 2 * u)))
                + 0.24 * math.sin(tau * (3 * frequency * v + 0.5 * math.sin(tau * u))))
    if kind == "weave":
        return 0.5 * math.sin(tau * frequency * u) + 0.5 * math.sin(tau * frequency * v)
    if kind == "brushed":
        return 0.7 * math.sin(tau * frequency * v) + 0.2 * math.sin(tau * 3 * frequency * v)
    if kind == "stone":
        return (0.48 * math.sin(tau * (frequency * u + 3 * v))
                + 0.43 * math.cos(tau * (frequency * v - 2 * u)))
    if kind == "pebble":
        return (0.45 * math.sin(tau * frequency * u) * math.sin(tau * frequency * v)
                + 0.25 * math.cos(tau * (frequency // 2 * u + frequency // 3 * v)))
    return 0.0


def byte(value: float) -> int:
    return max(0, min(255, round(value * 255)))


def generate() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    entries = {}
    for name, (roughness, metalness, amplitude, frequency, kind) in PROFILES.items():
        normal = bytearray()
        orm = bytearray()
        for y in range(SIZE):
            for x in range(SIZE):
                dx = (height((x + 1) % SIZE, y, frequency, kind)
                      - height((x - 1) % SIZE, y, frequency, kind)) * amplitude
                dy = (height(x, (y + 1) % SIZE, frequency, kind)
                      - height(x, (y - 1) % SIZE, frequency, kind)) * amplitude
                length = math.sqrt(dx * dx + dy * dy + 1.0)
                normal.extend((byte(0.5 - dx / (2 * length)),
                               byte(0.5 - dy / (2 * length)),
                               byte(0.5 + 1 / (2 * length)), 255))
                variation = 0.025 * height(x, y, frequency, kind) if kind != "flat" else 0.0
                orm.extend((255, byte(roughness + variation), byte(metalness), 255))
        png(OUT / f"{name}_normal.png", bytes(normal))
        png(OUT / f"{name}_orm.png", bytes(orm))
        entries[name] = {
            "normal": f"{name}_normal.png",
            "orm": f"{name}_orm.png",
            "note": "Original periodic procedural detail; authored UVs are retained",
        }
    manifest = {
        "version": 1,
        "orm_channels": {"r": "ambient_occlusion", "g": "perceptual_roughness", "b": "metalness"},
        "texture_assignments": {
            "wood_floor": ["d03b01", "d03b04"],
            "wood_wall": ["d03b02", "d03b03", "d03b06", "d03b07"],
            "cloth": ["c01d02", "c01d04", "c01d05"],
            "leather": ["c01d03"],
            "skin": ["c01d01"],
            "metal": ["c01w01"],
            "prop": ["d03b13"],
        },
        "texture_prefix_assignments": {
            "cloth": ["c01d"],
            "metal": ["c01w"],
        },
        "unlisted_textured_shipwreck_material": "prop",
        "profiles": entries,
    }
    (OUT / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    generate()
