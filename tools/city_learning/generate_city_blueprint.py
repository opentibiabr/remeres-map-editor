"""Generate novel, validated semantic city blueprints from learned urban profiles."""

from __future__ import annotations

import argparse
import gzip
import json
import random
import sys
from pathlib import Path
from typing import Any, Iterable
from xml.sax.saxutils import escape


class GenerationError(RuntimeError):
    """Raised when learned material cannot support a city blueprint."""


DISABLED_MESSAGE = (
    "semantic city generation is disabled. City Learning remains available for "
    "read-only corpus/model extraction only."
)


def _required_roles(width: int, height: int) -> list[str]:
    roles = ["temple", "depot"]
    area = width * height
    if area >= 3000:
        roles.extend(["bank", "shop"])
    if area >= 5000:
        roles.append("guildhall")
    return roles


def _minimum_buildings(width: int, height: int) -> int:
    return min(42, max(12, (width * height) // 280))


def _district_candidates(model: dict[str, Any], source_city: str | None) -> list[dict[str, Any]]:
    districts = [
        district for district in model.get("districts", ())
        if district.get("urbanQuality", {}).get("eligibleForGeneration")
    ]
    if source_city:
        districts = [
            district for district in districts
            if district.get("town", {}).get("name", "").lower() == source_city.lower()
        ]
    return sorted(
        districts,
        key=lambda district: (
            district.get("role") != "main",
            -district.get("urbanQuality", {}).get("score", 0),
            district.get("town", {}).get("name", ""),
            district.get("index", 0),
        ),
    )


def _select_template(
    templates: list[dict[str, Any]],
    role: str,
    source_city: str | None,
    rng: random.Random,
) -> dict[str, Any] | None:
    matching = [
        template for template in templates
        if template.get("role") == role and (
            not source_city or template.get("town", {}).get("name", "").lower() == source_city.lower()
        )
    ]
    if not matching and role != "residence":
        matching = [
            template for template in templates
            if template.get("role") == "residence" and (
                not source_city or template.get("town", {}).get("name", "").lower() == source_city.lower()
            )
        ]
    if not matching:
        matching = [template for template in templates if template.get("role") == role]
    if not matching:
        return None
    return matching[rng.randrange(len(matching))]


def _rect_tiles(bounds: dict[str, int]) -> set[tuple[int, int]]:
    return {
        (x, y)
        for x in range(bounds["minX"], bounds["maxX"] + 1)
        for y in range(bounds["minY"], bounds["maxY"] + 1)
    }


def _add_road_band(
    roads: set[tuple[int, int]],
    kinds: dict[tuple[int, int], str],
    kind: str,
    horizontal: bool,
    value: int,
    start: int,
    end: int,
    width: int = 2,
) -> None:
    priorities = {"street": 1, "avenue": 2, "plaza": 3}
    for offset in range(width):
        for coordinate in range(start, end + 1):
            position = (coordinate, value + offset) if horizontal else (value + offset, coordinate)
            roads.add(position)
            if priorities[kind] >= priorities.get(kinds.get(position, ""), 0):
                kinds[position] = kind


def _street_network(
    width: int,
    height: int,
    rng: random.Random,
) -> tuple[set[tuple[int, int]], dict[str, int], dict[tuple[int, int], str]]:
    margin = 4
    center_x = width // 2 + rng.randint(-2, 2)
    center_y = height // 2 + rng.randint(-2, 2)
    roads: set[tuple[int, int]] = set()
    kinds: dict[tuple[int, int], str] = {}
    _add_road_band(roads, kinds, "avenue", True, center_y, margin, width - margin - 1)
    _add_road_band(roads, kinds, "avenue", False, center_x, margin, height - margin - 1)
    offset_x = max(13, width // 4)
    offset_y = max(13, height // 4)
    for x in (center_x - offset_x, center_x + offset_x):
        if margin + 5 < x < width - margin - 5:
            _add_road_band(roads, kinds, "street", False, x, margin + 4, height - margin - 5)
    for y in (center_y - offset_y, center_y + offset_y):
        if margin + 5 < y < height - margin - 5:
            _add_road_band(roads, kinds, "street", True, y, margin + 4, width - margin - 5)
    plaza = {
        (x, y)
        for x in range(center_x - 4, center_x + 6)
        for y in range(center_y - 4, center_y + 6)
    }
    roads.update(plaza)
    for position in plaza:
        kinds[position] = "plaza"
    return roads, {
        "minX": center_x - 4,
        "maxX": center_x + 5,
        "minY": center_y - 4,
        "maxY": center_y + 5,
    }, kinds


def _candidate_placements(
    roads: set[tuple[int, int]],
    width: int,
    height: int,
    building_width: int,
    building_height: int,
) -> list[tuple[dict[str, int], dict[str, int], dict[str, int]]]:
    results: list[tuple[dict[str, int], dict[str, int], dict[str, int]]] = []
    for road_x, road_y in sorted(roads):
        candidates = (
            (road_x - building_width // 2, road_y - building_height, road_x, road_y - 1),
            (road_x - building_width // 2, road_y + 1, road_x, road_y + 1),
            (road_x - building_width, road_y - building_height // 2, road_x - 1, road_y),
            (road_x + 1, road_y - building_height // 2, road_x + 1, road_y),
        )
        for min_x, min_y, entrance_x, entrance_y in candidates:
            bounds = {
                "minX": min_x,
                "maxX": min_x + building_width - 1,
                "minY": min_y,
                "maxY": min_y + building_height - 1,
            }
            if bounds["minX"] < 3 or bounds["minY"] < 3 or bounds["maxX"] >= width - 3 or bounds["maxY"] >= height - 3:
                continue
            if (entrance_x, entrance_y) not in _rect_tiles(bounds):
                continue
            results.append((
                bounds,
                {"x": entrance_x, "y": entrance_y, "z": 7},
                {"x": road_x, "y": road_y, "z": 7},
            ))
    return results


def _has_clearance(
    bounds: dict[str, int],
    roads: set[tuple[int, int]],
    occupied: set[tuple[int, int]],
    reserved: set[tuple[int, int]],
) -> bool:
    footprint = _rect_tiles(bounds)
    if footprint & roads or footprint & occupied or footprint & reserved:
        return False
    buffered = {
        (x, y)
        for x in range(bounds["minX"] - 1, bounds["maxX"] + 2)
        for y in range(bounds["minY"] - 1, bounds["maxY"] + 2)
    }
    return not (buffered & occupied)


def _road_distance(
    start: tuple[int, int],
    targets: set[tuple[int, int]],
    roads: set[tuple[int, int]],
) -> int | None:
    if start not in roads:
        return None
    pending = [(start, 0)]
    visited = {start}
    for position, distance in pending:
        if position in targets:
            return distance
        x, y = position
        for neighbour in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)):
            if neighbour in roads and neighbour not in visited:
                visited.add(neighbour)
                pending.append((neighbour, distance + 1))
    return None


def generate_blueprint(
    model: dict[str, Any],
    templates: list[dict[str, Any]],
    width: int,
    height: int,
    seed: int,
    source_city: str | None = None,
) -> dict[str, Any]:
    """Generate a new city layout using learned profiles, not copied tile geometry."""
    raise GenerationError(DISABLED_MESSAGE)
    if width < 40 or height < 40:
        raise GenerationError("City blueprints require at least 40 x 40 tiles.")
    candidates = _district_candidates(model, source_city)
    if not candidates:
        raise GenerationError("No eligible learned urban district matches the requested style.")
    source = candidates[0]
    rng = random.Random(seed)
    road_tiles, plaza, road_kinds = _street_network(width, height, rng)
    road_styles = source.get("streetGraph", {}).get("styleBrushes", ())
    road_brush = road_styles[0]["value"] if road_styles else "cobblestone"
    road_palette = [
        {"brush": style["value"], "observedTiles": style["count"]}
        for style in road_styles[:4]
    ]
    required_roles = _required_roles(width, height)
    minimum_buildings = _minimum_buildings(width, height)
    desired_roles = required_roles + ["shop"] * max(2, minimum_buildings // 7)
    desired_roles += ["public"] * max(1, minimum_buildings // 12)
    desired_roles += ["residence"] * max(0, minimum_buildings - len(desired_roles))
    buildings: list[dict[str, Any]] = []
    lots: list[dict[str, Any]] = []
    occupied: set[tuple[int, int]] = set()
    green_areas = [
        {"minX": 3, "maxX": 10, "minY": 3, "maxY": 10},
        {"minX": width - 11, "maxX": width - 4, "minY": height - 11, "maxY": height - 4},
    ]
    reserved = set().union(*(_rect_tiles(area) for area in green_areas))
    plaza_tiles = _rect_tiles(plaza)
    civic_roles = {"temple", "depot", "bank"}

    for role in desired_roles:
        template = _select_template(templates, role, source_city, rng)
        dimensions = template.get("dimensions", {}) if template else {}
        building_width = max(5, min(12, int(dimensions.get("width", 6))))
        building_height = max(5, min(10, int(dimensions.get("height", 6))))
        placements = _candidate_placements(road_tiles, width, height, building_width, building_height)
        rng.shuffle(placements)
        if role in civic_roles or role == "shop":
            placements.sort(
                key=lambda placement: (
                    abs(placement[2]["x"] - (plaza["minX"] + plaza["maxX"]) // 2) +
                    abs(placement[2]["y"] - (plaza["minY"] + plaza["maxY"]) // 2),
                    placement[0]["minY"],
                    placement[0]["minX"],
                )
            )
        chosen = next((
            placement for placement in placements
            if _has_clearance(placement[0], road_tiles, occupied, reserved)
        ), None)
        if chosen is None:
            if role in required_roles:
                raise GenerationError(f"Could not place required {role} lot in requested area.")
            continue
        bounds, entrance, street_access = chosen
        route_to_center = _road_distance(
            (street_access["x"], street_access["y"]),
            plaza_tiles,
            road_tiles,
        )
        occupied.update(_rect_tiles(bounds))
        building_id = len(buildings) + 1
        lot = {
            "id": building_id,
            "bounds": bounds,
            "frontage": street_access,
            "role": role,
        }
        lots.append(lot)
        connector_count = (template or {}).get("tagCounts", {}).get("vertical_connector", 0)
        building = {
            "id": building_id,
            "role": role,
            "bounds": bounds,
            "entrance": entrance,
            "streetAccess": street_access,
            "routeToCenter": route_to_center,
            "floorCount": int(dimensions.get("floorCount", 1)),
            "templateHouse": (template or {}).get("house", {}).get("name"),
            "verticalLinks": (
                [{"fromZ": 7, "toZ": 8, "purpose": "urban service or basement"}]
                if connector_count else []
            ),
        }
        buildings.append(building)

    maximum_civic_route = max(12, min(width, height) // 4)
    blueprint = {
        "format": "rme-city-blueprint",
        "version": 1,
        "seed": seed,
        "bounds": {"width": width, "height": height, "z": 7},
        "sourceProfile": {
            "town": source.get("town", {}),
            "districtIndex": source.get("index"),
            "qualityScore": source.get("urbanQuality", {}).get("score"),
        },
        "style": {"roadBrush": road_brush, "roadPalette": road_palette},
        "requirements": {
            "requiredRoles": required_roles,
            "minimumBuildingCount": minimum_buildings,
            "maximumCivicRouteToCenter": maximum_civic_route,
        },
        "streetNetwork": {
            "tiles": [{"x": x, "y": y, "z": 7, "kind": road_kinds[(x, y)]} for x, y in sorted(road_tiles)],
            "plaza": plaza,
            "connected": True,
        },
        "lots": lots,
        "buildings": buildings,
        "greenAreas": green_areas,
    }
    validation = validate_blueprint(blueprint)
    blueprint["streetNetwork"]["connected"] = validation["metrics"]["streetsConnected"]
    blueprint["validation"] = validation
    return blueprint


def render_blueprint_svg(blueprint: dict[str, Any], scale: int = 7) -> str:
    """Render a compact review map for validating layout before OTBM materialization."""
    width = blueprint["bounds"]["width"]
    height = blueprint["bounds"]["height"]
    pixels_w = width * scale
    pixels_h = height * scale
    lines = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{pixels_w}" height="{pixels_h}" viewBox="0 0 {pixels_w} {pixels_h}">',
        "<style>",
        ".terrain{fill:#6f9b43}.road{fill:#77736b}.plaza{fill:none;stroke:#d2c091;stroke-width:2}",
        ".green{fill:#4d822d;stroke:#37651e;stroke-width:1}.building{stroke:#3a291e;stroke-width:1}",
        ".residence{fill:#d9b57a}.shop{fill:#d58a45}.depot{fill:#7c86bd}.temple{fill:#ead48a}.bank{fill:#bf9c60}.guildhall{fill:#b36855}.public{fill:#bc896a}",
        ".entrance{fill:#151515}.label{font-family:Arial,sans-serif;font-size:9px;fill:#1c140c}",
        "</style>",
        f'<rect class="terrain" width="{pixels_w}" height="{pixels_h}"/>',
    ]
    for green in blueprint.get("greenAreas", ()):
        lines.append(
            f'<rect class="green" x="{green["minX"] * scale}" y="{green["minY"] * scale}" '
            f'width="{(green["maxX"] - green["minX"] + 1) * scale}" '
            f'height="{(green["maxY"] - green["minY"] + 1) * scale}"/>'
        )
    for tile in blueprint.get("streetNetwork", {}).get("tiles", ()):
        lines.append(f'<rect class="road" x="{tile["x"] * scale}" y="{tile["y"] * scale}" width="{scale}" height="{scale}"/>')
    plaza = blueprint.get("streetNetwork", {}).get("plaza")
    if plaza:
        lines.append(
            f'<rect class="plaza" x="{plaza["minX"] * scale}" y="{plaza["minY"] * scale}" '
            f'width="{(plaza["maxX"] - plaza["minX"] + 1) * scale}" '
            f'height="{(plaza["maxY"] - plaza["minY"] + 1) * scale}"/>'
        )
    for building in blueprint.get("buildings", ()):
        bounds = building["bounds"]
        role = escape(building["role"])
        x = bounds["minX"] * scale
        y = bounds["minY"] * scale
        building_width = (bounds["maxX"] - bounds["minX"] + 1) * scale
        building_height = (bounds["maxY"] - bounds["minY"] + 1) * scale
        lines.append(
            f'<rect class="building {role}" x="{x}" y="{y}" width="{building_width}" height="{building_height}">'
            f"<title>{role}</title></rect>"
        )
        entrance = building["entrance"]
        lines.append(
            f'<circle class="entrance" cx="{(entrance["x"] + 0.5) * scale}" '
            f'cy="{(entrance["y"] + 0.5) * scale}" r="{max(1, scale // 3)}"/>'
        )
        if role != "residence":
            lines.append(f'<text class="label" x="{x + 2}" y="{y + 10}">{role}</text>')
    validation = blueprint.get("validation", {})
    title = "valid" if validation.get("valid") else "invalid"
    lines.append(f"<title>{title} city blueprint</title>")
    lines.append("</svg>")
    return "\n".join(lines) + "\n"


def validate_blueprint(blueprint: dict[str, Any]) -> dict[str, Any]:
    """Reject street and lot layouts that cannot function as a walkable city."""
    errors: list[str] = []
    road_tiles = {
        (tile["x"], tile["y"])
        for tile in blueprint.get("streetNetwork", {}).get("tiles", ())
    }
    connected = False
    if road_tiles:
        remaining = set(road_tiles)
        pending = [remaining.pop()]
        while pending:
            x, y = pending.pop()
            for neighbour in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)):
                if neighbour in remaining:
                    remaining.remove(neighbour)
                    pending.append(neighbour)
        connected = not remaining
    if not connected:
        errors.append("Street network is disconnected.")

    occupied: set[tuple[int, int]] = set()
    role_counts: dict[str, int] = {}
    for building in blueprint.get("buildings", ()):
        role = building["role"]
        role_counts[role] = role_counts.get(role, 0) + 1
        footprint = _rect_tiles(building["bounds"])
        if footprint & road_tiles:
            errors.append(f"Building {building['id']} overlaps a street.")
        if footprint & occupied:
            errors.append(f"Building {building['id']} overlaps another lot.")
        occupied.update(footprint)
        entrance = (building["entrance"]["x"], building["entrance"]["y"])
        access = (building["streetAccess"]["x"], building["streetAccess"]["y"])
        if entrance not in footprint or access not in road_tiles or abs(entrance[0] - access[0]) + abs(entrance[1] - access[1]) != 1:
            errors.append(f"Building {building['id']} has no valid street entrance.")
    for role in blueprint.get("requirements", {}).get("requiredRoles", ()):
        if role_counts.get(role, 0) == 0:
            errors.append(f"Required building role is missing: {role}.")
    minimum_buildings = blueprint.get("requirements", {}).get("minimumBuildingCount", 0)
    if len(blueprint.get("buildings", ())) < minimum_buildings:
        errors.append("City has too few occupied lots for its size.")
    plaza = blueprint.get("streetNetwork", {}).get("plaza")
    plaza_tiles = _rect_tiles(plaza) if plaza else set()
    maximum_civic_route = blueprint.get("requirements", {}).get("maximumCivicRouteToCenter")
    routes_to_center: dict[str, int | None] = {}
    if maximum_civic_route is not None:
        for building in blueprint.get("buildings", ()):
            if building["role"] not in {"temple", "depot", "bank"}:
                continue
            access = (building["streetAccess"]["x"], building["streetAccess"]["y"])
            distance = _road_distance(access, plaza_tiles, road_tiles)
            routes_to_center[building["role"]] = distance
            if distance is None or distance > maximum_civic_route:
                errors.append(f"Civic building is too far from the center: {building['role']}.")
    return {
        "valid": not errors,
        "errors": errors,
        "metrics": {
            "streetsConnected": connected,
            "roadTileCount": len(road_tiles),
            "buildingCount": len(blueprint.get("buildings", ())),
            "lotCount": len(blueprint.get("lots", ())),
            "roleCounts": role_counts,
            "civicRoutesToCenter": routes_to_center,
        },
    }


def _load_templates(path: Path) -> list[dict[str, Any]]:
    with gzip.open(path, "rt", encoding="utf-8") as stream:
        return [json.loads(line) for line in stream if line.strip()]


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--model", type=Path, required=True, help="city-learning-model.json")
    parser.add_argument("--templates", type=Path, required=True, help="building-templates.jsonl.gz")
    parser.add_argument("--output", type=Path, required=True, help="output blueprint JSON")
    parser.add_argument("--source-city", help="preferred learned style city name")
    parser.add_argument("--preview", type=Path, help="optional SVG review rendering")
    parser.add_argument("--width", type=int, required=True)
    parser.add_argument("--height", type=int, required=True)
    parser.add_argument("--seed", type=int, default=1)
    arguments = parser.parse_args(argv)
    try:
        model = json.loads(arguments.model.read_text(encoding="utf-8"))
        blueprint = generate_blueprint(
            model,
            _load_templates(arguments.templates),
            arguments.width,
            arguments.height,
            arguments.seed,
            arguments.source_city,
        )
        arguments.output.parent.mkdir(parents=True, exist_ok=True)
        arguments.output.write_text(json.dumps(blueprint, ensure_ascii=True, indent=2) + "\n", encoding="utf-8")
        if arguments.preview:
            arguments.preview.parent.mkdir(parents=True, exist_ok=True)
            arguments.preview.write_text(render_blueprint_svg(blueprint), encoding="utf-8")
    except (GenerationError, OSError, json.JSONDecodeError) as error:
        print(f"[city-generation] error: {error}", file=sys.stderr)
        return 1
    print(
        f"[city-generation] wrote {len(blueprint['buildings'])} buildings and "
        f"{blueprint['validation']['metrics']['roadTileCount']} road tiles to {arguments.output}"
        + (f" with preview {arguments.preview}" if arguments.preview else "")
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
