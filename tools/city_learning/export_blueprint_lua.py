"""Export a validated semantic city blueprint as an executable RME Lua script."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any


class LuaExportError(RuntimeError):
    """Raised when a blueprint cannot safely be exported to a map script."""


DISABLED_MESSAGE = (
    "city blueprint Lua export is disabled because procedural city generation "
    "must not write map content."
)


def _quoted(value: str) -> str:
    return json.dumps(value, ensure_ascii=True)


def _road_runs(blueprint: dict[str, Any]) -> list[dict[str, Any]]:
    rows: dict[tuple[int, str], list[int]] = {}
    for tile in blueprint["streetNetwork"]["tiles"]:
        rows.setdefault((tile["y"], tile.get("kind", "street")), []).append(tile["x"])
    runs: list[dict[str, Any]] = []
    for (y, kind), xs in sorted(rows.items()):
        start = previous = sorted(xs)[0]
        for x in sorted(xs)[1:]:
            if x == previous + 1:
                previous = x
                continue
            runs.append({"y": y, "x1": start, "x2": previous, "kind": kind})
            start = previous = x
        runs.append({"y": y, "x1": start, "x2": previous, "kind": kind})
    return runs


def emit_lua_script(blueprint: dict[str, Any], origin_x: int = 1000, origin_y: int = 1000) -> str:
    """Create an undoable ground-floor city materialization script for RME."""
    raise LuaExportError(DISABLED_MESSAGE)
    validation = blueprint.get("validation", {})
    if not validation.get("valid"):
        raise LuaExportError("Only validated city blueprints can be exported to Lua.")
    town = blueprint.get("sourceProfile", {}).get("town", {}).get("name", "Learned Style")
    width = int(blueprint["bounds"]["width"])
    height = int(blueprint["bounds"]["height"])
    road_brush = str(blueprint.get("style", {}).get("roadBrush", "cobblestone"))
    desert = road_brush == "sandstone"
    base_brush = "sand" if desert else "grass"
    floor_brush = "sandstone" if desert else "wooden floor"
    wall_brush = "egypt wall" if desert else "framework wall"
    runs = _road_runs(blueprint)

    lines = [
        f"-- @Title: Generated City Blueprint - {town}",
        "-- @Description: Applies a validated learned city ground-floor blueprint with undo/redo",
        "-- @Author: RME City Learning",
        "-- @Version: 1.0.0",
        "-- This script materializes roads and ground-floor building shells only.",
        "if not app.hasMap() then",
        '\tprint("[City Blueprint] No map open.")',
        "\treturn",
        "end",
        "",
        f"local WIDTH = {width}",
        f"local HEIGHT = {height}",
        "local ROAD_RUNS = {",
    ]
    for run in runs:
        lines.append(
            f'\t{{ y = {run["y"]}, x1 = {run["x1"]}, x2 = {run["x2"]}, kind = {_quoted(run["kind"])} }},'
        )
    lines.extend(["}", "local BUILDINGS = {"])
    for building in blueprint["buildings"]:
        bounds = building["bounds"]
        entrance = building["entrance"]
        lines.append(
            "\t{ "
            f"role = {_quoted(building['role'])}, "
            f"minX = {bounds['minX']}, maxX = {bounds['maxX']}, "
            f"minY = {bounds['minY']}, maxY = {bounds['maxY']}, "
            f"entranceX = {entrance['x']}, entranceY = {entrance['y']} "
            "},"
        )
    lines.extend([
        "}",
        "",
        "local dlg = Dialog({ title = \"Generate Validated City Blueprint\", width = 430 })",
        f'dlg:label({{ text = {_quoted("Learned style: " + town + " | Ground floor prototype")} }})',
        'dlg:label({ text = "Generate into an empty or grass-only area; review before saving." })',
        "dlg:separator()",
        f'dlg:number({{ id = "cx", label = "Start X:", value = {origin_x}, min = 1, max = 65000 }})',
        f'dlg:number({{ id = "cy", label = "Start Y:", value = {origin_y}, min = 1, max = 65000 }})',
        'dlg:number({ id = "cz", label = "Floor Z:", value = 7, min = 0, max = 15 })',
        "dlg:newrow()",
        f'dlg:input({{ id = "baseBrush", label = "Base Brush:", text = {_quoted(base_brush)} }})',
        f'dlg:input({{ id = "roadBrush", label = "Road Brush:", text = {_quoted(road_brush)} }})',
        "dlg:newrow()",
        f'dlg:input({{ id = "floorBrush", label = "Building Floor:", text = {_quoted(floor_brush)} }})',
        f'dlg:input({{ id = "wallBrush", label = "Building Wall:", text = {_quoted(wall_brush)} }})',
        "dlg:newrow()",
        'dlg:check({ id = "overwrite", text = "Clear existing items/house ids in area", selected = false })',
        "dlg:separator()",
        'dlg:button({ id = "go", text = "Generate Prototype", focus = true, onclick = function(d) d:close() end })',
        "dlg:show()",
        "",
        "local data = dlg.data",
        "local cx, cy, cz = math.floor(data.cx), math.floor(data.cy), math.floor(data.cz)",
        "local map = app.map",
        "local baseBrush, roadBrush = data.baseBrush, data.roadBrush",
        "local floorBrush, wallBrush = data.floorBrush, data.wallBrush",
        "",
        "local function requireBrush(name)",
        "\tif not Brushes.get(name) then error(\"Unknown brush: \" .. name) end",
        "end",
        "requireBrush(baseBrush)",
        "requireBrush(roadBrush)",
        "requireBrush(floorBrush)",
        "requireBrush(wallBrush)",
        "",
        "local function absoluteTile(x, y)",
        "\treturn map:getOrCreateTile(cx + x, cy + y, cz)",
        "end",
        "",
        "if not data.overwrite then",
        "\tfor y = 0, HEIGHT - 1 do",
        "\t\tfor x = 0, WIDTH - 1 do",
        "\t\t\tlocal tile = map:getTile(cx + x, cy + y, cz)",
        "\t\t\tif tile then",
        "\t\t\t\tlocal groundName = tile.ground and string.lower(tile.ground.name or \"\") or \"\"",
        "\t\t\t\tlocal usableGround = not tile.ground or string.find(groundName, \"grass\", 1, true)",
        "\t\t\t\tif tile.itemCount > 0 or tile.isHouseTile or tile.hasWall or not usableGround then",
        '\t\t\t\t\terror("Area contains non-grass content. Choose an empty area or explicitly clear it.")',
        "\t\t\t\tend",
        "\t\t\tend",
        "\t\tend",
        "\tend",
        "end",
        "",
        'app.transaction("Generate Validated City Blueprint", function()',
        "\tfor y = 0, HEIGHT - 1 do",
        "\t\tfor x = 0, WIDTH - 1 do",
        "\t\t\tlocal tile = absoluteTile(x, y)",
        "\t\t\tif data.overwrite then",
        "\t\t\t\tfor index = #tile.items, 1, -1 do tile:removeItem(tile.items[index]) end",
        "\t\t\t\ttile.houseId = 0",
        "\t\t\tend",
        "\t\t\ttile:applyBrush(baseBrush, false)",
        "\t\tend",
        "\tend",
        "",
        "\tfor _, run in ipairs(ROAD_RUNS) do",
        "\t\tfor x = run.x1, run.x2 do",
        "\t\t\tlocal tile = absoluteTile(x, run.y)",
        "\t\t\ttile:applyBrush(roadBrush, false)",
        "\t\tend",
        "\tend",
        "",
        "\tlocal wallTiles = {}",
        "\tfor _, building in ipairs(BUILDINGS) do",
        "\t\tfor y = building.minY, building.maxY do",
        "\t\t\tfor x = building.minX, building.maxX do",
        "\t\t\t\tlocal tile = absoluteTile(x, y)",
        "\t\t\t\ttile:applyBrush(floorBrush, false)",
        "\t\t\t\tlocal perimeter = x == building.minX or x == building.maxX or y == building.minY or y == building.maxY",
        "\t\t\t\tlocal entrance = x == building.entranceX and y == building.entranceY",
        "\t\t\t\tif perimeter and not entrance then",
        "\t\t\t\t\ttile:applyBrush(wallBrush, false)",
        "\t\t\t\t\ttable.insert(wallTiles, tile)",
        "\t\t\t\tend",
        "\t\t\tend",
        "\t\tend",
        "\tend",
        "",
        "\tfor _, run in ipairs(ROAD_RUNS) do",
        "\t\tfor x = run.x1, run.x2 do",
        "\t\t\tlocal tile = absoluteTile(x, run.y)",
        "\t\t\ttile:borderize()",
        "\t\tend",
        "\tend",
        "\tfor _, tile in ipairs(wallTiles) do tile:wallize() end",
        "end)",
        "",
        "app.setCameraPosition(cx + math.floor(WIDTH / 2), cy + math.floor(HEIGHT / 2), cz)",
        'print(string.format("[City Blueprint] Applied %dx%d %s prototype. Review and save the OTBM only if accepted.", WIDTH, HEIGHT, roadBrush))',
        "",
    ])
    return "\n".join(lines)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("blueprint", type=Path, help="validated rme-city-blueprint JSON")
    parser.add_argument("--output", required=True, type=Path, help="Lua file to create")
    parser.add_argument("--origin-x", type=int, default=1000)
    parser.add_argument("--origin-y", type=int, default=1000)
    arguments = parser.parse_args(argv)
    try:
        blueprint = json.loads(arguments.blueprint.read_text(encoding="utf-8"))
        script = emit_lua_script(blueprint, arguments.origin_x, arguments.origin_y)
        arguments.output.parent.mkdir(parents=True, exist_ok=True)
        arguments.output.write_text(script, encoding="ascii", newline="\n")
    except (OSError, json.JSONDecodeError, LuaExportError) as error:
        print(f"[city-lua] error: {error}", file=sys.stderr)
        return 1
    print(f"[city-lua] wrote RME script to {arguments.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
