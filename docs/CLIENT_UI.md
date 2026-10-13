# Native Hunts client UI

## Functional FrameForge integration

The editable design remains `assets/Native-Hunts.fforge.json` and the authoritative FrameForge
export remains `content/WoWUI/`. Do not hand-edit either as part of module integration. Package 21
ships `FrameForgeLayout.xml` byte-for-byte at
`Interface/FrameXML/NativeHuntsFrameForgeLayout.xml`. Its `FrameForge_Dungeon_Finder_UI` root stays
parented to `LFDParentFrame` at its authored 355 x 440 dimensions. The preserved host at
`content/client/Interface/FrameXML/NativeHuntsFrame.xml` still supplies the tabs, backdrop, and
hidden legacy presentation subtree.

The functional host, `NativeHuntsFrame.lua`, both PvE tabs, and the initializer/event frame remain
active. Hunts selection shows the host and `FrameForge_Dungeon_Finder_UI`; Dungeon Finder selection
hides both and restores `LFDQueueFrame`. Blizzard's `LFDParentFrame`, title, geometry, close button,
templates, and microbutton remain stock-owned. Initialization validates every exported control and
falls back to Dungeon Finder with a diagnostic if any required global is absent.

The PvE tab contract explicitly establishes `LFDParentFrame.numTabs = 2` before calling Blizzard's
`PanelTemplates_SetTab`. Optional tab-resize support is independent, matching the 3.3.5a API where
the selection helper requires a numeric `numTabs` field.

## Runtime values

The server continues to send the versioned `NHUNTS` snapshot over the existing same-player addon
message channel. FrameForge has zero runtime bindings; module Lua consumes its actual
`bindings` identities:

| Control | Snapshot source |
| --- | --- |
| `Standard_Hunts_Value` | `stats.standard` lifetime Standard completions |
| `Elite_Hunts_Value` | `stats.elite` lifetime Elite completions |
| `Elite_Today_Available` / `Elite_Today_Unavailable` | `stats.eliteUnlocked`, `stats.eliteAvailable` |
| `Hunt_Seal_Value` | `stats.sealState`, `stats.seals` physical inventory balance |
| `Tracker_Target_Name` | assignment `prey` |
| `Tracker_Huntmaster` | assignment `huntmaster` |
| `Tracker_Location` | assignment `city` |
| `Tracker_Hunt_Ground_Value` | assignment `zone`, or `finalLocation` after reveal |
| `Tracker_Hunt_Progress` | bounded assignment `progress` |
| `Complete2B_Huntmaster_Value` | assignment `huntmaster` |
| `Complete3B_Location_Value` | assignment `city` |

No values are invented. Missing optional strings use bounded `Unknown ...` fallbacks already used by
the functional client. Content or snapshot failure uses the Idle composition with an unavailable
message.

`LFDQueueFrameTitleText` is Blizzard-owned and is not changed by Native Hunts.

## Visual states

Lua carries the exact state memberships from `frameforge-manifest.json` and addresses region wrapper
frames using the exported `bindings.visibilityRuntimeId` identities. The authoritative server lifecycle
maps as follows:

| FrameForge state | Module condition |
| --- | --- |
| Idle | no active assignment, unavailable content/snapshot, or unknown state |
| Tracking | `HuntState::Tracking` / protocol `T` |
| Located | `HuntState::FinalRevealed` / protocol `F` |
| Fight | `HuntState::PreyActive` / protocol `P` |
| Turnin | `HuntState::ReadyToTurnIn` / protocol `R` |

Tier-specific Standard/Elite artwork is selected within active states. Daily availability and Seal
artwork are selected after the shared state membership is applied. The exported StatusBar is
initialized to `0..100` and value `0` before the first server response.

## Artwork and packaging

Run:

```bash
python3 content/build_epf.py
python3 tests/run_checks.py
```

The FrameForge export already contains eleven uncompressed 32-bit, top-origin, power-of-two TGAs in
`content/WoWUI/textures/`, including the padding and corresponding texture-coordinate adjustments.
Package 21 declares those generated files directly at the exact MPQ destinations from
`assets-manifest.json`; integration does not reconvert or resize them. Original PNGs and development
metadata are excluded from the EPF and final MPQ content set.

`clientFrameXml.loadEntries` establishes the exact order: stock `LFDFrame.xml`,
`NativeHuntsFrame.xml`, `NativeHuntsFrameForgeLayout.xml`, and `NativeHuntsInitializer.xml`.
The generic `frameForgeWowUi` ingestion path is not used.

## PTR deployment and verification

Local implementation stops before deployment. For an authorized PTR release:

1. Copy `content/mod-native-hunts.epf` to Content Manager's configured discovery directory.
2. Run `.content scan`, then `.content install mod-native-hunts`.
3. Optionally run `.content stage mod-native-hunts` and inspect the staged paths, FrameXML insertion,
   Lua, and all `Interface/FrameForge/Dungeon_Finder_UI/*.tga` files.
4. Run `.content build`; record the immutable build number and SHA-256. Do not activate unless the
   reviewed build contains package version 21 and the expected protected-FrameXML requirement.
5. When authorized, run `.content activate <build-number>` and publish that exact artifact.
6. Update the PTR Portalkeeper patch entry with the published URL and SHA-256 while retaining
   `Requirements=protected-framexml` and isolated runtime mode.
7. Exit every WoW process, install through Portalkeeper, and fully restart the client. `/reload` is
   insufficient for replaced protected FrameXML/MPQ textures.
8. Open Player vs. Environment through the microbutton, key binding, and LFG gossip. Verify Dungeon
   Finder remains the default stock tab and that repeated tab switches do not drift geometry.
9. Select Hunts and verify exactly one presentation, no Lua/XML errors, no green/missing textures,
   and correct Idle, Tracking, Located, Fight, and Turnin states.
10. Verify Standard/Elite tier selection, target, issuer, city, hunting ground, progress, lifetime
    statistics, daily Elite availability, and physical Seal balance.
11. Exercise relog, abandon, tracking progress, reveal, crystal use, prey kill, Return Rift, turn-in,
    `/reload`, and reconnect. Recheck stock queueing, roles, proposals, eye indicator, Escape handling,
    gossip opening, and both bottom tabs.

Static validation cannot prove in-game rendering, protected FrameXML loading, client texture decoding,
or live event timing; those remain PTR acceptance requirements.
