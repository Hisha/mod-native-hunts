#!/usr/bin/env python3
"""Compile and run the database-free Milestone 1 checks."""

from __future__ import annotations

import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def compiler() -> str:
    configured = os.environ.get("CXX")
    if configured:
        return configured
    for candidate in ("g++", "clang++"):
        resolved = shutil.which(candidate)
        if resolved:
            return resolved
    raise RuntimeError("No C++17 compiler found; set CXX to a compiler path")


def run_domain_tests() -> None:
    # Cygwin toolchains cannot reliably execute binaries from the Windows user
    # temp directory in restricted environments. Keep ephemeral output beneath
    # the repository, matching AzerothCore module test conventions.
    with tempfile.TemporaryDirectory(prefix=".native-hunts-m1-", dir=ROOT) as directory:
        executable = Path(directory) / ("hunt_domain_tests.exe" if os.name == "nt" else "hunt_domain_tests")
        command = [compiler(), "-std=c++17", "-Wall", "-Wextra", "-Werror", "-pedantic", "-Isrc",
            "tests/hunt_domain_tests.cpp", "src/HuntDomain.cpp", "src/HuntSnapshot.cpp", "-o", str(executable)]
        subprocess.run(command, cwd=ROOT, check=True)
        subprocess.run([str(executable)], cwd=ROOT, check=True)
        print("PASS hunt domain and snapshot tests", flush=True)


def run_source_safety_checks() -> None:
    prohibited = {
        "legacy package key": '"mod-hunts"',
        "old loader": "Addmod_huntsScripts",
        "old runtime table": "hunt_runtime",
        "old statistics table": "hunt_stats",
        "old import table": "hunt_currency_realm",
        "old migration component": "HuntCurrencyMigration",
        "old client protocol": '"HUNTS"',
        "old crystal entry": "14999010",
        "old rift entry": "14999011",
        "old Huntmaster entry": "14999980",
    }
    production = list((ROOT / "src").glob("**/*")) + list((ROOT / "conf").glob("**/*"))
    for path in production:
        if not path.is_file():
            continue
        text = path.read_text(encoding="utf-8")
        for description, token in prohibited.items():
            if token in text:
                raise AssertionError(f"{description} found in {path.relative_to(ROOT)}: {token}")
    obsolete_directories = (ROOT / "content", ROOT / "data" / "sql")
    for obsolete in obsolete_directories:
        if obsolete.exists() and any(path.is_file() for path in obsolete.rglob("*")):
            raise AssertionError(f"obsolete milestone content remains: {obsolete.relative_to(ROOT)}")
    obsolete_loader = ROOT / "src" / "mod_hunts_loader.cpp"
    if obsolete_loader.exists():
        raise AssertionError(f"obsolete milestone content remains: {obsolete_loader.relative_to(ROOT)}")
    print("PASS clean-foundation source safety checks", flush=True)


def main() -> None:
    run_domain_tests()
    run_source_safety_checks()


if __name__ == "__main__":
    main()
