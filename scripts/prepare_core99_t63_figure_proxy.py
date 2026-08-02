#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T63 Figure-4 terrain and Figure-5 wind-rose digitizer
Paper/DOI: Wind Farm Layout Optimization on Complex Terrains - Integrating a
CFD Wake Model with Mixed-Integer Programming;
10.1016/j.apenergy.2016.06.085
Public source: no paper-linked code/data; inputs are the two raster images
embedded in the hash-verified primary PDF
Missing/reconstruction: private Carleton CFD arrays and numeric wind rose are
absent; this script deterministically samples the published figures
Controlling contract: shared/contracts/core99_t63_kuo_2016.json
Claim boundary: figure-digitized declared proxy, not author CFD data
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
"""
from __future__ import annotations

import argparse
import math
from pathlib import Path
import struct

from PIL import Image


GRID = 20
SECTORS = 12


def squared_rgb_distance(left: tuple[int, ...], right: tuple[int, ...]) -> int:
    return sum((int(a) - int(b)) ** 2 for a, b in zip(left[:3], right[:3]))


def digitize_terrain(path: Path) -> list[float]:
    image = Image.open(path).convert("RGB")
    if image.size != (1124, 845):
        raise RuntimeError(f"unexpected T63 terrain image size: {image.size}")
    # Interior of the 0..2800 m axes in the PDF-embedded raster.
    left, right, top, bottom = 143.0, 876.0, 25.0, 759.0
    # The printed discrete colourbar has 24 20-m bands: 580 down to 120 m.
    palette = [
        image.getpixel((955, int(round(248.0 + 16.0 * band))))
        for band in range(24)
    ]
    elevations: list[float] = []
    # Store row 0 at y=0 (south), consistent with the paper axes.
    for row in range(GRID):
        y_pixel = bottom - (row + 0.5) * (bottom - top) / GRID
        for column in range(GRID):
            x_pixel = left + (column + 0.5) * (right - left) / GRID
            colour = image.getpixel(
                (int(round(x_pixel)), int(round(y_pixel)))
            )
            band = min(
                range(len(palette)),
                key=lambda index: squared_rgb_distance(colour, palette[index]),
            )
            elevations.append(580.0 - 20.0 * band)
    return elevations


def digitize_wind_rose(path: Path) -> list[float]:
    image = Image.open(path).convert("RGB")
    if image.size != (950, 860):
        raise RuntimeError(f"unexpected T63 wind-rose image size: {image.size}")
    centre_x, centre_y = 475.0, 430.0
    maximum_radius = [0.0] * SECTORS
    for y in range(image.height):
        for x in range(image.width):
            red, green, blue = image.getpixel((x, y))
            is_green_bar = (
                green > 180
                and 80 < red < 220
                and blue < 150
                and green > red + 40
            )
            if not is_green_bar:
                continue
            dx, dy = x - centre_x, centre_y - y
            angle = math.degrees(math.atan2(dx, dy)) % 360.0
            sector = int(round(angle / 30.0)) % SECTORS
            delta = abs((angle - sector * 30.0 + 180.0) % 360.0 - 180.0)
            if delta <= 12.0:
                maximum_radius[sector] = max(
                    maximum_radius[sector], math.hypot(dx, dy)
                )
    total = sum(maximum_radius)
    if total <= 0.0:
        raise RuntimeError("cannot detect T63 wind-rose bars")
    return [radius / total for radius in maximum_radius]


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--terrain-image", required=True, type=Path)
    parser.add_argument("--wind-rose-image", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    elevations = digitize_terrain(args.terrain_image)
    probabilities = digitize_wind_rose(args.wind_rose_image)
    payload = bytearray(b"T63PXY1\0")
    payload.extend(struct.pack("<II", GRID, SECTORS))
    payload.extend(struct.pack(f"<{len(elevations)}f", *elevations))
    payload.extend(struct.pack(f"<{len(probabilities)}f", *probabilities))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(payload)
    print(
        f"wrote {args.output} bytes={len(payload)} "
        f"elevation=[{min(elevations):.0f},{max(elevations):.0f}] "
        f"probability_sum={sum(probabilities):.12f}"
    )


if __name__ == "__main__":
    main()
