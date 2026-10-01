#!/usr/bin/env python3
"""Build the Native Hunts EPF deterministically from repository sources."""

from __future__ import annotations

from pathlib import Path
import zipfile


CONTENT = Path(__file__).resolve().parent
OUTPUT = CONTENT / "mod-native-hunts.epf"
MEMBERS = (
    "manifest.json",
    "client/Interface/FrameXML/NativeHuntsFrame.xml",
    "client/Interface/FrameXML/NativeHuntsFrame.lua",
    "upstream/FrameXML.toc",
)
ZIP_TIMESTAMP = (1980, 1, 1, 0, 0, 0)


def main() -> None:
    with zipfile.ZipFile(OUTPUT, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for member in MEMBERS:
            info = zipfile.ZipInfo(member, ZIP_TIMESTAMP)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            archive.writestr(info, (CONTENT / member).read_bytes())
    print(f"built {OUTPUT} with {len(MEMBERS)} members")


if __name__ == "__main__":
    main()
