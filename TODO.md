# TODO - Materials Workbench

## Current State
- [x] `materials.db` is the editing source of truth
- [x] `palette_groups` drive top-level organization in Workbench and runtime
- [x] `Palettes` edit composition, while `Brushes` edit behavior and metadata
- [x] `Borders` and `Walls` already have dedicated workspaces with save/revert and targeted runtime sync
- [x] Stage 10D core palette flow is in place: add, move, remove, reorder, custom groups, preserved selection/scroll, better tree UX, overview manuals, and contextual tooltips

## Finalization Stages (target: no XML dependency)
- [x] Stage A: runtime DB-first hard
  - [x] Ensure runtime never loads materials from XML when `materials.db` is present and healthy
  - [x] Make XML fallback explicit (only when DB is missing/corrupt/incompatible), with clear user-facing messaging
  - [x] Fix startup hang when both `materials.db` and legacy XML files are missing (always close load dialog on failure)
- [ ] Stage B: bootstrap-once + migrations
  - [x] First run: create/upgrade `materials.db` schema then import legacy XML once
  - [x] Persist a “bootstrap completed” marker/version so XML is never re-imported silently
  - [x] Schema migrations must not require re-reading XML
- [ ] Stage C: parity audit (no data gaps)
  - [ ] Audit every runtime/editor field for XML-only behavior or defaults; move remaining gaps into DB load/save
  - [ ] Add validations for any newly surfaced invariants
  - [x] Preserve wall brush metadata flags when rebuilding runtime XML from SQLite
  - [x] Prevent ground border inlining crashes by deriving ground_equivalent when needed
  - [x] Fix inline border set creation to link owner brush by materials.db id
  - [x] Use discovered data directory when running from build dirs (avoid writing to a different materials.db)
  - [x] Fix inline creation: FindBrushDialog returns 0/1 (not wxID_OK); treat 0 as cancel
  - [x] Import doodad remove_optional_border from legacy XML so DB can round-trip it
  - [x] Add hard validations for border set invariants (global vs inline) in the SQLite repository layer
  - [x] Hide Border Workspace scope control (scope is set on create, immutable after)
  - [x] Import inline border set type/group from legacy XML when present
  - [x] Require inline border sets to have Center Tile > 0 (UI + SQLite layer)
  - [x] Validate border item edges against the supported edge set before saving
  - [x] SQLite inspector: show runtime XML for a selected brush to support parity audits
  - [x] Surface runtime-ready summary (schema/import marker/audit) in the SQLite inspector
  - [x] Tighten “DB ready” checks to block auto-mark when audit detects unresolved references
  - [x] Ensure SQLite bootstrap resolves tileset brush ids by name so unresolvedTilesetEntries stays at 0
  - [x] Make XML→SQLite tileset import explicit when skipping unsupported sections/entries (no silent data loss)
  - [x] Extend SQLite audit report to detect broken border-case references (match_border / replace_border ids+edges)
  - [x] Fix audit semantics: replace_border “with” is item id (don’t treat as border id); ignore edge checks for empty border sets
  - [x] Fix SQLite inspector schema version label (show db vs expected)
  - [x] Treat “import marker complete” as healthy only if audit shows 0 unresolved refs (avoid false-green DB)
- [ ] Stage D: recovery tooling
  - [x] Add explicit “rebuild/reset DB from XML” tooling (manual action), with strong warnings
  - [ ] Add integrity checks and a safe recovery path when DB is corrupted or partially missing
    - [x] Allow Reset-from-XML even when SQLite cannot open (corrupt materials.db recovery)
    - [x] Startup recovery dialog: offer Open Inspector / Reset from XML when SQLite falls back to XML with an existing DB
    - [x] Classify startup SQLite failures (notadb/corrupt/locked/read-only/schema mismatch) and show a recommended recovery path (+ SQLite rc/ext codes)
    - [x] After reset-from-XML, offer “Close now” to exit cleanly (restart is still manual)
    - [x] Ensure recovery actions appear before modal Warnings, and suppress Warnings when exiting after reset
    - [x] Show startup Warnings after creating a new map (not only when opening an existing map)
