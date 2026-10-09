# Native Hunts client UI

## Functional FrameForge integration

The editable design remains `assets/Native-Hunts.fforge.json` and the authoritative FrameForge
export remains `content/WoWUI/`. Do not hand-edit either as part of module integration. Package 20
uses the exported `NativeHuntsFrame.xml` composition as the visual source, while the checked-in
runtime copy at `content/client/Interface/FrameXML/NativeHuntsFrame.xml` adds one integration-only
change: the legacy `$parentContentPanel` is hidden.

The functional host, `NativeHuntsFrame.lua`, both PvE tabs, and the initializer/event frame remain
active. `FrameForge_Dungeon_Finder_UI` is the only visible Native Hunts presentation. Blizzard's
`LFDParentFrame`, `LFDQueueFrame`, templates, close button, microbutton, and load order remain owned
by the stock client.

## Runtime values

The server continues to send the versioned `NHUNTS` snapshot over the existing same-player addon
message channel. FrameForge has zero runtime bindings; module Lua consumes its actual
`controlInventory` identities:

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

`LFDQueueFrameTitleText` is Blizzard-owned. The FrameForge stock override contract is implemented at
runtime by setting it to `Player vs. Environment`; Blizzard XML is not modified.

## Visual states

Lua carries the exact state memberships from `frameforge-manifest.json` and addresses region wrapper
frames using the actual `controlInventory.parentPath` identities. The authoritative server lifecycle
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
python3 tools/prepare_ui_assets.py
python3 content/build_epf.py
python3 tests/run_checks.py
```

The asset preparation tool retains the historical source textures needed by the preserved functional
XML and converts all eleven `content/WoWUI/assets/*.png` files to deterministic uncompressed 32-bit,
top-origin TGA files under `content/client/Interface/FrameForge/Dungeon_Finder_UI/`. Complete source
images are resampled to power-of-two canvases; FrameXML scales them into the unchanged authored
geometry.

Package 20 explicitly includes functional XML, Lua, and every required TGA. Its
`clientFrameXml.loadEntries` inserts only `Interface/FrameXML/NativeHuntsFrame.xml` immediately after
stock `Interface/FrameXML/LFDFrame.xml`. The broken package-19 static-layout declaration and empty
content array are not used.

## PTR deployment and verification

Local implementation stops before deployment. For an authorized PTR release:

1. Copy `content/mod-native-hunts.epf` to Content Manager's configured discovery directory.
2. Run `.content scan`, then `.content install mod-native-hunts`.
3. Optionally run `.content stage mod-native-hunts` and inspect the staged paths, FrameXML insertion,
   Lua, and all `Interface/FrameForge/Dungeon_Finder_UI/*.tga` files.
4. Run `.content build`; record the immutable build number and SHA-256. Do not activate unless the
   reviewed build contains package version 20 and the expected protected-FrameXML requirement.
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
