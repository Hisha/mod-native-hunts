# Milestone 1 checks

`run_checks.py` performs two database-free checks:

1. compiles `HuntDomain` and `HuntSnapshot` as strict C++17 and runs transition,
   recovery, duplicate-event, abandonment, snapshot, redaction, and unavailable-
   content tests;
2. scans production source/config files for old package, loader, table, migration,
   protocol, and fixed resource identities, and confirms no EPF or SQL tree exists.

Run from the repository root:

```text
python tests/run_checks.py
```

Set `CXX` to override compiler discovery. These checks do not substitute for an
AzerothCore configure/build; they deliberately exercise the core-independent
foundation when a full core checkout is unavailable.
