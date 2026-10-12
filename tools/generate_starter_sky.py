"""Offline, deterministic cubemap generator. Python standard library only.

Original procedural artwork for RAG Engine; no external imagery or runtime dependency.
Run from any directory: python tools/generate_starter_sky.py
Use --check to verify committed images without writing them.
"""

import argparse
import math
from pathlib import Path
import struct
import zlib

SIZE = 256
FACES = ("px", "nx", "py", "ny", "pz", "nz")
OUTPUT = Path(__file__).resolve().parents[1] / "assets" / "skyboxes" / "starter"


def direction(face, u, v):
    """u goes left->right and v top->bottom, both in [-1, 1]."""
    xyz = ((1, -v, -u), (-1, -v, u), (u, 1, v),
           (u, -1, -v), (u, -v, 1), (-u, -v, -1))[face]
    length = math.sqrt(sum(c * c for c in xyz))
    return tuple(c / length for c in xyz)


def sky_color(d):
    # One continuous directional function for all faces, including the lower
    # hemisphere. No per-face gradients, borders, lighting or random seeds.
    x, y, z = d
    horizon = (0.66, 0.73, 0.78)
    pole = (0.045, 0.20, 0.48) if y >= 0 else (0.075, 0.11, 0.15)
    blend = abs(y) ** 0.55
    color = [horizon[i] * (1 - blend) + pole[i] * blend for i in range(3)]
    # Broad warm landmark towards +X/-Z, useful for checking yaw and seams.
    glow = math.exp(18 * ((0.70014 * x + 0.14 * y - 0.70014 * z) - 1))
    color = [c + glow * tint for c, tint in zip(color, (0.28, 0.14, 0.025))]

    def srgb(c):
        c = max(0, min(1, c))
        return round(255 * (12.92 * c if c <= 0.0031308 else 1.055 * c ** (1 / 2.4) - 0.055))

    return bytes(srgb(c) for c in color)


def png(face):
    rows = bytearray()
    for row in range(SIZE):
        rows.append(0)  # PNG filter: none
        for col in range(SIZE):
            rows.extend(sky_color(direction(face, 2 * (col + 0.5) / SIZE - 1,
                                            2 * (row + 0.5) / SIZE - 1)))

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))

    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", SIZE, SIZE, 8, 2, 0, 0, 0))
            + chunk(b"sRGB", b"\x00") + chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b""))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    if not args.check:
        OUTPUT.mkdir(parents=True, exist_ok=True)
    for face, name in enumerate(FACES):
        path = OUTPUT / (name + ".png")
        data = png(face)
        if args.check:
            if path.read_bytes() != data:
                raise SystemExit(f"Different from deterministic generator: {path}")
        else:
            path.write_bytes(data)
    print(f"{'Verified' if args.check else 'Generated'} six {SIZE}x{SIZE} sRGB cubemap faces in {OUTPUT}")


if __name__ == "__main__":
    main()