- [ ] Stage E: relationship navigation (beyond brushes)
  - [ ] Expand `Used By`/go-to surfaces across walls/parts/borders so users can jump to owners reliably
    - [x] Wall Workspace: add a `Used By` dialog (search + open brush/palette targets)
    - [x] Wall Workspace: add “Open Target” navigation for outgoing friend/redirect links
    - [x] Border Workspace: add “Open Owner Brush” for inline border sets
    - [x] Brush Workspace: add “Open Target” navigation for friend/enemy link rows
  - [ ] Add deeper cross-reference helpers and stable deep links where needed
- [ ] Stage F: links parity
  - [x] Expand `Links` to cover remaining legacy link types (e.g. `redirect`) with safe validation + clear UX
- [ ] Stage G: polish + hardening
  - [ ] Finish product-language/workflow refinements (empty states, confirmations, consistent labels)
  - [ ] Final edge-case pass for import/export, validations, and crash/freeze hardening

## Next
- [x] Border Workspace visual pass: align `Preview Matrix` to the same geometry as `Slot Grid`
- [x] Border Workspace visual pass: show a real visual `Center Ground` using `groundEquivalent`
- [x] Border Workspace visual pass: upgrade the preview from loose icons to a stronger composed border preview

## Visual Polish
- [x] Add visual metadata previews for `lookId` and `serverLookId` in the Brush Workspace
- [x] Add richer visual previews inside `variations`, including per-item thumbnails and domain-specific preview feedback
- [x] Border Workspace: make `Specific Cases` (ground border `<specific>`) editor more visual (tables + inline sprite icons + picker + selection previews)
- [ ] Brush Workspace `Carpet` future polish: only revisit with higher-level authoring affordances or richer composed previews if real usage still shows the new map-first flow feels too list-like
- [x] Continue the `Aligned` visual editor pass with mini-scene previews and a stronger semantic layout for `table`
- [x] Continue the `Doodad` editor rebuild from the new scene-preview base: move from read-first preview to direct grid authoring on top of the existing composite fields
- [x] Improve Border Workspace presentation with stronger contrast, clearer slot-to-preview correspondence, a compact authoring panel, inline/global-specific fields, scope-aware metadata naming (including `Global Border ID` / `Autoborder Group`), a centered compact slot grid, a richer searchable `Used By` context panel with a real `wxGrid` table for global borders, and first-pass CRUD for border sets / global contexts
- [x] Improve Wall Workspace presentation with richer visual previews for wall parts, doors, and composed wall results
- [x] Wall Workspace authoring: add fast `Add Part Type` affordance (e.g. add missing `pole` / `corner` / ends / diagonals) and keep previews faithful when a part is missing instead of silently substituting
- [x] Wall Workspace preview: add `Strict / Fill` mode toggle, overlay diagnostics, and explicit door-side controls
- [x] Wall Workspace density pass: tighten preview controls and reduce unused vertical space in grids
- [x] Workbench wxWidgets cleanup: fix `wxStaticBoxSizer` parenting warnings by creating static-box children under `GetStaticBox()`
- [x] Continue reducing spacing and padding across workspaces where the layout still feels too loose
- [ ] Continue small product-language and workflow refinements across Workbench texts and actions
  - [x] Palette Workspace: move palette category actions into the footer action bar
  - [x] Palette Workspace: tighten entry action buttons, show Source Catalog entries for all kinds (including Raw), and add catalog filters + double-click-to-add
  - [x] Brush Workspace (Carpet): layout map supports diagonal slots (`dnw/dne/dsw/dse`) with corrected labels, centered slot sprites, cleaner slot chrome (badge placement, no redundant missing text, hidden Context Details), and seamless preview rules for diagonals + sparse sets (diag+only E/W or diag+only N/S collapse without showing center, and missing opposite-edge pairs no longer leave a gap)
  - [x] Brush Workspace (Aligned): show a compact `Details` toggle (tooltip explains why) when a brush contains legacy/duplicate aligned slots; keep it hidden otherwise
  - [x] Brush Workspace (Table): hide outdated helper copy under `Table States` and keep `Add Node`/`Remove`/`Details` as a compact single action row
- [x] Standardize technical labels such as `lookId`, `serverLookId`, `zOrder`, and `partType`

