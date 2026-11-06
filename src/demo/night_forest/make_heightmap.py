#!/usr/bin/env python3
"""Generate the two height-field inputs the experiment manual's 3.2 reads.

Not part of the build: it exists so the doc's commands can be re-run by anyone.
Both files are small and derived from closed-form functions, so a regeneration
is bit-identical (no RNG involved). They land in `outputs/.tmp/` (gitignored),
never next to this script - the repository keeps no binary assets in `src/`.

  outputs/.tmp/contour.png        512x512 8-bit gray   -> --terrain-map (image path)
  outputs/.tmp/contour_banded.png 512x512, already terraced in the source
  outputs/.tmp/ridge.raw          384x384 16-bit LE    -> --terrain-map (raw path)
"""
import math
import struct
from pathlib import Path

OUT_DIR = Path(__file__).resolve().parents[3] / "outputs" / ".tmp"


def hill(x, y, cx, cy, r):
    """Gaussian bump, used as metres of a synthetic survey map."""
    dx, dy = (x - cx) / r, (y - cy) / r
    return math.exp(-(dx * dx + dy * dy))


def field(x, y, n):
    """A broad swell, three hills, a ridge and a gully, in 0..1.

    The swell term matters: without it most of the map is a single flat value, and
    a survey sheet that is 70% plain demonstrates nothing about contour reading.
    """
    u = x / (n - 1)
    v = y / (n - 1)
    h = 0.30 + 0.20 * math.sin(2.3 * math.pi * u) * math.cos(1.7 * math.pi * v)
    h += 0.55 * hill(x, y, 0.30 * n, 0.35 * n, 0.13 * n)
    h += 0.40 * hill(x, y, 0.72 * n, 0.60 * n, 0.18 * n)
    h += 0.30 * hill(x, y, 0.55 * n, 0.18 * n, 0.09 * n)
    h += 0.25 * math.exp(-((v - 0.5) / 0.06) ** 2) * (0.5 + 0.5 * math.sin(6.0 * u * math.pi))
    h -= 0.30 * math.exp(-((u - 0.45) / 0.035) ** 2)   # the gully the camera must cross
    return min(max(h, 0.0), 1.0)


def write_png(path, n, bands):
    """Grayscale PNG; bands>0 quantises it into contour lines like a survey sheet."""
    from PIL import Image
    img = Image.new("L", (n, n))
    px = img.load()
    for y in range(n):
        for x in range(n):
            v = field(x, y, n)
            if bands > 1:
                v = math.floor(v * bands) / (bands - 1)
            px[x, y] = round(v * 255)
    img.save(path)


def write_raw(path, n):
    """Headerless 16-bit little-endian grid - the manual's loadRawFile format."""
    with open(path, "wb") as f:
        for y in range(n):
            for x in range(n):
                f.write(struct.pack("<H", round(field(x, y, n) * 65535)))


if __name__ == "__main__":
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    write_png(OUT_DIR / "contour.png", 512, 0)         # smooth picture-derived field
    write_png(OUT_DIR / "contour_banded.png", 512, 8)  # already terraced in the source
    write_raw(OUT_DIR / "ridge.raw", 384)
    for p in sorted(OUT_DIR.glob("contour*.png")) + [OUT_DIR / "ridge.raw"]:
        print(f"{p.name:20s} {p.stat().st_size:>9d} bytes  {p.parent}")
