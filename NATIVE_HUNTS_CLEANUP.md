# Native Hunts cleanup

## Summary and audit

Audited the complete copied repository before changes: production sources,
headers, configuration, loader/project identity, SQL, documentation, tests and
test adapters, and the contents of the EPF archive. Normal Hunt gameplay was
separated from the obsolete virtual store, the one-time realm importer, and
the proven native Content Manager consumer.

The module is now **mod-native-hunts**, with native-only currency operations.
No virtual Seal economy is available when Content Manager is absent or invalid.
This pass adds no UI, quartermaster, expanded vendor catalog, client attestation,
per-login negotiation, new schema, allocation reassignment, or economic tuning.

## Architecture and removed functionality

- `HuntCurrencyService` exposes readiness instead of Legacy/Native/Paused modes.
  Startup validates the provider, resources, loaded cost/item templates and realm
  migration identity. Missing/invalid dependencies fail closed with a reason.
  A missing/non-stackable Seal template is rejected before migration dereference.
- Balances read physical item counts. Virtual balance reads, awards, spend and
  refund methods are removed. Runtime completion SQL no longer reads/increments
  the historical Seal column. The column's existing database default supplies
  zero for new records; existing values remain unchanged on duplicate updates.
- Removed HUNTS addon-message processing/session maps/catalogs, fake merchant
  packets, gossip spec/tier/slot purchase flow and manual virtual purchasing.
- The native vendor continues using core list/buy hooks, the resolved vendor
  relationship and ItemExtendedCost debit. Class/spec/level/equipment filters,
  single-item/single-count/slot checks and wrong-Huntmaster rejection remain.
  Tier-1 eligibility becomes three native vendor settings; unused tiers 2–4 go.
- Loader: `Addmod_native_huntsScripts`. New config: `mod_native_hunts.conf.dist`,
  `NativeHunts.*` keys. Gameplay defaults are preserved. Old loader/config files
  contain only retirement comments, making a changed-files ZIP safe to overlay
  without requiring deletions. Reconfigure CMake and rebuild after installing.
- Logs identify NativeHunts; in-game Hunts terminology and `.hunt` commands stay.
- The fresh Huntmaster SQL seed no longer deletes `npc_vendor` rows, and starts
  NPCs with gossip flag 1 rather than legacy gossip+vendor 129. Content Manager
  owns the native proof vendor flag/row. **Do not replay the prebuilt seed over
  ACTIVE owned templates**: its existing template/spawn replacement behavior is
  not an incremental upgrade. This cleanup requires no SQL execution on a
  deployed native realm.

## Proven functionality preserved

The EPF and authored manifest are byte-for-byte unchanged: symbolic Seal item,
server item definition, CurrencyTypes, logical Hunts category, five-Seal
ExtendedCost and the single vendor relationship. Numeric allocations remain
resolved resources; no deployment item/category/bit/cost ID was hard-coded.

Normal and Elite Hunt assignment/progression, tracking, ambushes, prey combat,
Huntmasters/guards, Return Rifts, equipment selection, XP/gold/Seal quantities,
Hunting Record and authoring commands remain. Runtime turn-in always uses the
existing native transaction path. XP/gold/equipment timing outside that
transaction is unchanged; this cleanup does not claim broader reward atomicity.

## One-time import and durability

`HuntCurrencyMigration.cpp/.h`, `HuntCharacterTransaction.h`, all character SQL,
and all world SQL except the Huntmaster seed are unchanged. The importer remains
separate from normal native runtime:

1. Validate native resources at startup before logins.
2. Atomically snapshot existing `hunt_stats.huntmaster_seals` into durable import
   work, retaining the original values for audit.
3. Persist each item/mail/mail-items delivery and delivered amount in the same
   transaction. Offline characters are included through durable mail.
4. Wait for async worker completion, check its bool result, verify the receipt,
   then publish in-memory mail state. Failed/uncertain operations never publish.
5. Atomically finalize and verify the realm's `NATIVE` receipt. Restarts resume
   pending work, and an existing completed receipt prevents another import.

`MIGRATING`/`NATIVE` database strings are durable migration states, not runtime
currency modes. Fresh installations with no legacy amounts perform an empty
snapshot/finalization and issue no items. No schema/state changes, automatic
restoration of virtual balances or downgrade behavior are introduced. The
existing bounded batch limit and startup-only requirement remain; large or
failed imports require another controlled restart after addressing the error.

An explicit standalone import command/workflow may be useful later. It is
intentionally not invented in this cleanup; existing startup activation/import
behavior is preserved. Never delete receipts to force migration to run again.

## Persistent identities and Content Manager follow-up

The repository/loader identity changes, but **the content package ownership key
remains `mod-hunts`**, isolated in `src/HuntContentIdentity.h`. The unchanged EPF
filename/manifest and tests use the same key. Changing it cosmetically would
create another allocation namespace, break the realm's recorded Seal identity,
and collide with already-owned server/vendor rows. There is no second package,
new alias, fallback resolution, or silent transfer in this change.

