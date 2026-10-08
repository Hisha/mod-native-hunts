# Native Hunts checks

`run_checks.py` performs five database-free check groups:

1. a duplicate-definition guard that parses every `tests/*.py` script and
   rejects top-level functions or classes defined more than once (a later
   definition silently shadows the earlier one);
2. strict C++17 tests for domain transitions, restart recovery, tracking kill
   eligibility, Local/Continent/World filtering, zone deduplication, ambush
   thresholds/pending tracking exclusion, crystal ownership/location checks,
   failed-spawn recovery, final
   prey identity, Return Rift ownership, turn-in bag-space refusal, managed
   resource unavailability, abandonment, snapshots, all 195 unique authored
   sites, regional counts, and per-zone site pools;
3. the functional client contract of the known-working version-18 package:
   synchronized package sources, the exact pinned build-12340 FrameXML
   baseline, additive LFD-shell load ordering, passive UI safety, all ten
   Huntmasters and spawns, all sixteen standard prey, both clean
   scripted-button runtime templates, the physical Seal declaration, and
   removal of disposable validation symbols; packages that ship only static
   FrameForge layout fail this contract;
4. the static FrameForge WoWUI packaging contract for packages that declare
   `frameForgeWowUi` (currently package version 19): deterministic EPF member
   set and hashes, portable source artwork, and layout-only load entries;
   it is skipped for packages with functional FrameXML content;
5. native-only source/persistence safety checks that reject legacy package,
   table, protocol, migration, world-database assignment placement, undocumented
   configuration reads, and hard-coded allocated identities, and require
   the single-transaction inventory/statistics/assignment turn-in safeguards.

Every check group runs even if an earlier one fails; the script prints
`FAIL <group>: <reason>` per group and exits non-zero if any group failed.

Run from the repository root:

```text
python3 tests/run_checks.py
```

Set `CXX` to override compiler discovery. A full AzerothCore configure/build and
PTR database/content activation are still required because these local checks
do not compile the AzerothCore-facing integration layer.
