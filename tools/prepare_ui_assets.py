#!/usr/bin/env python3
"""Prepare source Native Hunts PNG artwork for the WoW 3.3.5a client.

The source PNGs remain untouched.  Each usable component is alpha-trimmed,
resampled to its authored in-game size, placed at the top-left of a
power-of-two canvas, and written as an uncompressed 32-bit top-origin TGA.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import struct

from PIL import Image


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "assets"
OUTPUT = ROOT / "content/client/Interface/NativeHunts"


@dataclass(frozen=True)
class Asset:
    source: str
    target: str
    size: tuple[int, int]
    canvas: tuple[int, int]


ASSETS = (
    Asset("hunt_panel_idle.png", "hunt_panel_idle.tga", (328, 244), (512, 256)),
    Asset("hunt_panel_active.png", "hunt_panel_identity.tga", (328, 82), (512, 128)),
    Asset("hunt_panel_progress.png", "hunt_panel_state.tga", (328, 156), (512, 256)),
    Asset("hunt_panel_record.png", "hunt_panel_record.tga", (328, 90), (512, 128)),
    Asset("hunt_divider.png", "hunt_divider.tga", (308, 8), (512, 8)),
    Asset("hunt_icon_standard.png", "hunt_icon_standard.tga", (64, 64), (64, 64)),
    Asset("hunt_icon_elite.png", "hunt_icon_elite.tga", (64, 64), (64, 64)),
    Asset("hunt_icon_turnin.png", "hunt_icon_turnin.tga", (64, 64), (64, 64)),
    Asset("hunt_icon_seal.png", "hunt_icon_seal.tga", (32, 32), (32, 32)),
    Asset("hunt_trail_prints.png", "hunt_trail_prints.tga", (96, 48), (128, 64)),
)


def write_tga(path: Path, image: Image.Image) -> None:
    width, height = image.size
    header = struct.pack(
        "<BBBHHBHHHHBB",
        0, 0, 2, 0, 0, 0, 0, 0, width, height, 32, 0x28,
    )
    rgba = image.convert("RGBA").tobytes()
    bgra = bytearray(len(rgba))
    for offset in range(0, len(rgba), 4):
        red, green, blue, alpha = rgba[offset:offset + 4]
        bgra[offset:offset + 4] = bytes((blue, green, red, alpha))
    path.write_bytes(header + bgra)


def prepare(asset: Asset) -> None:
    source_path = SOURCE / asset.source
    image = Image.open(source_path).convert("RGBA")
    alpha = image.getchannel("A")
    if alpha.getextrema()[0] == 255:
        raise ValueError(f"{asset.source} has no transparency")
    bounds = alpha.getbbox()
    if not bounds:
        raise ValueError(f"{asset.source} has no visible pixels")
    image = image.crop(bounds).resize(asset.size, Image.Resampling.LANCZOS)
    canvas = Image.new("RGBA", asset.canvas, (0, 0, 0, 0))
    canvas.alpha_composite(image, (0, 0))
    write_tga(OUTPUT / asset.target, canvas)


def main() -> None:
    OUTPUT.mkdir(parents=True, exist_ok=True)
    expected = {asset.target for asset in ASSETS}
    for old in OUTPUT.glob("*.tga"):
        if old.name not in expected:
            old.unlink()
    for asset in ASSETS:
        prepare(asset)
    print(f"prepared {len(ASSETS)} client textures in {OUTPUT}")


if __name__ == "__main__":
    main()
