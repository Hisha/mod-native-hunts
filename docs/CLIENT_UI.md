# Native Hunts client UI

## Architecture

`NativeHuntsFrame.xml` adds one bounded view after stock `LFDFrame.xml`. It does
not replace `LFDParentFrame`, `LFDQueueFrame`, `LFDFrame.lua`, or the stock TOC.
The Hunts view is a child of `LFDParentFrame`; the stock Dungeon Finder and
`TOGGLELFGPARENT` path remain intact. Identity, authoritative state, idle, and
record regions are all children of one internal content panel. The record is
anchored to that panel, not the outer PvE frame.

The stock build-12340 shell is fixed artwork: `LFDParentFrame` is 355 by 440,
while `LFDQueueFrame` owns a 512-square `UI-LFG-FRAME` texture whose visible
bounds are 356 by 440. Merely enlarging the parent therefore detaches its close
button and tabs from the unchanged visible shell. Hunts keeps the stock width,
uses a 355 by 500 parent, and composes its taller shell from fixed-size crops of
the same Blizzard texture. The paper center and repeated side segment add height
without scaling the stock border artwork.

Selecting Dungeon Finder or hiding the parent restores the captured stock
dimensions and reapplies the stock close-button and tab anchors. Reopening Hunts
reapplies the Hunts geometry from constants, so repeated switching cannot
accumulate offsets. Runtime Hunt panel textures retain their pre-resize authored
dimensions; only the space between fixed panels grows.

`NativeHuntsFrame.lua` is display-only. It requests a snapshot when the Hunts
tab opens and accepts versioned `NHUNTS` addon messages whispered by the server
to the same player. Records are bounded, decoded defensively, sequenced, and
assembled before rendering. The server supplies the state; Lua does not advance
Hunts or make gameplay decisions.

## Artwork

Original PNG artwork stays in `assets/`. Run:

```bash
python3 tools/prepare_ui_assets.py
```

The script alpha-trims and resamples usable sources, then writes deterministic,
uncompressed 32-bit TGA files on power-of-two canvases under
`content/client/Interface/NativeHunts/`. FrameXML uses explicit texture
coordinates so transparent canvas padding is never displayed.

`hunt_forest_trees.png` and `hunt_state_dust.png` are intentionally not
converted: inspection shows their checkerboard is baked into fully opaque RGB
pixels rather than represented by PNG alpha. Shipping them would display the
checkerboard in game. Replace those two source files with genuine-alpha PNGs
before adding them to the preparation map.

The standalone corner, header, and empty-progress-frame sources are also kept
but not packaged: the selected identity/state/record panel sources already
contain those ornaments. Layering the standalone copies would duplicate and
misalign the artwork.

## Content Manager and client lifecycle

The schema-3 manifest declares every XML, Lua, and TGA runtime file. Its
`clientFrameXml` contribution inserts only `NativeHuntsFrame.xml` after stock
`Interface/FrameXML/LFDFrame.xml`. Content Manager owns the composed stock TOC
and automatically records the `protected-framexml` client requirement.

No worldserver restart is required for this milestone's Lua/XML/TGA changes.
A restart is required only when deploying rebuilt Native Hunts server-module
code; none changed here. Install the updated EPF and use Content Manager's
normal build/activate lifecycle to create and publish a new client patch. The
player must fully restart the client after replacing that patch; `/reload` is
not sufficient for protected FrameXML or replaced MPQ textures. Clear the
client cache only when testing indicates stale packaged content.

## Manual PTR validation

1. Copy the rebuilt `mod-native-hunts.epf` into the configured Content Manager
   discovery directory. Run `.content scan`, `.content install mod-native-hunts`,
   and optionally `.content stage mod-native-hunts` for a development MPQ.
2. Run `.content build`, record the reported build number and SHA-256, inspect
   the staged artifact, then run `.content activate <build-number>`.
3. Publish that immutable MPQ. Update the PTR Portalkeeper `WowPatch` entry's
   `SourceURL` and `SHA256`; retain isolated runtime mode and
   `Requirements=protected-framexml`.
4. Exit every WoW process, let Portalkeeper install/update and verify the patch,
   then launch PTR through its prepared generation-2 runtime.
5. Open Player vs. Environment through the microbutton, key binding, and LFG
   gossip. Confirm Dungeon Finder remains the default stock view where expected.
6. Select Hunts and verify no Lua/XML errors, missing green textures, or tab
   overlap at normal and reduced UI scales.
7. Check idle, Standard Tracking, Elite Tracking, Trail Located, Final
   Confrontation, and Hunt Complete presentations.
8. Verify progress updates, final location, exact issuing Huntmaster/city,
   lifetime counts, daily Elite status, and physical Seal balance.
9. Exercise relog, `/reload`, abandon, prey kill, Return Rift, and turn-in.
   Confirm stale state is cleared and the server snapshot reconstructs the view.
10. Recheck Dungeon Finder queueing, roles, proposals, eye indicator, Escape
   handling, gossip opening, and both bottom tabs.