## Future UX
- [x] Add search and filtering in the navigation tree: the sidebar now exposes a catalog filter above the tree, keeps matching parent groups visible, auto-expands filtered branches, preserves and restores pre-filter navigation state when clearing the query, and shows an explicit empty-result row when nothing matches
- [ ] Add safer creation and removal flows for core entities directly inside the Workbench
  - [x] Brush Workspace: add `New Brush` and `Delete Brush` actions in the footer, including immediate navigation refresh on create/delete
  - [x] Brush Workspace: make `Type` a dropdown and warn before destructive type changes clear variation payload
- [x] Add a Workbench Inspector dialog (opened from a button inside the Materials Workbench) with tabbed surfaces
  - [x] `Health` / `Warnings`: master/detail (search, severity/domain/issue filters, rescan, go-to), covering `Borders`, `Walls`, `Brushes`, and `Palettes`
    - [x] First-pass domain warnings for `carpet` (missing/empty/unknown slots + invalid item ids)
    - [x] First-pass domain warnings for `table` (missing/empty/unknown slots + invalid item ids)
    - [x] First-pass brush warnings for invalid `lookId` / `serverLookId`, invalid item ids, unresolved links, and missing ground border refs
    - [x] First-pass `doodad` warnings for empty alternatives/composites/tiles and invalid item ids
    - [x] First-pass `wall` warnings for empty parts, empty door type, and invalid item ids
    - [x] First-pass `border set` warnings for empty sets, invalid item ids, invalid `groundEquivalent`, and unused global sets
    - [x] First-pass `palette` warnings are per-entry (missing brush) and per-field (invalid itemId/from/to/after), not aggregated into a single count row
  - [x] `SQLite`: fold the legacy SQLite Materials Inspector into this surface
- [ ] Add `Used By` reference views (not in the Inspector) so users can jump to owners without hunting in the tree
  - [x] Brush Workspace: add a `Used By` dialog listing palette references, inbound brush links, and border-target references, with search + open
- [x] Brush Workspace: add a `Links` tab to edit and reorder `friend` / `enemy` brush links and persist them to `materials.db`
- [x] Brush Workspace: show inbound brush links (read-only) alongside outgoing links, so targets are discoverable without opening `Used By`
- [x] Brush Workspace: fix Links double-click crash (avoid lifetime issues in event handlers under unity build)
- [ ] Brush Workspace: expand `Links` to support other legacy link types when needed (e.g. `redirect`) with safe validation and clear UX
- [ ] Add richer cross-reference views so users can navigate relationships beyond brushes (e.g. wall/part ownership and more global border usage surfaces)
- [ ] Add import/export tooling for any Materials Workbench entity (at least `walls`, `borders`, `brushes`, `palettes`, and `palette categories`) so users can share items without swapping the full `.db`
  - [x] Workbench: add global Export/Import UI that produces a single `.rme-materials.json` file from a user-selected list of entities
  - [x] Export/Import: support `border sets` (global), `brushes` (including inline border sets + ground borders + specific cases), `palettes`, and `palette groups` with update-on-conflict by stable keys
  - [x] Export: dependency closure now includes linked/target brushes and palette references (and exports palette groups before palettes)
  - [x] Import: palette groups now update-on-conflict by name instead of failing with “already exists”
  - [x] Import UX: show progress bars for large JSON (read/parse/preview/apply) and refresh runtime brushes+palettes after import
  - [x] Import runtime refresh: avoid crash by reloading brushes incrementally (no global clear while palettes are alive)
  - [x] Import performance: refresh only affected brushes/borders, avoiding minutes-long “Finishing” freeze
  - [x] Catalog header: remove redundant title/subtitle and match Import/Export/Inspector button height to toolbars
  - [x] Import atomicity/perf: single transaction + avoid controller reload per palette/palette group (bulk save, reload once)
  - [x] Import conflicts: add “on conflict” strategy (update/skip) to preview + apply without partial updates
  - [x] Import conflicts: support rename-with-suffix for palette categories and palettes (preview + apply)
  - [x] Navigation: show empty palette categories in the catalog tree (count 0) so imported empty categories are visible
  - [x] Export UX: show detailed dependency breakdown in the export preview (what gets added by deps)
  - [x] Export/Import: preserve wall/brush metadata `thicknessCeiling` in JSON
  - [x] Export UI: replace four giant columns with tabbed pickers + search and “select/clear shown” actions per tab
  - [x] Import UX: preview plan uses tabbed lists + filter with Kind/Key/Action/Detail columns; invalid rows block Import
  - [x] Import UX: per-item warnings for unresolved refs (links, palette brush names) + hard invalid for missing global borders not present in file/DB
