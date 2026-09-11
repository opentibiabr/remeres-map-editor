# Editing world layers

A world project opens an OTBM as the background for external objects. Object
positions, AID/UID attributes and teleport relationships live in readable JSON
layers. RME renders them on the native canvas and Canary resolves them during
startup. Saving a world project writes layers only; it never invokes OTBM saving.

## Open and edit

1. Install the normal client assets in RME and use an item catalog compatible with
   your server. Choose **File > Open World Project...** (Ctrl+Alt+O) or
   **Open World Project...** on the welcome screen, then select a `*.world.json`.
   The normal **File > Open** dialog also accepts world projects. The project
   points to its OTBM, server `items.xml`, and explicit layer list using relative paths.
2. The **World Layers** panel opens on the right and the map centers on the first
   object, unless a saved view position is available. Use **View > World Layers**
   (Ctrl+Alt+L) to show or hide the panel at any time, including before a project is
   open. Its **Open project...** button and introductory text guide the next step.
   Opening an OTBM alone does not load external layers.
3. Search by object ID in the panel, then select an object and choose **Go to object**.
   Double-clicking a list entry also centers the map on that object.
   Click or drag external objects directly on the map. The source selector that
   identifies a replaced OTBM item remains fixed at its original position.
4. Use the inspector to change position, AID, UID, destination object and arrival
   offset, then click **Apply changes**. AID may repeat; UID must be unique. Identity and
   origin are read-only in this first version.
5. Use **Go to arrival** to inspect the resolved destination, including destinations
   on another floor. The selected object's label shows its target and arrival floor;
   destinations on the current floor also have a connecting line and marker.
6. Ctrl+Z/Ctrl+Y undo and redo layer edits. Escape cancels an active drag. **Save layers**
   or Ctrl+S saves applied changes. The panel identifies the current project, reports
   unsaved layer changes and shows validation issues when present. Reopening the
   project reconstructs the same objects. The panel scrolls when space is limited.

Blue outlines identify external objects, yellow identifies the selection, and red
indicates validation errors. Hide external objects to inspect the unchanged OTBM
originals. Original items are suppressed only in a valid effective preview; they
are never removed from the editor's base-map data.

World project mode keeps base-map drawing, modification tools, live hosting and
base-map Lua scripts unavailable. Open the OTBM separately to edit terrain and
ordinary map items. The layer document has its own undo history and dirty state.

## Example: Black Knight

The Canary global datapack supplies `world/otservbr.world.json` and
`world/layers/black_knight.layer.json`. Open that project beside the normal
`otservbr.otbm`. It contains two externally owned replacements for item 1949:

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
opening the project; existing files are preserved. Semantically invalid drafts
remain editable, display diagnostics and can be saved. Canary rejects these drafts
before accepting connections. Its final validation also sees legacy startup changes
that RME cannot simulate, so the local server validation remains necessary.

## Saving and external edits

Only changed layers are serialized, with stable field order and indentation. The
project, OTBM and item catalog remain unchanged. Each layer is written to a temporary
file and atomically replaced; a multi-layer save is not a single filesystem transaction.
If a later layer fails, successfully saved layers are acknowledged and the others
remain dirty. A failed save does not silently close the editor.

Before saving, RME compares project and layer contents with the bytes read at open
or last save. An external modification blocks overwrite. **Save selected layer copy**
exports the current draft to a different `*.layer.json` for manual reconciliation.
Then close/reopen the project to read external edits. There is no watcher, automatic
merge or server hot reload in v1; restart Canary after applying the layer changes.

The inspector does not create/delete objects, rename identities, edit replacement
selectors or move OTBM objects. Edit the JSON explicitly for those authoring tasks
and reopen the project. Unknown future components are rejected rather than discarded.

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

The test checks identity-based arrivals, shared AIDs, duplicate UIDs, conflicts with
unconsumed originals, duplicate JSON properties, undo/redo, reopen, external write
conflicts and byte-for-byte preservation of an OTBM sentinel. A sentinel proves the
document's save routing; it does not replace visual testing of the actual editor.

For the integration check, record the real map hash, open Black Knight, move the
exit, undo/redo, save and reopen. Verify a single effective portal at each location,
cross-floor navigation, validation diagnostics and an unchanged OTBM hash. Restart
a local Canary with that project and check native creature/item travel and the
legacy fallback with `worldProject = ""`. Record these checks separately from
headless tests; passing document tests does not prove the native UI or gameplay.
