# Native Hunts server-side parity notes

## Reward consistency

Turn-in creates provisional physical items and adjusts gold in memory, then
persists inventory/gold, native statistics, and the conditional assignment
delete in one character-database transaction. A deliberate primary-key
collision aborts stale/replayed revisions. On a confirmed rollback, provisional
items and gold are removed and the ready assignment remains. If the commit
result and assignment read are both unavailable, repeat claims are blocked;
the next turn-in/logout resolves by the durable assignment state.

XP is deliberately granted only after the durable transaction succeeds. This
prevents a database failure from leaving repeatable XP beside a retained Hunt.
A process crash in the narrow interval after durable completion but before
`GiveXP` can therefore lose the XP portion; AzerothCore exposes no practical
cross-transaction primitive joining character XP mutation with item/database
transactions. Physical Seals, equipment, gold, statistics, and assignment
deletion share the safer recoverable path. Standard Hunts award zero Seals.

## Managed spells

Content Manager allocates and deploys the Standard/Elite informational auras and
the dedicated `examine-cursed-relic` spell through paired client `Spell.dbc` and
server `spell_dbc` rows. The relic spell uses the generic
`stock-spell-clone-v1` profile with stock donor 61413, changing only its managed
ID and localized display name. Native Hunts resolves the ID symbolically and
validates the loaded SpellInfo before enabling gameplay; no allocated spell ID
is hard-coded.

## Elite content representation

Elite creature identities are managed symbolically through the EPF. Content
Manager's generic creature descriptor does not expose rank/health/armor/damage
overrides, so the established per-archetype multipliers and the global Elite
configuration are applied by the server encounter initializer. No allocated ID
is hard-coded.