- [x] Safer removal (Palette Workspace): `Delete Palette` now shows category + section/entry counts and a small entry preview before confirming
- [x] Safer removal (Border Workspace): `Delete Border` now shows a `Used By` preview + context count for global borders, and an owner + slots summary for inline borders, before confirming
- [x] Safer removal (Brush Workspace / Doodad): removing an alternative/composite/tile now asks for confirmation and shows what will be removed (counts and key details) before applying the local change
- [x] Palette Workspace category delete safety pass: deleting a custom category that still owns palettes now requires an explicit destination category, moves the affected palettes in one transaction, snapshots the selected category name before mutation, and only then removes the old category so no tileset falls back to an implicit uncategorized state or hits a use-after-reload crash while finalizing the UI
- [ ] Add richer entity navigation helpers (go-to targets, deep links, and cross-references) as the Inspector surfaces mature
- [ ] Revisit `Doodad` polish with contextual scene actions, stronger active-tile/layer feedback, and richer visual affordances after the current editor flow settles
- [ ] Add `Doodad` reordering helpers for `alternatives`, `composites`, tiles, and tile layers if later authoring passes show they would speed up editing
- [ ] Add richer `Doodad` list visuals such as tiny sprite previews or layer thumbnails only if they improve scanability without bringing back layout bloat
- [ ] Add optional `Doodad` keyboard shortcuts and higher-level context menus as a future polish pass, not as a requirement for the current editor milestone

## Nice To Have
- [x] Add a real composed preview for `wall brush`, beyond the current parts grid
- [ ] Add manual runtime sync controls only if a future workflow really needs them
- [ ] Revisit XML deprecation messaging once the visual Workbench flow feels complete and clearly superior

## Legacy Parity Gaps
- [x] Tilesets: support legacy `<raw>` sections in the DB-first import + editor flow (so raw-only tilesets are not silently ignored)
- [x] Ground borders: add an editor for legacy `<specific>` cases (conditions/actions) stored under ground border contexts
- [x] Brushes: expose explicit “clear” actions that map cleanly to legacy `clear_borders` / `clear_friends` intent (instead of only resulting empty state)
- [x] Borders: expose `super` (super border) in global usage contexts and persist it (`ground_brush_borders.super_border`)
- [x] Wall Workspace: add editing UI for wall `friend` + `redirect` links (and show inbound refs), matching legacy `<friend name="..." redirect="...">`
- [x] Wall Workspace: expose wall brush metadata flags that exist in legacy XML (`lookid`/`server_lookid`, `draggable`, `on_blocking`, `on_duplicate`, `redo_borders`/`reborder`, `one_size`, `thickness`)
- [x] Doodad: support legacy `remove_optional_border` behavior in DB-first (storage + editor UX), keeping runtime behavior consistent with the XML loader
- [x] Wall Workspace: fix crash when selecting a link row (defer refresh + guard against re-entrant list refresh)
- [x] Wall Workspace: fix link list freeze by preventing selection-change refresh loops (suppress selection events during rebuild; selection only updates action buttons)

## Delivered Milestones
- [x] Stage 8: `variations` support landed inside the Brush Workspace
- [x] Stage 9: dirty state, validations, save/revert flow, loss prevention, and origin metadata review are complete
- [x] Stage 10A-10C: palette navigation and editing were rebuilt around a real DB-first palette workflow
- [x] Stage 10D core UX: palette composition is now professional, fast, and inline, without exposing backend-only `section` concepts in the main UI
- [x] Stage 10E: palette saves refresh runtime palettes and navigation state together so Workbench and runtime stay aligned
- [x] Stage 10F base: first import bootstraps the base palette groups from legacy XML into `materials.db`
- [x] Stage 10G: palette grouping is DB-first and shared by Workbench and runtime, including custom groups