A future clean `mod-native-hunts` content key needs explicit administrative
Content Manager support for adopting/transferring the package's leases and
owned rows. It must preserve item.id, currency.known-bit, currency-category.id,
item-extended-cost.id, vendor ownership, references, baseline provenance and
build/activation history, with collision/drift checks and a reviewable rollback
plan. Existing physical item instances must retain their identity. This belongs
in Content Manager; consumer-side numeric remapping or database edits would be
unsafe. No external CM source was modified or new claim of CM testing made.

Until then, this is a compatible package namespace, not an independent package
that can coexist with the old native EPF. Install only one Hunts server module
and one copy of the content package. The sunset standalone module and this
native continuation share feature tables, script bindings and commands, so
simultaneous loading is unsupported even if their directory names differ.

Other intentional old references:

- `mod_hunts_huntmaster`, `mod_hunts_activation`, `mod_hunts_return_rift` are
  persistent creature/gameobject ScriptName bindings; code and seed SQL keep
  them so existing spawned content still runs its scripts.
- `hunt_*` table/column names represent persistent feature data. The virtual
  balance column and `legacy_amount` occur in schemas/import code and fixtures;
  they are not spendable in native runtime.
- `conf/mod_hunts.conf.dist` and `src/mod_hunts_loader.cpp` are inert overlay
  placeholders; `.gitignore` still ignores an abandoned old local config.
- Older phase documents/results and the archived original README retain their
  historical naming and architecture. Each is explicitly labeled archival.
  Earlier CM test fixtures retain the package versions/keys they test.
- The vendored V1 capability header is unchanged. Its general-purpose comment
  calls the ABI optional; this module requires a Ready provider operationally.
  C++ `virtual` declarations are unrelated to virtual Seal currency.

## Validation performed

- `git diff --check` passed; all diffs and remaining identity references reviewed.
- `tests/run_native_checks.py`: both database-free C++ suites passed, including
  native readiness failures, dynamic IDs/physical balances, worker futures,
  failed migration commits and receipt-before-publication ordering.
- EPF contract passed and authored/archived manifests matched. EPF, manifest,
  migration implementation, transaction helper and public ABI bytes unchanged.
- All eight production `.cpp` files passed C++17 `-fsyntax-only` using the available
  reference core's compile definitions/includes (commit
  `06234df3d5ab26c93f4f1f06f3edb828b73ecd3c`). This includes the new loader and inert
  old loader. No external build configuration/source was written.
- Updated currency-service SQL harness and existing realm-migration SQL harness
  passed syntax compilation with their MySQL adapter; **not executed** here.
- Function-body comparison found 53 shared HuntManager bodies unchanged after
  log-label normalization. The changed shared bodies were native eligibility,
  turn-in and stats. Guard, Elite prey, activation and Return Rift script blocks
  were unchanged. Additional deleted/renamed functions are the store cleanup.
- No database/worldserver was started, no database changed, no linked PTR build
  or live-client test claimed. No commit or push was made.

## Deployment and next decisions

Follow README.md for the new directory/loader/configuration names. Migrate
custom settings explicitly; old `Hunts.*` keys are ignored. Keep deployed
allocations, active content, physical items and import receipts intact. The
changed-files ZIP uses repository-relative paths and deliberately omits the
unchanged EPF and SQL schemas.

Next, rebuild in the target core and run the acceptance checklist in
`tests/NATIVE_TESTS.md`, including retained-native and isolated fresh/import
fixtures, normal/Elite gameplay, vendor rejection cases and missing-dependency
failure. Plan package adoption in Content Manager before renaming the content
key. Broader import administration, a new quartermaster/catalog and native
PvE/Hunts UI remain design decisions for later work.

## Files changed

- `.gitignore`
- `.project`
- `NATIVE_HUNTS_CLEANUP.md`
- `README.md`
- `conf/mod_hunts.conf.dist`
- `conf/mod_native_hunts.conf.dist`
- `content/README.md`
- `data/sql/db-world/prebuilt/903_huntmasters.sql`
- `docs/CHARACTER_TRANSACTION_FIX.md`
- `docs/NATIVE_CHANGED_FILES.txt`
- `docs/NATIVE_SEAL_REALM_CUTOVER.md`
- `docs/NATIVE_TEST_RESULTS.txt`
- `docs/PHASE5_CONSUMER_4_2_0.md`
- `docs/PHASE5_CONSUMER_TEST_RESULTS.txt`
- `docs/PRE_NATIVE_CLEANUP_HISTORY.md`
- `src/HuntContentIdentity.h`
- `src/HuntCreatureTemplateManager.h`
- `src/HuntCurrencyService.cpp`
- `src/HuntCurrencyService.h`
- `src/HuntManager.cpp`
- `src/HuntManager.h`
- `src/HuntScripts.cpp`
- `src/HuntsModule.cpp`
- `src/mod_hunts_loader.cpp`
- `src/mod_native_hunts_loader.cpp`
- `tests/NATIVE_TESTS.md`
- `tests/PHASE5_CONSUMER_TESTS.md`
- `tests/currency_adapter/DatabaseEnv.h`
- `tests/currency_service_mysql.cpp`
- `tests/native_currency_startup.cpp`
- `tests/run_native_checks.py`
