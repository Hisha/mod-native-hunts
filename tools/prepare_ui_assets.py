#!/usr/bin/env python3
"""Prepare Native Hunts and FrameForge PNG artwork for the WoW 3.3.5a client.

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
FRAMEFORGE_SOURCE = ROOT / "content/WoWUI/assets"
FRAMEFORGE_OUTPUT = ROOT / "content/client/Interface/FrameForge/Dungeon_Finder_UI"


@dataclass(frozen=True)
class Asset:
    source: str
    target: str
    size: tuple[int, int]
    canvas: tuple[int, int]


ASSETS = (
    Asset("hunt_panel_idle.png", "hunt_panel_idle.tga", (280, 212), (512, 256)),
    Asset("hunt_panel_active.png", "hunt_panel_identity.tga", (280, 88), (512, 128)),
    Asset("hunt_panel_progress.png", "hunt_panel_state.tga", (280, 118), (512, 128)),
    Asset("hunt_panel_record.png", "hunt_panel_record.tga", (280, 86), (512, 128)),
    Asset("hunt_divider.png", "hunt_divider.tga", (260, 8), (512, 8)),
    Asset("hunt_icon_standard.png", "hunt_icon_standard.tga", (64, 64), (64, 64)),
    Asset("hunt_icon_elite.png", "hunt_icon_elite.tga", (64, 64), (64, 64)),
    Asset("hunt_icon_turnin.png", "hunt_icon_turnin.tga", (64, 64), (64, 64)),
    Asset("hunt_icon_seal.png", "hunt_icon_seal.tga", (32, 32), (32, 32)),
    Asset("hunt_trail_prints.png", "hunt_trail_prints.tga", (96, 48), (128, 64)),
)

# FrameForge renders the complete source image into each authored object. Resizing the complete
# normalized image to a power-of-two canvas preserves those normalized coordinates when WoW scales
# the texture back to the exact FrameXML geometry.
FRAMEFORGE_ASSETS = (
    Asset("hunt_circle_icon.png", "hunt_circle_icon.tga", (64, 64), (64, 64)),
    Asset("hunt_header.png", "hunt_header.tga", (512, 128), (512, 128)),
    Asset("hunt_icon_elite.png", "hunt_icon_elite.tga", (64, 64), (64, 64)),
    Asset("hunt_icon_finalfight.png", "hunt_icon_finalfight.tga", (128, 128), (128, 128)),
    Asset("hunt_icon_standard.png", "hunt_icon_standard.tga", (64, 64), (64, 64)),
    Asset("hunt_icon_turnin.png", "hunt_icon_turnin.tga", (64, 64), (64, 64)),
    Asset("hunt_panel_active.png", "hunt_panel_active.tga", (512, 256), (512, 256)),
    Asset("hunt_panel_idle.png", "hunt_panel_idle.tga", (512, 512), (512, 512)),
    Asset("hunt_panel_record.png", "hunt_panel_record.tga", (512, 128), (512, 128)),
    Asset("hunt_progress_empty.png", "hunt_progress_empty.tga", (512, 128), (512, 128)),
    Asset("hunt_seal.png", "hunt_seal.tga", (16, 16), (16, 16)),
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


def prepare_frameforge(asset: Asset) -> None:
    image = Image.open(FRAMEFORGE_SOURCE / asset.source).convert("RGBA")
    if not image.getchannel("A").getbbox():
        raise ValueError(f"{asset.source} has no visible pixels")
    write_tga(FRAMEFORGE_OUTPUT / asset.target,
              image.resize(asset.canvas, Image.Resampling.LANCZOS))


def main() -> None:
    OUTPUT.mkdir(parents=True, exist_ok=True)
    expected = {asset.target for asset in ASSETS}
    for old in OUTPUT.glob("*.tga"):
        if old.name not in expected:
            old.unlink()
    for asset in ASSETS:
        prepare(asset)
    FRAMEFORGE_OUTPUT.mkdir(parents=True, exist_ok=True)
    frameforge_expected = {asset.target for asset in FRAMEFORGE_ASSETS}
    for old in FRAMEFORGE_OUTPUT.glob("*.tga"):
        if old.name not in frameforge_expected:
            old.unlink()
    for asset in FRAMEFORGE_ASSETS:
        prepare_frameforge(asset)
    print(f"prepared {len(ASSETS)} legacy and {len(FRAMEFORGE_ASSETS)} FrameForge client textures")


if __name__ == "__main__":
    main()
