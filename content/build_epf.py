#!/usr/bin/env python3
"""Build the Native Hunts EPF deterministically from repository sources."""

from __future__ import annotations

import json
from pathlib import Path
import zipfile

CONTENT = Path(__file__).resolve().parent
OUTPUT = CONTENT / "mod-native-hunts.epf"
ZIP_TIMESTAMP = (1980, 1, 1, 0, 0, 0)


def _safe_relative(value: str) -> str:
    path = Path(value)
    if not value or path.is_absolute() or ".." in path.parts or "\\" in value:
        raise ValueError(f"unsafe EPF member path: {value!r}")
    return path.as_posix()


def members() -> tuple[str, ...]:
    manifest = json.loads((CONTENT / "manifest.json").read_text(encoding="utf-8"))
    found = {"manifest.json"}
    found.update(_safe_relative(entry["source"]) for entry in manifest.get("content", []))
    frame_xml = manifest.get("clientFrameXml")
    if frame_xml:
        found.add(_safe_relative(frame_xml["stockTocSource"]))
    for package in manifest.get("frameForgeWowUi", []):
        root = CONTENT / _safe_relative(package["source"])
        if not root.is_dir():
            raise FileNotFoundError(f"FrameForge package directory is missing: {root}")
        for path in root.rglob("*"):
            if path.is_file():
                found.add(path.relative_to(CONTENT).as_posix())
    return tuple(sorted(found))


def main() -> None:
    package_members = members()
    with zipfile.ZipFile(OUTPUT, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for member in package_members:
            source = CONTENT / member
            if not source.is_file():
                raise FileNotFoundError(f"EPF member is missing: {member}")
            info = zipfile.ZipInfo(member, ZIP_TIMESTAMP)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            archive.writestr(info, source.read_bytes())
    print(f"built {OUTPUT} with {len(package_members)} members")


if __name__ == "__main__":
    main()

