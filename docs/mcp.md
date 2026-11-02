# Built-in MCP server

Remere's Map Editor can expose the map you have open to an AI client over the
[Model Context Protocol](https://modelcontextprotocol.io). The client gets tools
to read the map by coordinate region, inspect tiles, items, houses, spawns,
waypoints and zones, render an area as an image, and edit the map through Lua.

The server lives inside the editor process, so it always acts on the map
currently in focus — no export step, no second copy of the map.

## Turning it on

`MCP -> MCP Server` opens the panel.

- **Enable server** binds a listener on `127.0.0.1` at the chosen port
  (default `7331`). The setting is remembered, so the server comes back up on
  the next launch.
- **Allow write operations** is off by default. While it is off, `run_lua` is
  refused and the AI can only read.
- **Copy config** puts a ready-to-paste snippet for the selected client on the
  clipboard.
- The **log** shows every tool call with its duration, plus refusals and errors.

The listener is bound to loopback only, so nothing outside the machine can reach
it. Requests carrying a cross-origin `Origin` header are rejected, which is what
stops a web page in your browser from driving the editor.

## Connecting a client

The endpoint is `http://127.0.0.1:<port>/mcp`, Streamable HTTP transport.

**Claude Code**

```bash
claude mcp add --transport http remeres http://127.0.0.1:7331/mcp
```

**Claude Desktop** (`claude_desktop_config.json`)

```json
"mcpServers": {
  "remeres": {
    "type": "http",
    "url": "http://127.0.0.1:7331/mcp"
  }
}
```

**Codex** (`~/.codex/config.toml`)

```toml
[mcp_servers.remeres]
url = "http://127.0.0.1:7331/mcp"
```

**A client that only speaks stdio** — use the bundled bridge, which needs
nothing but Python:

```json
"remeres": {
  "command": "python",
  "args": ["tools/mcp_stdio_bridge.py", "http://127.0.0.1:7331/mcp"]
}
```

## Tools

### Orientation

| Tool | Purpose |
| --- | --- |
| `map_info` | Name, dimensions, tile count, and how many houses, towns, waypoints, zones and spawns the map has. |
| `editor_state` | Unsaved changes, undo/redo availability, selection bounds, active brush. |
| `selection_get` | The positions currently selected in the editor. |

### Reading an area

`map_read_region` takes two opposite corners (`from` and `to`, each
`{x, y, z}`) and covers one or several floors. Three modes, cheapest first:

- `summary` (default) — tile counts, blocking ratio, the most common grounds and
  items, which houses and zones the area touches, spawn and monster counts.
  This is the mode to call first.
- `ascii` — one character per tile per floor, with a legend derived from the
  minimap colours. Good for seeing corridors, rooms and water at a glance.
- `tiles` — full per-tile detail for non-empty tiles, paginated through
  `limit` and `offset` (use the `nextOffset` the previous call returned).

`map_render_region` renders a single floor as a PNG. `mode=sprites` draws the
real client sprites offscreen — this is the one to use to actually *see* an
area, and unlike `map_screenshot` it is not limited to what the editor window
happens to be showing. `mode=minimap` (default) paints minimap colours instead:
much cheaper, better for large areas and overall layout.

Any region scan is capped at 262144 tiles (512x512 on one floor). Past that the
tool refuses and asks for a narrower region — pick the area with `summary`
first, then zoom in.

| Tool | Purpose |
| --- | --- |
| `map_read_region` | Region contents as a summary, an ASCII map, or full tile detail. |
| `map_render_region` | One floor of a region as a PNG image. |
| `map_find` | Tiles matching item id/name, monster name, house, zone, action or unique id, spawn or house exit. Region-scoped or whole-map. |
| `tile_get` | Full detail for up to 256 specific positions, including teleport destinations, container contents, door and depot ids. |

### Content

| Tool | Purpose |
| --- | --- |
| `item_info` | Everything about an item type by server id. |
| `item_search` | Item ids by name, exact matches first. |
| `house_list` / `house_get` | Houses, optionally by town or name; `house_get` includes every tile. |
| `town_list` | Towns with ids and temple positions. |
| `waypoint_list` | Waypoints, optionally restricted to a region. |
| `zone_list` | Zone ids and names. |
| `spawn_list` | Monster and npc spawns with radius and the creatures inside. |
| `monster_types_list` / `npc_types_list` | Which creature names this installation can place. |


### Start here

`generation_guide` returns the working order for building a map from a
description — pick brushes, lay terrain, carve, detail, verify, look at it —
and is registered first so it is the most visible tool in the list. Call it
before generating anything substantial.

### Seeing the palette

Choosing "the right brush for a river" is impossible from a list of names, so
these expose the palette the way the editor organises it, and draw brushes as
images.

| Tool | Purpose |
| --- | --- |
| `tileset_list` | Tilesets and their categories (terrain, doodad, item, raw, …) with the brushes inside — the palette structure. Browse by tileset instead of guessing names. |
| `brush_info` | One brush: kind, whether it auto-borders, the item it shows in the palette, and which tilesets and categories it belongs to. |
| `brush_preview` | A PNG of what the brush actually produces: painted onto a scratch area, auto-bordered, and drawn with real sprites. Check a brush is the water or mountain you meant before touching the map. |
| `brush_apply` | Apply it for real. Borders, wall corners and carpet edges are computed by the brush. |
| `terrain_pairing` | How a ground brush relates to the others: z-order, which of a pair draws the border, whether each declares outer/inner borders. Pick pairs that blend before painting. |
| `border_check` | Find hard seams where two ground brushes meet with no border between them, grouped by brush pair. Also flags ground placed as raw item ids, which no brush owns and autoborder can never fix. `fix=true` borderizes them. |

### Item and creature configuration

Everything an item or creature carries is both readable and writable:

- **Items** — `count`, `actionId`, `uniqueId`, `text`, and the complex state:
  a teleport's `destination`, a container's `contents` (nested), a door's
  `doorId`, a depot's `depotId`. Pass them in the item object anywhere
  `tile_edit` takes an item; read them back from `tile_get`.
- **Creatures** — `spawnTime`, `weight`, and `direction` (north/east/south/west)
  via `spawn_manage add_creature`.
- **Spawns** — `radius` at creation and afterwards through `spawn_manage update`.

```json
{ "tiles": [{ "position": {"x":1000,"y":1000,"z":7},
              "addItems": [{ "id": 1387, "destination": {"x":1050,"y":1050,"z":7} },
                           { "id": 1740, "actionId": 5001,
                             "contents": [{"id": 2160, "count": 25}] }] }] }
```

### Find, select, palette

| Tool | Purpose |
| --- | --- |
| `map_search` | The Edit > Find family, over the whole map, a region, or the selection: everything, unique, action, container, writeable, duplicated items, walls on walls, a specific item id, a monster by name. |
| `selection_op` | The Select menu against the current selection: select_region, borderize, randomize, delete, count/remove monsters, set_spawn_time, remove/replace an item id, remove duplicated items. Undoable. |
| `palette_select` | Set the editor's active brush, palette page and brush size — what a person would click. |
| `editor_settings` | Read or change `automagic` (auto-border while drawing — it changes what `brush_apply` produces), selection floor mode, and selection compensation. |

### What the player sees

The client shows a 19x15 tile window centred on the player (`ClientMapWidth+1`
by `ClientMapHeight+1`, matching the editor's Show Ingame Box). A tile with no
ground renders as nothing, so a cave or room whose edges were never closed off
shows the player unmapped space — invisible in the editor unless you go looking.

| Tool | Purpose |
| --- | --- |
| `client_view` | The client viewport at one position: the box, the unmapped tiles inside it, and optionally a PNG of exactly that view. |
| `client_view_scan` | Sweep every standable tile in an area and report where a player would see unmapped space, plus the distinct tiles that need ground. The fix list for a newly carved interior. |

### Diagnostics

| Tool | Purpose |
| --- | --- |
| `map_validate` | Scan the whole map for problems, including houses without a door, duplicate door ids, bed counts that disagree with the actual beds, house tiles missing PZ, monster spawns inside a protection zone, and creatures standing outside any spawn radius. Also: items without ground, stacked grounds, duplicate items, walls on walls, spawns with no creature and creatures with no spawn, unresolvable creatures, broken house/town links, waypoints on empty tiles, duplicate unique ids. Reports only. |
| `teleport_graph` | Every teleport with its destination, flagging the ones that lead nowhere — no destination, or a destination that is empty, groundless or blocking. |
| `path_check` | Walkability on one floor. With `from`+`to`, whether a player can walk between them and the path. With only `from`, flood-fills the walkable area and says whether it is sealed — the way to verify a boss arena or quest room has no leaks. |
| `floor_transitions` | Stairs, ladders and holes in a region and where each leads, flagging destinations that are missing or blocked **and** stairs with no way back — the classic dungeon dead end. |
| `map_statistics` | Counts across the map: tiles, items, blocking tiles, containers, action/unique ids, creatures, tiles per floor, top monsters. |

### Editing

Every write goes through the editor's action queue as `ACTION_MCP`, so it shows
up in the actions history and reverts with Ctrl+Z. All of these are refused
while the panel is in read-only mode.

| Tool | Purpose |
| --- | --- |
| `tile_edit` | Batch tile mutation in a single undo step: ground, add/remove items (with count, action id, unique id, text), clear items, map flags, house id, zones, optional borderize/wallize. Prefer one call with many tiles. |
| `brush_apply` | Apply a named editor brush over a list of positions. This is how you get correct borders, wall connections and doodad composition instead of placing raw ids. |
| `brush_list` | The brushes this installation has. |
| `editor_action` | undo, redo, save, goto, select, deselect, clear_selection. |
| `house_manage` | create / update / delete / set_exit / assign_tiles / unassign_tiles |
| `town_manage` | create / update / delete (refuses to orphan houses unless forced) |
| `waypoint_manage` | create / move / delete |
| `zone_manage` | create / delete / assign_tiles / unassign_tiles |
| `spawn_manage` | create / update (radius) / delete / add_creature / remove_creature, monster or npc. `add_creature` takes `spawnTime`, `weight` and the `direction` the creature faces. |

### Regions and whole-map operations

| Tool | Purpose |
| --- | --- |
| `region_copy` / `region_paste` | Duplicate a room, tower or camp through the editor clipboard. Undoable. |
| `stamp_manage` / `stamp_place` | A named library of reusable pieces for the session, placed with 90-degree rotation and mirroring. A village out of one house. Undoable. |
| `region_transform` | Rotate or mirror an area in place, for symmetric arenas and dungeon wings. 90/270 needs a square area. Undoable. |
| `region_replace_items` | Swap one item id for another across a region or the whole map; `toItemId: 0` deletes. Carries action and unique ids over. Undoable. |
| `map_cleanup` | The Edit > Tools passes over the whole map: remove invalid tiles, clear invalid houses, borderize, randomize, clear modified state, remove corpses, remove unreachable tiles, remove duplicated items, remove empty spawns. **Cannot be undone** and clears the undo history, so it requires `confirm: true`. Run `map_validate` first. |
| `map_open` / `map_new` / `map_properties` | Open an .otbm, start an empty map, or read/change name, description, dimensions and sidecar filenames. Refuses to discard unsaved work unless told to. |
| `map_import` | Merge another .otbm at an offset, with per-policy handling of houses and spawns. Requires `confirm: true`. |
| `map_from_bitmap` | Paint terrain from an image, one pixel per tile, each colour mapped to a brush — so borders and transitions are generated properly. Image by path or inline base64. Undoable. |
| `import_creatures` | Load monster or npc definitions from an OT `.xml` or a directory of Lua creature scripts. |
| `export_minimap` | Write minimap images (png / bmp / otmm) for all floors, the ground floor, one floor, or the selection. |
| `map_screenshot` | Capture the editor's live viewport — whatever the map window is showing at the current zoom. Bounded by the window size, so for a specific region prefer `map_render_region` with `mode=sprites`. |

### Client assets (read-only)

These expose the loaded client. None of them writes: `docs/static-data.md`
describes a CipSoft-compatible contract for `staticdata`, `staticmapdata` and
`mapdata`, and exporting stays exclusively with the editor's own menu.

| Tool | Purpose |
| --- | --- |
| `client_info` | Version, assets directory, sprite and appearance counts, valid item id range, and a summary of `catalog-content.json`. |
| `appearance_get` | The complete appearance record for one id, straight from the client's protobuf: every flag, frame group, sprite info, animation, market and cyclopedia data. |
| `appearance_search` | Find appearances by name/description, and/or by any `AppearanceFlags` field being set. |
| `sprite_render` | A client sprite as a PNG, by sprite id or by item id. |
| `item_sprite_info` | How an item is drawn: dimensions, layers, patterns, animation phases, draw offset and height, minimap colour, and the sprite ids per layer/pattern/frame. |
| `clientdata_read` | Parse an exported `staticdata`, `staticmapdata` or `mapdata` into JSON. House previews are decoded into absolute world positions. |

Appearance records come straight from protobuf's own JSON conversion, so they
stay exactly in step with `source/protobuf/appearances.proto` as client versions
change. Sprite identity is always `GameSprite::getSpriteID`, never the GL
texture id — see rule 9 in `docs/static-data.md`.

### Scripting escape hatch

`run_lua` executes Lua inside the editor and returns its `print()` output. It
reuses the editor's existing scripting API (`source/lua/`), so anything the
Script Manager can do is reachable — useful for procedural generation, where
the `noise`, `geo` and `algo` modules already do the heavy lifting. Wrap edits
in `app.transaction(function() ... end)` so they land as one undo step.

```lua
app.transaction(function()
  for x = 1000, 1004 do
    for y = 1000, 1004 do
      app.map:getOrCreateTile(x, y, 7).ground = Item.get(431)
    end
  end
end)
app.refresh()
```

## Notes for maintainers

- Transport and JSON-RPC: `source/mcp/mcp_server.cpp`. Only `POST /mcp` is
  implemented; there is no SSE stream because every tool call is one round trip.
- Tool handlers run on the GUI thread via `callOnGui` in
  `source/mcp/mcp_gui_call.h`, because `Map`, `Tile` and the GUI are not thread
  safe. The 30 second deadline there is what keeps a modal dialog in the editor
  from hanging a client connection.
- Tools are registered per domain through the registry in `mcp_tools.{h,cpp}`:
  `mcp_tools_map.cpp` (reading), `mcp_tools_entities.cpp` (listing),
  `mcp_tools_edit.cpp` and `mcp_tools_manage.cpp` (writing),
  `mcp_tools_analyze.cpp` (diagnostics), `mcp_tools_ops.cpp` (regions, session,
  screenshot), `mcp_tools_assets.cpp` (client assets), `mcp_render.cpp`, and
  `mcp_tools_script.cpp`.
- Writes go through `TileBatch` in `mcp_write.{h,cpp}`, which follows
  `Editor::drawInternal`: deep-copy the tile, mutate the copy, hand it to one
  `Action`. `Action::commit` and its undo already reconcile house membership and
  monster/npc spawns from the swapped tiles, so handlers never do that
  bookkeeping themselves.
- A tool that fails returns a result with `isError` set rather than a JSON-RPC
  error, so the model reads the message and corrects its next call.
- `map_cleanup` is the one write path that is not undoable: the editor's bulk
  passes clear the action queue, which is why the tool demands `confirm: true`.
- `MapCanvas::CaptureScreenshot` (`map_display.cpp`) is the capture half of
  `TakeScreenshot`, returning the image instead of writing a timestamped file.
  It is viewport-bound by nature; `renderTileRegion` in `mcp_render.cpp` is the
  offscreen path for arbitrary regions.
- `renderTileRegion` composites each item's base sprite only — no animation
  phase or per-layer compositing. `buildCyclopediaSatelliteChunk`
  (`iomap_otbm.cpp`) is the full-fidelity reference if previews ever need to
  match the exporter exactly.
- Item reading and construction live in `mcp_tools.cpp` (`itemToJson`,
  `buildItem`) so the read and write tools cannot drift apart on complex
  subclasses.
