# Editing server worlds in a normal map

Open and edit your OTBM normally. A server world catalog adds external objects to
that same map tab. Terrain and ordinary items remain editable with the existing
brushes, palettes, selection tools and Lua scripts. External object properties
are stored in JSON layers and resolved by Canary during startup.

## Load the server catalog

RME uses the existing `*.world.json` format as a catalog: it lists the OTBM,
server `items.xml`, and layer files in load order. All paths are relative to that
catalog. No Lua execution or recursive script discovery is required.

1. Open the OTBM with **File > Open** or the normal welcome screen.
2. RME automatically looks for `<map-name>.world.json` beside it. For a catalog
   elsewhere in the server folder, choose **Map > Load Server Worlds...** while
   the OTBM is open. RME verifies that the catalog refers to that exact map file.
3. The chosen catalog is remembered in **Preferences > Directories > World
   catalog**, beside the NPC and monster source paths. It loads automatically
   when its corresponding OTBM is opened. Unrelated maps keep their normal flow.
   A sibling catalog takes priority over this configured fallback.
4. Find objects in **Worlds**, a category of the existing palette beside NPCs and
   RAW. **View > Worlds Palette** (Ctrl+Alt+L) selects that category. Search by
   qualified object ID, name, item ID, AID or UID; selecting a result centers the map.

Opening a catalog through **File > Open** remains a shortcut to its normal OTBM.
There is no separate world project mode, extra map tab type or standalone inspector.
An invalid catalog reports an error without disabling ordinary OTBM editing.

## Edit on the map

In selection mode, click and drag an external object directly on the canvas.
Double-click it, or use its right-click **Properties...** command, to open the
normal item properties window. The Worlds palette also provides **Properties...**.

World item properties show the qualified identity, layer file and anchored
replacement selector. Edit the name, position, AID, UID, destination object and
arrival offset, then choose **OK**. AID may repeat; each nonzero UID must be unique
across world layers and the base map, including container contents. A blank
destination makes an inert portal. **Cancel** leaves the object unchanged.

**Go to arrival** in the palette navigates to the resolved destination, including
another floor. Blue outlines identify external objects, yellow identifies the
selection, and red indicates validation errors. **Show world objects** toggles
the preview so the base OTBM originals can be inspected. Originals are suppressed
only in a valid preview; the overlay never removes them from base-map data.

Choose another palette or switch to drawing mode to edit ordinary map content.
Ctrl+Z/Ctrl+Y use one chronological history for base-map and world-object actions.
Escape cancels a world drag. Map actions refresh the UID index for affected tiles;
bulk changes outside the action queue trigger a new census before validation.

## Example: Black Knight

The Canary global datapack supplies `world/otservbr.world.json` and
`world/layers/black_knight.layer.json`. Open `otservbr.otbm` normally; its sibling catalog loads automatically. It contains two externally owned replacements for item 1949:

| Identity | Original position | Destination | Arrival offset |
| --- | --- | --- | --- |
| `black_knight.entry` | 32874,31941,12 | `black_knight.exit` | 0,-7,0 |
| `black_knight.exit` | 32874,31955,11 | `black_knight.entry` | 0,1,0 |

The offsets preserve the existing quest arrival positions. Moving the exit also
moves the entry's resolved arrival, while the original exit remains suppressed at
its old location. The UIDs remain 38012 and 38013.

## Contract and validation

See `schemas/world-project-v1.schema.json` and `schemas/world-layer-v1.schema.json`.
The shared model and validator live in `source/world/world_layers.*` and
`source/world/world_validation.*`. Their corresponding implementation in Canary
must be updated together when the format changes; neither application executes
JSON as Lua. Fixtures are in `tests/world_layers/fixtures`.

Version 1 supports native teleport items, optionally replacing a single original
identified by position and item ID. References use `<layer.id>.<object.id>` and are
independent of AID and UID. Arrival is the target position plus an integer offset.
An absent teleport component means an inert native portal.

