# Cyclopedia Export Performance Roadmap

This document records follow-up optimization candidates for the Cyclopedia map export after the initial static data compatibility and export performance work.

The current implementation intentionally keeps the exported data contract stable. Follow-up work must preserve the rules documented in [static-data.md](static-data.md), especially the sprite-based `SATELLITE` assets, aligned `MINIMAP`/`SATELLITE` layers, hash-named files, and deterministic `mapdata` references.

## Completed in This PR

- Removed the modal export progress dialog from the Cyclopedia map export path and kept progress reporting in the status bar, so the editor remains usable while the export runs.
- Replaced repeated full-map floor/chunk scans with a precomputed Cyclopedia floor plan.
- Reduced BMP encoding overhead by writing directly into a pre-sized BMP buffer.
- Reused rendered minimap chunks when building the matching satellite chunk for the same area.
- Added a bounded asynchronous asset encoding pipeline so BMP hashing and CIP LZMA compression can overlap with render work.
- Kept async workers isolated from `Map`, `wxImage`, protobuf objects, and UI state. Workers only receive owned BMP byte buffers, preserving thread-safety and deterministic asset insertion order.

## Deferred Optimization Candidates

### 1. Satellite Sprite Sampling Cache

Profile data after the current work shows `buildCyclopediaSatelliteChunk`, `getSampledSpriteForSpriteId`, `getTinySpriteForSpriteId`, sprite sheet lookup, and sprite image loading as the next major export costs.

Potential approach:

- cache resolved sprite sample metadata more aggressively inside the export run;
- avoid repeated `GameSprite::getDrawOffset()` and pattern/subtype resolution when the same item sprite is sampled many times;
- keep cache keys based on exported sprite identity from `GameSprite::getSpriteID(...)`, not GL texture or atlas ids.

Why it was deferred:

- this path is part of the visual compatibility contract for `SATELLITE` assets;
- caching the wrong identity or pattern can produce visually plausible but incorrect CipSoft-like output;
- the change needs focused visual validation against real exported assets.

### 2. Satellite Render Loop Restructure

The satellite renderer still composes sprite samples in the main export flow. The inner loop draws ground, border, and item sprites in tile draw order while also sampling neighboring tile footprints.

Potential approach:

- precompute per-tile draw item lists for a chunk;
- precompute source tile positions needed by the 2x2 sampling window;
- reduce repeated vector clearing, item filtering, and per-source position construction inside the hot loop.

Why it was deferred:

- a simple tile pointer grid was profiled and did not improve the total export cost enough to justify the added code and temporary memory;
- a larger loop rewrite has a higher regression risk because it touches draw order and footprint sampling;
- the current implementation is slower, but its behavior is easier to reason about and matches the documented contract.

### 3. Dedicated Export Snapshot for Parallel Rendering

The current async work avoids reading live editor structures from worker threads. Rendering still uses the live `Map` and sprite APIs on the main export path.

Potential approach:

- build a compact, immutable export snapshot for the needed floors/chunks;
- include tile minimap colors, ordered exported sprite ids, pattern data, and sprite sample metadata;
- render independent chunks from that snapshot on worker threads.

Why it was deferred:

- this is a larger architectural change, not a narrow optimization;
- it must define memory bounds for global maps to avoid trading CPU time for excessive RAM use;
- it must prove thread-safety around sprite image access or copy the required sprite data into the snapshot;
- it requires broader validation because it changes where export data is read from, even if the output format remains the same.

### 4. Compression Tuning and Asset Deduplication

Hashing and LZMA compression remain visible in profiles even after being moved to bounded worker tasks.

Potential approach:

- evaluate whether repeated empty or near-empty chunks can be detected earlier;
- deduplicate identical encoded assets only if `mapdata` references remain correct;
- benchmark compression parameters against client compatibility and file size.

Why it was deferred:

- the current CIP LZMA container path is compatible and deterministic;
- changing compression behavior may affect client loading assumptions or output size expectations;
- asset deduplication could complicate backup/restore behavior and `mapdata` reference validation.

### 5. Backup and Filesystem Cost

Profiles still show smaller costs in backup and file creation paths.

Potential approach:

- reduce redundant directory creation checks;
- batch filesystem existence checks where possible;
- avoid moving unchanged generated assets when content hashes already match.

Why it was deferred:

- this is no longer the primary bottleneck after the UI and encoding changes;
- backup/restore behavior is safety-critical for client assets;
- correctness and recoverability are more important than small wins in this area.

## Validation Expectations for Future Work

Future performance work should include:

- `git diff --check` and focused static inspection when no build is requested;
- profile comparison on a global map export before and after the change;
- visual validation of `SATELLITE` Surface View output, especially ground, border, item draw order, and multi-tile sprite footprints;
- validation that `MINIMAP` and `SATELLITE` assets are still emitted for all documented scales, including `1/16`;
- validation that generated asset filenames still embed hashes matching their content.

Avoid accepting an optimization based only on code intuition. The tile-grid experiment showed that a change can reduce one visible function cost while adding equivalent overhead elsewhere.
