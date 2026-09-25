# mod-native-hunts

`mod-native-hunts` is a new, independent native Hunt module for AzerothCore
WotLK / WoW 3.3.5a. The project is being designed from the ground up; it is not
a compatibility continuation of the standalone Hunt implementation.

Milestone 1 contains only a clean module foundation:

- AzerothCore module registration and minimal configuration;
- a core-independent Hunt state machine;
- a typed, read-only snapshot model for a future native Hunts interface;
- database-free unit and source-safety tests.

There is no playable Hunt yet. This milestone intentionally performs no database
I/O, spawning, rewards, purchasing, client messaging, or UI work.

## Architecture boundary

Playable native Hunts will require Content Manager, an ACTIVE/APPLIED package
whose key is `mod-native-hunts`, and the matching Portalkeeper-delivered client
patch. Future persistence will use new `native_hunt_*` tables. Physical
Huntmaster's Seal items will be the only currency representation.

This repository will not implement virtual balances, conversion or migration,
legacy store protocols, compatibility aliases, or a fallback economy. A realm
must load either this module or the standalone Hunt module, never both.

The initial vertical slice planned after the required Content Manager extension
uses Huntmaster Corvin in Stormwind, Gorehide in Westfall, and one validated
final site with a known-good fallback. It is not part of Milestone 1.

## Domain model

`HuntDomain` owns deterministic transitions:

```text
Idle -> Tracking -> FinalRevealed -> PreyActive -> ReadyToTurnIn -> Idle
```

An active Hunt can be abandoned to `Idle`. Restart recovery turns an interrupted
`PreyActive` state back into `FinalRevealed`, allowing the future crystal
encounter to be retried. The model contains no AzerothCore object types, SQL,
packets, client code, spawn GUIDs, or allocated native-content IDs.

`HuntSnapshot` projects the aggregate into a read-only future UI view. Final-site
text is absent during `Tracking`. Native Seal readiness and physical item count
are explicit; an unavailable state never creates another balance model.

## Configuration

Copy `conf/mod_native_hunts.conf.dist` through the normal AzerothCore module
configuration process. The settings are inert foundation values until gameplay
integration is implemented.

## Local checks

Run with Python 3 and a C++17 compiler:

```text
python tests/run_checks.py
```

The runner compiles and executes the pure domain/snapshot tests with strict
warnings, then checks production sources for prohibited legacy identities and
resource IDs. See `tests/README.md` for details.

The archive branch `archive/native-prototype-pre-clean-start-2026-09-25`
preserves the replaced prototype for implementation research.
