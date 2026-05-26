# Cyclopedia Static and Map Data Export

This document explains how this RME fork exports Cyclopedia house data, map data, minimap assets, and satellite assets.

For follow-up performance work that intentionally stays outside the compatibility contract, see [cyclopedia-export-roadmap.md](cyclopedia-export-roadmap.md).

Scope:

- `staticdata-<sha256>.dat`
- `staticmapdata-<sha256>.dat`
- `map-<sha256>.dat`
- `minimap-<scale>-<chunk_x>-<chunk_y>-<floor>-<sha256>.bmp.lzma`
- `satellite-<scale>-<chunk_x>-<chunk_y>-<floor>-<sha256>.bmp.lzma`
- integration through `catalog-content.json`

Main code references:

- `source/protobuf/staticdata.proto`
- `source/protobuf/staticmapdata.proto`
- `source/protobuf/mapdata.proto`
- `source/iomap_otbm.cpp`
  - `IOMapOTBM::saveStaticData(...)`
  - `IOMapOTBM::saveCyclopediaMapData(...)`
  - `IOMapOTBM::serializeCyclopediaMapData(...)`
  - `mergeStaticDataTemplate(...)`
  - `buildStaticMapHouseTemplate(...)`
  - `buildStaticMapHousePreviewData(...)`
  - `buildStaticMapHouseTemplates(...)`

## 1. Overview

The Cyclopedia export is split into two user-facing flows:

1. Static house export writes `staticdata` and `staticmapdata`.
2. Cyclopedia map export writes `mapdata` and its referenced minimap/satellite assets.

In the client, Cyclopedia combines:

- static house data (`staticdata` + `staticmapdata`)
- map data and map assets (`mapdata` + `minimap`/`satellite`)
- dynamic state from the server, such as house auctions, status, and bids

## 2. Files and Catalog

Data files are written under the selected assets directory and referenced by `catalog-content.json`.

Catalog entries managed by the exporter:

- `type: "staticdata"` points to `staticdata-<sha256>.dat`.
- `type: "staticmapdata"` points to `staticmapdata-<sha256>.dat`.
- `type: "map"` points to `map-<sha256>.dat`.

Minimap and satellite files are not listed directly in `catalog-content.json`. They are referenced by `MapData.mapassets` inside `map-<sha256>.dat`.

Important rules:

- the hash in every hash-named data filename must match the file content.
- template files are rejected when their filename hash does not match their content.
- the default template lookup names are `staticdata.dat`, `staticmapdata.dat`, and `map.dat`, but final written files use hash-named filenames.
- existing replaced files are moved to a timestamped snapshot under `bkps` before the new file is written.
- backup snapshots are never overwritten; if two exports happen in the same second, a numeric suffix is added to the snapshot directory.
- when `mapdata` is replaced, the previous `mapdata` file and its referenced minimap/satellite assets are moved to the export snapshot.
- template `SUBAREA` assets are preserved in place because merged `mapdata` keeps referencing them.
- if a preserved `SUBAREA` asset is missing from the output assets directory, the exporter restores it from the newest matching backup snapshot or from the source client assets before writing the new `mapdata`.
- before updating `catalog-content.json`, every asset referenced by the final `mapdata` must exist in the output assets directory.

## 3. Protobuf Structure

## 3.1 staticdata

Contains per-house metadata:

- house id
- name
- city
- rent
- beds
- square meters
- flags
- anchor position

Primary uses:

- filling the Cyclopedia house list
- resolving static metadata by `houseId`
- serving as the base for id remapping during template merge

## 3.2 staticmapdata

Legacy CIP-compatible house preview format:

- `house_id`
- `data.origin (pos_x, pos_y, pos_z)`
- `data.dimensions (pos_x=width, pos_y=height, pos_z=floors)`
- `data.preview.layer.tile[]`
  - `item[].value` (item client ids)
  - `skip`

## 3.3 mapdata

Map data contains Cyclopedia map bounds and asset descriptors:

- `topleftedge`
- `bottomrightedge`
- `mapassets[]`
  - `type` (`SUBAREA`, `MINIMAP`, or `SATELLITE`)
  - `topleft`
  - `filename`
  - `widthsquare`
  - `heightsquare`
  - `scale`

The exporter scans floors `0..7` and emits minimap and satellite chunks for each floor that contains visible tile data. `SUBAREA` entries come from the template mapdata and are kept for client compatibility.

Current emitted scales:

- `1/64` with `1024` square chunks and `0.5` pixels per square.
- `1/32` with `512` square chunks and `1.0` pixel per square.
- `1/16` with `256` square chunks and `2.0` pixels per square.

The same scale set is emitted for both minimap and satellite assets.

## 4. CIP Serialization Semantics

`staticmapdata` tiles are serialized linearly inside a `width * height * floors` volume.

Decoding:

1. `linearIndex` starts at `0`.
2. each serialized entry represents the current cell.
3. next index is `linearIndex = linearIndex + skip + 1`.

Index-to-local-coordinate mapping:

- `floorArea = width * height`
- `floor = linearIndex / floorArea`
- `planeIndex = linearIndex % floorArea`
- `x = planeIndex / height`
- `y = planeIndex % height`

Absolute world coordinate:

- `worldX = origin.x + x`
- `worldY = origin.y + y`
- `worldZ = origin.z + floor`

## 5. Current RME Export Flow

Static house export:

1. load `staticdata` and `staticmapdata` templates when they are available.
2. serialize current map house data.
3. merge generated `staticdata` with the template only when at least one current-map house matches it.
4. build house preview templates from template `staticmapdata` only for compatible template exports.
5. export each house preview while preserving template framing when available, otherwise use dynamic map framing.
6. write hash-named final files and update `catalog-content.json`.

Cyclopedia map export:

1. load the existing `map` template when available.
2. scan map bounds across floors `0..7`.
3. render minimap chunks from tile minimap colors.
4. render satellite chunks for Surface View from the actual tile sprite stack, with minimap colors used only as fallback terrain.
5. write each chunk as BMP bytes inside the CIP LZMA asset container.
6. merge compatible template `mapdata` fields.
7. write hash-named `map-<sha256>.dat`, write referenced assets, and update `catalog-content.json`.

Compatibility details currently applied:

- no per-tile house/context mask is serialized in the compatibility export.
- when a valid house template is present, template framing (`origin/dimensions/skip`) is preserved.
- template item payload can be preserved to keep visual parity with the CIP client.
- dynamic fallback is used for custom maps or houses without a matching template entry.
- minimap sea/background pixels use the Cip minimap water color `(51, 102, 153)`.
- minimap downscaling uses weighted averaging to avoid checker/pixel aliasing.
- minimap upscaling stays nearest-neighbor to keep tile-aligned pixels.
- Surface View must be emitted as `SATELLITE` assets; minimap colors are only used as a fallback behind sprite pixels.
- Surface View draws the ground, border, and item sprites in tile draw order so the exported map stays close to CipSoft's surface view.
- Surface View sprites are composed on a transparent intermediate image; satellite sea color is only applied to pixels that remain empty on the ground layer.
- downscaling preserves alpha during sampling, then exported Surface View pixels that contain sprite data are resolved as opaque so the client does not tint them with the sea/background color.

## 6. How the Client Receives and Renders

Logical flow:

1. client loads `staticdata`, `staticmapdata`, and `map` from assets.
2. `mapdata` points the client to minimap and satellite chunk filenames.
3. server sends dynamic house list/state using house ids.
4. client matches dynamic `houseId` with static metadata/preview.
5. house preview is decoded via `origin + dimensions + tile/skip/item`.
6. map chunks are rendered from `MapData.mapassets` using `topleft`, `widthsquare`, `heightsquare`, and `scale`; `MINIMAP` feeds Map View and `SATELLITE` feeds Surface View.
7. external house context (`outside`/blur background) is rendered in a separate pass, aligned to the same preview frame.

Key point:

- outside blur/context depends on correct house framing.
- if `origin/dimensions` diverge from the expected CIP frame, context appears stretched, shifted, or out-of-scale.
- if minimap chunks are downscaled by nearest-neighbor sampling, large areas can look checker-patterned or overly pixelated.

## 7. Typical Visual Regression Causes

House preview symptom:

- house tiles look mostly correct, but outside blur/context has wrong scale/alignment.

Typical causes:

- exported `staticmapdata` has `origin/dimensions` different from the CIP template.
- template file was loaded from a hash-mismatched asset.

Map asset symptom:

- exported minimap appears checker-patterned or too pixelated.

Typical causes:

- downscaled minimap chunks used point sampling instead of averaging.
- the `1/16` layer was not emitted, forcing the client to magnify a lower-resolution layer.

Current exporter protections:

- validates embedded hash in data filenames.
- rejects invalid templates.
- tries a hash-valid sibling template when available.
- falls back to current-map dynamic export when no compatible template is found.
- skips out-of-bounds map coordinates before tile lookup so preview and map asset scans cannot alias negative coordinates into high map regions.
- ignores tiles whose minimap color is `0` so they do not overwrite the chunk background with black pixels.
- writes filenames whose embedded hash matches the generated content.
- emits `1/64`, `1/32`, and `1/16` minimap/satellite layers.
- stores backups in timestamped `bkps/export-*` snapshots instead of replacing an existing `.bkp` file.
- provides a `File > Export > Revert Client Assets Backup` menu action that restores a selected snapshot and first moves overwritten current files into a `bkps/restore-*` safety snapshot.
- uses `GameSprite::getSpriteID(...)` for exported sprite image lookup; `GameSprite::getHardwareID(...)` is a GL texture/atlas id and must not be serialized or used as an asset sprite id.
- preserves full-size sprite images for export sampling so multi-tile sprites can be sampled with draw offsets instead of being cropped to one tile.

## 8. Validation Checklist

1. verify final data file hashes.

```sh
sha256sum assets/staticmapdata-<hash>.dat
sha256sum assets/staticdata-<hash>.dat
sha256sum assets/map-<hash>.dat
```

2. computed hash must match `<hash>` in each data filename.

3. verify `catalog-content.json` references the current `staticdata`, `staticmapdata`, and `map` files.

4. inspect `map-<hash>.dat` and verify each `MapData.mapassets[].filename` exists under the assets directory.

5. validate reference houses in client:

- `40503`
- `40211`
- `40510`
- `10301`
- `10302`

6. if house preview mismatch appears, compare:

- `origin`
- `dimensions`
- serialized tile count
- `skip` progression semantics

7. if map image mismatch appears, compare:

- emitted scale list
- `topleft`
- `widthsquare`
- `heightsquare`
- generated chunk filename
- BMP dimensions after LZMA decode

## 9. Regression Prevention Rules

1. use hash-valid CIP templates for original CIP-compatible maps.
2. do not reintroduce per-tile house/context mask serialization in the compatibility path.
3. do not shift template framing during merge/remap.
4. keep linear decode semantics (`+ skip + 1`).
5. keep custom-map exports independent from unmatched CIP templates.
6. keep minimap and satellite `mapassets` metadata aligned with the generated chunk dimensions.
7. keep the `1/16` layer when changing map asset generation.
8. keep Surface View sprite-based: draw ground, borders, and items into `SATELLITE` assets in tile draw order, using minimap colors only as fallback terrain.
9. do not use GL texture/atlas ids as exported sprite ids; use `GameSprite::getSpriteID(...)`.
10. validate changes on a real CIP-compatible client after exporter modifications.

## 10. Summary

Cyclopedia quality depends on:

1. correct static files (`staticdata` + `staticmapdata`).
2. correct map files (`mapdata` + referenced minimap/satellite assets).
3. CIP-preserving house framing (`origin/dimensions/skip`) so house and outside context share the same projection space.
4. complete, correctly scaled map asset layers so the client does not over-magnify a lower-resolution chunk.

When export preserves this contract, house geometry, outside blur/context, minimap, and satellite chunks align with expected CIP behavior.
