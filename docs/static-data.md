# Static Data / Static Map Data (Cyclopedia Houses)

This document explains how this RME fork exports Cyclopedia house data, how the protobuf payload is structured, and how clients consume/render it.

Scope:

- `staticdata-<sha256>.dat`
- `staticmapdata-<sha256>.dat`
- integration through `catalog-content.json`

Main code references:

- `source/protobuf/staticdata.proto`
- `source/protobuf/staticmapdata.proto`
- `source/iomap_otbm.cpp`
  - `mergeStaticDataTemplate(...)`
  - `buildStaticMapHouseTemplate(...)`
  - `buildStaticMapHousePreviewData(...)`
  - `buildStaticMapHouseTemplates(...)`

## 1. Overview

The house pipeline is split into two files:

1. `staticdata`: house metadata (id/name/city/rent/anchor position, etc.).
2. `staticmapdata`: visual house preview in tiles (origin, dimensions, items per tile, `skip`).

In the client, Cyclopedia combines:

- static data (`staticdata` + `staticmapdata`)
- dynamic house state from the server (auction/status/bids/etc.)

## 2. Files and Catalog

Files are published under `assets` and referenced by `catalog-content.json`.

Expected names:

- `staticdata-<sha256>.dat`
- `staticmapdata-<sha256>.dat`

Important rule:

- the hash in the filename must match the file content.

The exporter validates this before using template files. If there is a mismatch, the template is rejected to prevent invalid preview framing.

## 3. Protobuf Structure

## 3.1 staticdata

Contains per-house metadata (house id, name, city, beds, sqm, rent, flags, anchor position).

Primary uses:

- filling the Cyclopedia house list
- resolving static metadata by `houseId`
- serving as base for id remapping during template merge

## 3.2 staticmapdata

Legacy CIP-compatible preview format:

- `house_id`
- `data.origin (pos_x, pos_y, pos_z)`
- `data.dimensions (pos_x=width, pos_y=height, pos_z=floors)`
- `data.preview.layer.tile[]`
  - `item[].value` (item client ids)
  - `skip`

## 4. CIP Serialization Semantics

Tiles are serialized linearly inside a `width * height * floors` volume.

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

Simplified flow:

1. load `staticdata` and `staticmapdata` templates when they are available.
2. serialize current map house data.
3. merge generated `staticdata` with the template only when at least one current-map house matches it.
4. build house preview templates from template `staticmapdata` only for compatible template exports.
5. export each house preview while preserving template framing when available, otherwise use dynamic map framing.
6. write hash-named final files into `assets` and update `catalog-content.json`.

Compatibility details currently applied:

- no per-tile house/context mask is serialized in compatibility export.
- when a valid template is present, template framing (`origin/dimensions/skip`) is preserved.
- template item payload can be preserved to keep visual parity with CIP client.
- dynamic fallback is used for custom maps or houses without a matching template entry.

## 6. How the Client Receives and Renders

Logical flow (CIP/OTClient):

1. client loads `staticdata` and `staticmapdata` from assets.
2. server sends dynamic house list/state using house ids.
3. client matches dynamic `houseId` with static metadata/preview.
4. preview is decoded via `origin + dimensions + tile/skip/item`.
5. house tiles are rendered in the preview frame.
6. external context (`outside`/blur background) is rendered in a separate pass, aligned to the same frame.

Key point:

- outside blur/context depends on correct house framing.
- if `origin/dimensions` diverge from the expected CIP frame, context appears stretched, shifted, or out-of-scale.

## 7. Typical Visual Regression Cause

Symptom:

- house tiles look mostly correct, but outside blur/context has wrong scale/alignment.

Typical causes:

- exported `staticmapdata` has `origin/dimensions` different from the CIP template.
- template file was loaded from a hash-mismatched asset (filename hash does not match content).

Current exporter protections:

- validates embedded hash in filename
- rejects invalid templates
- tries a hash-valid sibling template when available
- falls back to current-map dynamic export when no compatible template is found
- writes filenames whose embedded hash matches the generated content

## 8. Validation Checklist

1. verify final file hashes:

```powershell
Get-FileHash .\assets\staticmapdata-<hash>.dat -Algorithm SHA256
Get-FileHash .\assets\staticdata-<hash>.dat -Algorithm SHA256
```

2. computed hash must match `<hash>` in each filename.

3. validate reference houses in client:

- `40503`
- `40211`
- `40510`
- `10301`
- `10302`

4. if visual mismatch appears, compare:

- `origin`
- `dimensions`
- serialized tile count
- `skip` progression semantics

## 9. Regression Prevention Rules

1. use hash-valid CIP templates for original CIP-compatible maps.
2. do not reintroduce per-tile house/context mask serialization in the compatibility path.
3. do not shift template framing during merge/remap.
4. keep linear decode semantics (`+ skip + 1`).
5. keep custom-map exports independent from unmatched CIP templates.
6. validate changes on real CIP client after exporter modifications.

## 10. Summary

Cyclopedia preview quality depends on:

1. correct static files (`staticdata` + `staticmapdata`).
2. CIP-preserving framing (`origin/dimensions/skip`) so house and outside context share the same projection space.

When export preserves this contract, house geometry and outside blur/context align with expected CIP behavior.