Validation detects duplicate identities/UIDs, ambiguous originals, duplicate
replacement claims, missing references, competing portals, blocked or missing
ground, houses, invalid coordinates and effective teleport cycles. UID checks
include base-map container contents. The project's server catalog must declare
external items as native teleports and the loaded RME catalog must recognize them.
One external object per tile is supported.

Malformed JSON, unknown fields/components and unsupported schema versions prevent
attaching the catalog; the ordinary map remains open and existing files are preserved. Semantically invalid drafts
remain editable, display diagnostics and can be saved. Canary rejects these drafts
before accepting connections. Its final validation also sees legacy startup changes
that RME cannot simulate, so the local server validation remains necessary.

## Saving and external edits

Ctrl+S saves each kind of change to its own file:

- If only external objects changed, RME writes only changed layer JSON files and
  skips OTBM serialization. The base map hash remains identical.
- If the base map changed, its normal OTBM save path runs. External objects remain
  in their layers and are never baked into the OTBM.
- If both changed, layers are saved first, then the base map. Each successful save
  is acknowledged independently; failures retain the remaining unsaved work.

Layer JSON keeps stable field order and indentation. Before saving, RME compares
catalog and layer contents with the bytes read at open or last save. An external
modification blocks overwrite. Each layer replacement is atomic; saving several
layers and the map is not one filesystem transaction. A failed save does not
silently close an editor with unsaved changes.

**Save As** writes a new OTBM with its own auxiliary map files, a sibling
`<new-name>.world.json`, and a `<new-name>.world-layers/` directory containing
copies of every layer, including unsaved world edits. The tab then uses those
copies. Source layers remain untouched and existing destination catalogs are
never overwritten. Relative item-catalog paths require the same filesystem.
If the map copy succeeds but the catalog copy fails, the tab keeps the original
association and reports that the base copy was written.

Save and close/reopen the OTBM to read external catalog edits. There is no file
watcher, catalog replacement inside an active undo history, automatic merge or
server hot reload. Restart Canary after applying layer changes.

Version 1 edits existing native teleports. Add/delete declarations, rename stable
identities and change replacement selectors in JSON, then reopen the map. Cut and
Delete on an external selection leave the base original intact and explain this
limit. Live editing is unavailable for maps with an attached catalog because its
network protocol does not synchronize these external objects.

## Tests

The optional `WORLD_LAYER_TESTS` CMake setting adds `world_layers_test` and the
`world_layers_contract` CTest entry to the maintained build. Use the existing
configured build directory and project build workflow when compilation is authorized.
The headless target depends on the shared JSON model, document and map validator,
without wxWidgets or OpenGL at runtime.

The Linux and Windows CI builds enable and run the contract suite. Windows covers
both CMake and the Visual Studio solution workflow.

The Visual Studio workflow has the same headless test in
`vcproj/Project/WorldLayersTests.vcxproj`, sharing the solution's manifest dependency
directory. From a Visual Studio developer terminal at the repository root, run:

```powershell
msbuild vcproj/Project/WorldLayersTests.vcxproj /t:RunWorldLayerTests /p:Configuration=Release /p:Platform=x64 /m:1
```

The test checks identity-based arrivals, shared AIDs, duplicate UIDs, conflicts
with unconsumed originals, duplicate JSON properties, native action snapshots,
undo/redo, reopen, external write conflicts, map association, independent Save As
catalogs and byte-for-byte preservation of OTBM sentinels. These tests exercise
the document contract; they do not replace visual testing of the actual editor.

For the integration check, record the real map hash, open its OTBM, load Black
Knight, move the exit, undo/redo, save and reopen. Verify a single effective portal,
native properties, palette search, cross-floor navigation, diagnostics and an
unchanged OTBM hash after a layer-only edit. Then mix a terrain edit with a world
edit, undo them in order, save both and reopen. Confirm that terrain persists only
in the OTBM and world edits persist only in JSON. Check Save As independence and
failed saves separately. Restart
a local Canary with that project and check native creature/item travel and the
legacy fallback with `worldProject = ""`. Record these checks separately from
headless tests; passing document tests does not prove the native UI or gameplay.
