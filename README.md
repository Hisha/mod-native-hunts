# mod-native-hunts

Native Hunts gameplay for AzerothCore WotLK / WoW 3.3.5a build 12340.
Requires **mod-content-manager with the V1 native vendor capability API**, applied
and ACTIVE Hunt content, and the matching client patch. Deploy client content
through the realm's existing Content Manager/Portalkeeper publication workflow.
There is no HuntUI/HuntsUI dependency and no virtual-currency fallback.

## Preserved gameplay

Normal and Elite Hunts, capital/hub Huntmasters and guard directions, tracking,
required ambushes, authored final encounters, map guidance, class/spec reward
filtering, group credit, Hunting Record, Return Rifts and GM authoring remain.
XP, gold, equipment, Elite limits and Seal reward amounts keep their existing
rules and defaults. Hunt completion delivers earned physical Seals by durable
mail; take the attachments to add them to the usable currency balance.

The existing one-item native MerchantFrame proof remains: the eligible Dalaran
Huntmaster sells the existing proof item for five physical Seals through core
ItemExtendedCost purchasing. The module validates eligibility; the core handles
the debit. No expanded catalog, quartermaster or PvE/Hunts client tab is added.

## Installation and upgrade

1. Install this repository as `modules/mod-native-hunts`, with the compatible
   Content Manager module. Do not install the sunset `mod-hunts` server module
   alongside it: both implement the same gameplay tables, scripts and commands.
2. For a fresh realm, apply world/character base SQL and world prebuilt SQL in
   numeric order before Content Manager owns native vendor content. For an
   existing native realm, keep its data and migration receipts. **This cleanup
   needs no schema migration or SQL replay.** The prebuilt files are replacement
   seeds; replaying them can reset templates/spawns and cause ownership drift.
3. Create `mod_native_hunts.conf` from `conf/mod_native_hunts.conf.dist` using the
   normal AzerothCore configuration installation process. Move customized
   `Hunts.*` values to `NativeHunts.*`. Old keys are no longer read. Map old
   `SealStore.Tier1.Cost/MinItemLevel/MaxItemLevel` to
   `NativeVendor.ExpectedSealCost/MinItemLevel/MaxItemLevel`; drop tiers 2–4.
   ExpectedSealCost is an eligibility guard, not a Content Manager price override.
   Preserve customized Enable, difficulty, reward and Return Rift settings.
4. Re-run CMake and rebuild so the new directory's `Addmod_native_huntsScripts`
   loader and new configuration file are discovered. The old loader/config dist
   are inert placeholders so a ZIP overlay cannot leave a second active loader.
5. Use the existing administrator-controlled Content Manager inspect, build,
   apply and activation/publication workflow. An EPF is content source, not a
   client MPQ. Merely installing/discovering it is insufficient. Existing ACTIVE
   native deployments keep their package and allocations; no new build is needed
   solely for this source cleanup. Restart after content is ready.
6. Check startup logs for `mod-native-hunts: native currency ready`. Missing or
   invalid dependencies disable currency operations, vendor access and turn-in;
   they never enable another economy. Correct the reported problem and restart.

## Persistent content identity

The shipped `content/mod-hunts.epf` and `manifest.native.json` remain **unchanged**.
Their historical `mod-hunts` package key owns deployed item, known-bit, currency
category, ExtendedCost and vendor relationships. `src/HuntContentIdentity.h`
is the explicit boundary between that ownership key and this module's new name.
Do not rename the manifest/package or duplicate it under a second key.
See [the cleanup report](NATIVE_HUNTS_CLEANUP.md) for the required future CM
ownership-transfer work and limitations. There are no hard-coded allocated IDs.

The existing `hunt_*` tables and database-bound `mod_hunts_huntmaster`,
`mod_hunts_activation` and `mod_hunts_return_rift` script identifiers stay intact.
They preserve deployed feature data and template bindings.

## One-time balance import

`HuntCurrencyMigration` remains a separate startup-only import component. After
native resources validate, it resumes or creates the durable realm snapshot,
delivers physical Seals for recorded balances (including offline characters),
and verifies the final `NATIVE` receipt. Existing completed realms do not import
again. Fresh realms with no balances create the empty receipt without issuing
items. `hunt_stats.huntmaster_seals` remains historical import data only.

Do not erase receipts, restore old balances as spendable currency, or run the
legacy module against a converted realm. An administrative downgrade is a
separate migration problem. This cleanup does not redesign that process.

## Configuration and commands

See the new dist file for unchanged gameplay defaults and the three native
vendor eligibility settings. Logs use `module.native_hunts` and NativeHunts
labels; player-facing Hunts terminology is unchanged.

Commands remain rooted at `.hunt`: `status`, `progress <amount>`, `ambush`,
`final`, `abandon`, and `set final point/list/needs/export/levels/goto/delete`.
Authoring requires GM access and `NativeHunts.Debug = 1`; argument formats and
permissions are unchanged.

## Validation and history

[Current tests](tests/NATIVE_TESTS.md) distinguish locally executed checks from
live acceptance still needed. [The cleanup report](NATIVE_HUNTS_CLEANUP.md)
lists files, retained compatibility boundaries, and follow-up work.
[The archived README](docs/PRE_NATIVE_CLEANUP_HISTORY.md) preserves earlier
combat/gameplay design history; its legacy installation and addon instructions
are not current. Earlier phase/deployment/test reports in `docs` are archival.
