# Native Hunts content ownership

The module is mod-native-hunts. The unchanged 4.3.0 EPF's **persistent Content
Manager package key** is `mod-hunts`. This is deliberate compatibility, not an
optional legacy runtime mode. It preserves these logical resources:

- `seal / item.id`
- `seal-currency / currency.known-bit`
- `hunts / currency-category.id` (display name `Hunts`)
- `seal-cost-5 / item-extended-cost.id`
- `seal-proof-vendor` (the existing single vendor relationship)

The current deployment's numeric values are allocations, never constants to
copy into source. `manifest.native.json` and the manifest inside `mod-hunts.epf`
must match. This cleanup changes neither file nor resource definitions.

Do not install/select a second copy with a renamed package key. A safe future
rename needs an explicit Content Manager ownership/lease transfer, including
owned server rows, vendor relationships, history and existing item identities.
The sunset standalone module and native module are alternative realm installs,
not simultaneous providers. See ../NATIVE_HUNTS_CLEANUP.md.
