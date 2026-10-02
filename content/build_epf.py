#!/usr/bin/env python3
"""Build the Native Hunts EPF deterministically from repository sources."""

from __future__ import annotations

import json
from pathlib import Path
import zipfile


CONTENT = Path(__file__).resolve().parent
OUTPUT = CONTENT / "mod-native-hunts.epf"
ZIP_TIMESTAMP = (1980, 1, 1, 0, 0, 0)


def members() -> tuple[str, ...]:
    manifest = json.loads((CONTENT / "manifest.json").read_text(encoding="utf-8"))
    sources = tuple(entry["source"] for entry in manifest["content"])
    return ("manifest.json", *sources, manifest["clientFrameXml"]["stockTocSource"])


def main() -> None:
    package_members = members()
    with zipfile.ZipFile(OUTPUT, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for member in package_members:
            info = zipfile.ZipInfo(member, ZIP_TIMESTAMP)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            archive.writestr(info, (CONTENT / member).read_bytes())
    print(f"built {OUTPUT} with {len(package_members)} members")


if __name__ == "__main__":
    main()