## Delivered Highlights
- [x] Startup SQLite bootstrap bugfix: when `materials.db` is newly recreated but still missing imported data for the current schema, opening an already populated map now skips the premature runtime SQLite load, falls back to XML without false warnings, and schedules the background rebuild normally
- [x] Brush Workspace: metadata, `variations`, dirty state, save/revert, runtime-owner hints, and validation for `lookId`, `serverLookId`, and variation item ids
- [x] Brush Workspace `Ground` visual variation pass: weighted ground items now use visual cards with sprite previews, percent/badge feedback, contextual add/edit dialogs, and a seamless scrolled preview grid with hover tooltips, optional rarity highlighting, and click-to-select sync from preview back to the current variant
- [x] Brush Workspace `Aligned` stage 1: `carpet` and `tiny border` authoring now starts from a visual context map with connected context previews and missing-slot feedback, while `table` now uses semantic state cards with visually centered piece previews, external connectivity guides, compact labels/tooltips, and plainer product language; both domains keep sprite-backed item cards, right-click quick edit on context items, and an `Advanced` block that preserves direct align/item/chance control while the UX shifts away from raw lists
- [x] Brush Workspace `Aligned` stage 2: `carpet` and `table` now use independent variation pages/layouts, so table seamless preview widgets no longer leak into carpet and each domain can evolve its own UX safely
- [x] Brush Workspace `Carpet` stage 1: the carpet page now presents itself as a dedicated `Carpet Editor` with a real `Carpet Layout Map`, map-slot-driven context creation/selection, carpet-specific `context`/`variant` copy across actions and validation, and empty-slot selection that prepares `Add Context` directly from the visual map instead of falling back to generic aligned-editor wording
- [x] Brush Workspace `Carpet` stage 2: empty carpet slots now expose a stronger visual `+` affordance directly in the layout map, configured contexts surface lightweight variant counts, `Add Variant` now opens a preview dialog before insertion, and carpet variant cards now expose inline `edit` / `remove` actions instead of relying on generic right-click-only editing
- [x] Brush Workspace `Carpet` stage 3: the right side now includes a dedicated seamless preview that renders the carpet as one continuous 3x3 composition, uses the active selected variant in preview when available, keeps empty slots visible as future placements when needed, and turns the carpet flow into a real spatial authoring pass instead of a plain list of variants
- [x] Brush Workspace `Carpet` seamless preview pass: the old boxed carpet preview now draws a tighter runtime-style continuous tile composition, centered by visible sprite bounds like the table preview, so carpet authoring reads as a real seamless surface instead of nine isolated cards
- [x] Brush Workspace `Carpet` seamless collapse pass: when the layout is fully covered except for the opposite-edge pair `N+S` or `W+E`, the seamless preview now collapses the matching central stripe and joins the remaining carpet pieces instead of preserving a visually misleading empty gap
- [x] Brush Workspace `Carpet` seamless cleanup pass: the carpet preview now hides editorial overlays such as grid lines, slot letters, and selection highlights, leaving only the composed surface itself in the preview area
- [x] Brush Workspace `Table` seamless preview pass: vertical and horizontal preview strips now position sprites by their visible bounds with tighter continuous spacing, so the pieces read as seamless sequences instead of isolated centered cards
- [x] Brush Workspace `Table` runtime-truthful preview pass: the seamless preview now lays out only the nodes that actually exist using real tile anchors and adaptive connection rails, so 1/2/3-piece states mirror the runtime map logic instead of a synthetic bounding-box heuristic
- [x] Brush Workspace `Table` visual authoring pass: the inline `Advanced` block is now hidden from the main layout, empty nodes expose a direct `+` affordance, table item add/edit flows use a compact preview dialog, and the right-side item list copy now reinforces the selected-node workflow without raw inline form clutter
- [x] Brush Workspace `Table` item-card polish pass: context-item sprites now center by visible bounds instead of raw sprite rects, each table item card now owns inline `edit` and `-` affordances under the rarity badge, and the table copy now matches the visual-first node/item workflow instead of the older advanced-form flow
- [x] Brush Workspace `Table` naming pass: the variation tab now becomes `Table Editor` for table brushes, section titles were tightened to `Table States` and `State Items`, and the remaining helper copy now reflects the visual-first state/item workflow instead of the old generic variations wording
- [x] Brush Workspace editor-tab sync pass: the domain-specific tab title now refreshes immediately when switching between `table`, `carpet`, `doodad`, and `ground`, instead of waiting for an extra UI action to pick up the new brush type
- [x] Brush Workspace `Doodad` stage 1: the existing alternative/single/composite/tile forms now drive a visual `Scene Preview` with real 32x32 cells, floor filtering, centered single-item preview, and selection-aware composite rendering without breaking legacy XML-compatible authoring
- [x] Brush Workspace `Doodad` stage 2: the `Scene Editor` now keeps tile selection in backend state and drives visual tile authoring directly, with left-click add/select behavior, right-click item/tile removal, hidden tile offset controls, and Ctrl+click item stamping using the current `Item ID`
- [x] Brush Workspace `Doodad` stage 3: live runtime sync now mirrors valid in-memory composite edits into the active runtime brush and palette selection immediately, so edited doodads can be painted on the map before `Save`
- [x] Brush Workspace `Doodad` runtime reload correctness: `DoodadBrush::load()` now resets prior alternatives and runtime-owned item bindings before rehydrating, so live/save reloads no longer depend on `F5`
- [x] Brush Workspace `Doodad` save commit hardening: the final doodad save pass now commits the active alternative/composite/tile/tile-item using the editor's internal selection indices instead of querying listbox selection state during the `Save` click
- [x] Brush Workspace `Doodad` dialog edit sync: right-click edits for `single item`, `composite chance`, and `tile layer` now promote the edited entry to the active backend selection and refresh the hidden authoring controls, so `Save Brush` no longer replays stale values after a dialog-based edit
- [x] Brush Workspace `Doodad` layout pass: structure lists are now compacted into a single narrow column and the `Scene Editor` owns the main authoring area with substantially more width and height for larger composites and `All Floors`
- [x] Brush Workspace `Doodad` left-column compact pass: `Single Items`, `Composites`, and `Tile Layers` now edit through compact dialogs, with hidden inline field blocks and a denser single-column layout that reduces scrolling
- [x] Brush Workspace `Doodad` contextual edit pass: left-column lists now use left-click for selection and right-click for editing, while canceling a dialog preserves the current preview mode instead of flipping between `single` and `composite`
- [x] Brush Workspace `Doodad` floor preview pass: `All Floors` now aligns stacked floors to a shared XY origin, and per-floor filters no longer confuse the real `Floor -1` with the `All Floors` mode
- [x] Brush Workspace `Doodad` alternative navigation pass: the old `Alternatives` list is replaced by a visual slider in the `Scene Editor` header, with previous/next arrows, clickable square indicators, and inline `+` / `-` controls for fast alternative authoring
- [x] Brush Workspace `Doodad` alternative slider empty-state fix: when removing the last alternative, the slider now shows a clean `No alternatives` message without overlapping the controls
- [x] Brush Workspace `Doodad` floor navigation pass: the old floor dropdown is replaced by a vertical visual floor slider beside the `Scene Editor`, with separate add/remove floor controls plus up/down navigation so floor authoring stays compact and spatial while new tiles inherit the active floor
- [x] Palette Workspace: inline add/move/remove/reorder flow, support for `brush` and DB-backed `item` entries, custom destination groups, preserved selection/scroll, and better destination feedback
- [x] Palette Workspace performance: visible-range painting, local selection invalidation, cached preview bitmaps, and lighter runtime palette reload ordering from DB
- [x] Border Workspace: dirty state, save/revert, runtime-safe validation, canonical 5x5 slot geometry, selected-slot preservation, targeted runtime refresh, a runtime-style composed preview matrix with visual `Center Ground`, consistent `groundEquivalent` import, and runtime-truthful global border center preview via linked brush usage contexts
- [x] Wall Workspace: dirty state, save/revert, guarded selection changes, selected part/item/door preservation, and door compatibility validation
- [x] Navigation and UX: slimmer sidebar, click-on-label expand/collapse, contextual overview manuals, clearer `Palette Categories` terminology, and rich tooltips in tree and palette grids
- [x] Workspace density pass: the bottom action/status strip is now noticeably slimmer across `Brush`, `Border`, `Wall`, and `Palette` workspaces, including a single-row footer for status plus save/revert in the main editors so feedback consumes much less vertical space
- [x] Runtime alignment: Workbench and runtime now share DB-first palette groups, including custom groups, with targeted sync paths instead of unsafe full reloads

## Guardrails
- [x] Keep `section` as backend/runtime compatibility storage, not as a first-class UX concept
- [x] Keep palette saves on targeted sync paths; do not reintroduce unsafe full runtime reload on save
- [x] Keep the Workbench DB-first, with legacy XML only as bootstrap/compatibility support during the transition
