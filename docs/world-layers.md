# Editing server worlds in a normal map

World configuration belongs to the JSON files in the server's `world/` directory.
RME edits those files directly while displaying their objects in the ordinary
OTBM map tab. Canary reads the same files at startup; there is no export step or
change to the OTBM format.

## Open and discover

1. Open the OTBM with **File > Open**. RME loads its sibling
   `<map-name>.world.json` during map opening, before enabling editing.
2. For another catalog or an explicitly associated map copy, use
   **Map > Load Server Worlds...**. The association is remembered in
   **Preferences > Directories > World catalog**. A sibling catalog takes priority.
3. Use the normal **Worlds** palette beside NPCs and RAW, or select it with
   **View > Worlds Palette** (Ctrl+Alt+L). Search by identity, name, item ID, AID
   or UID. Selecting a result centers the map.
4. If the map has no catalog, the palette offers **Create world catalog...**.

A catalog lists active layers and behavior descriptors explicitly. Paths are
relative to the declaring file. Files merely present in a directory are not
activated. Use **Manage...** to include a layer or descriptor. Opening a catalog
through **File > Open** is a shortcut to its normal OTBM; there is no separate
World editing mode. An invalid catalog reports its diagnostics while ordinary
map editing remains available.

## Author configuration

The palette's creation menu supports configuring a selected base item, creating
an external item, replacing a selected original, and creating a reference point.
**Manage...** provides layer creation, renaming, activation and removal, object
renaming and removal, and moving declarations between layers. Deleting a
referenced object reports its dependents.

Select objects directly on the canvas and open their normal **Properties...**
dialog. The World pages expose the identity, destination file, original selector,
attributes, behaviors, relations and container content. Behavior fields come
from the catalog's JSON descriptors, including nested lists and records.
Attribute overrides are separate from inherited values: absence inherits;
`aid: 0` or `uid: 0` explicitly clears. AID may repeat. Every nonzero effective
UID must be unique, including inherited IDs and container contents.

External items and reference points move by changing JSON. Moving a configured
base item with the normal selection tool changes its OTBM position and selector
in one undo batch. Moving a replacement's original does not move the external
replacement. Occurrence selectors retain the selected original even when another
identical item shares its tile; invalidated preconditions require reassociation.

Container properties distinguish existing children from created content.
`fixture` owns a fixed instance. `refillOnStartup` is intended for collectible
content that is reconciled after persistence loads; it cannot reserve a UID.
RME previews initial configuration, while the server owns subsequent game state.

References use stable full object identities. Moving a declaration between files
or moving its position preserves those references. Explicit identity renaming
updates the affected references in the same action. **Go to arrival** navigates
to a teleport's resolved destination, including another floor.

**Manage... > Choose related object on map...** selects the relationship first
and its target with the next click on the ordinary canvas. Compatible objects
sharing a tile are listed individually. **Reassociate original on map...** lets
you choose the exact base item again after its selector becomes invalid. This
changes the binding without moving the base item or the external replacement.
Escape cancels either operation. A document revision change cancels a pending
choice instead of applying it to stale data.

Ctrl+Z/Ctrl+Y use the same chronological history for map and World changes.
Escape cancels an external drag. Preview visibility can be toggled to inspect the
base map; external replacements never remove originals from the OTBM data.

## Save, copy and resolve conflicts

Ctrl+S saves changes to their owning files. JSON-only changes skip OTBM
serialization and preserve its bytes. When both destinations changed, each
successful save is acknowledged independently; a failure keeps the remaining
work dirty. The OTBM and World publication are separate transactions.

**Save As** writes a new OTBM and auxiliary files, a sibling
`<new-name>.world.json`, and a `<new-name>.world-layers/` directory. It includes
unsaved World changes and rebases both structural and object history to the new
files. Undoing later cannot write into the original catalog. Existing destination
catalogs are never overwritten. Behaviors keep their configured relative links,
and changed descriptor/script revisions block the copy. Migration ownership is
copied, but a receipt for the original publication is not used to revert a copy.
If the map copy succeeds and its World copy fails, the tab retains its original
association and reports the written base copy.

RME watches the configured files and checks again after regaining focus and at
periodic intervals. Valid unrelated edits reload while preserving local work.
This includes descriptors and scripts included locally before the catalog has
been saved: reloading those files preserves the unpublished catalog and objects.
Overlapping edits offer base/local/disk comparison, a draft copy, or confirmed
reload limited to the conflict. Invalid JSON and missing files preserve the last
valid scene. Historical actions tied to superseded revisions cannot overwrite
the external work; independent history remains usable.

Invalid configuration cannot be saved to active files. **Manage... > Preserve
World draft...** writes a separate, inactive snapshot, including the loaded
versions of descriptors and Lua implementations even when their disk copies
have changed. Its filenames are not automatically discovered as active content.
See [file publication and recovery](world-file-publication.md) for interrupted
multi-file saves and the retained recovery data.

## Contract and compatibility

The shared model, validator and file service live in `source/world/`; schemas
live in `schemas/`. Version 2 supports map bindings, external creation,
replacement, anchors, typed attributes, relations and behavior descriptors.
Several compatible items may occupy a tile. Teleport arrival restrictions are
specific to teleport components, not a general restriction on doors or items.

Version 1 remains readable. Opening it never silently converts it; structural
v2 operations require explicit conversion. The original Black Knight example
uses `black_knight.entry` and `black_knight.exit`, with UIDs 38012 and 38013 and
arrival offsets `(0,-7,0)` and `(0,1,0)` respectively.

Canary's startup modes are `legacy`, `world` and `mixed`. Restart the server to
apply configuration changes. Lua reload only reloads compatible implementations;
it does not reload the world. RME live editing is unavailable for attached World
catalogs because that network protocol does not synchronize these documents.

## Validation

The maintained headless suite checks v1/v2 contracts, selectors, UIDs, authoring,
structural and native history, Save As, external edits, draft dependencies,
transaction conflicts/recovery and byte-for-byte OTBM preservation. When a local
build is authorized, reuse the configured build workflow. From a Visual Studio
developer terminal at the repository root:

```powershell
msbuild vcproj/Project/WorldLayersTests.vcxproj /t:RunWorldLayerTests /p:Configuration=Release /p:Platform=x64 /m:1
```

The optional `WORLD_LAYER_TESTS` CMake setting provides the same contract suite.
Linux and Windows CI exercise the maintained entries. Native tests do not replace
a visual walkthrough: create items, relations and container content; mix map and
World undo/redo; save and reopen; edit JSON externally; test conflicts and recovery;
and confirm the real OTBM hash after JSON-only changes. Run the same saved project
in Canary to verify behavior, persistence and compatibility separately.
