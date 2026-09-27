# Native Hunts checks

`run_checks.py` performs three database-free checks:

1. strict C++17 tests for domain transitions, restart recovery, tracking kill
   eligibility, crystal ownership/location checks, failed-spawn recovery, final
   prey identity, Return Rift ownership, turn-in bag-space refusal, managed
   resource unavailability, abandonment, snapshots, all 195 unique authored
   sites, regional counts, and per-zone site pools;
2. the managed EPF contract: all ten Huntmasters and spawns, all sixteen
   standard prey, both runtime object templates, the physical Seal declaration,
   and removal of disposable validation symbols;
3. native-only source/persistence safety checks that reject legacy package,
   table, protocol, migration, and hard-coded allocated identities, and require
   the single-transaction inventory/statistics/assignment turn-in safeguards.

Run from the repository root:

```text
python3 tests/run_checks.py
```

Set `CXX` to override compiler discovery. A full AzerothCore configure/build and
PTR database/content activation are still required because these local checks
do not compile the AzerothCore-facing integration layer.
