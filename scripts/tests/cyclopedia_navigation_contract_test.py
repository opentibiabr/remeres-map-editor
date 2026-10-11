#!/usr/bin/env python3
"""Check navigation schema/wiring and numeric expressions without building RME.

Execution only reads source files (or git show for --source-ref) and evaluates a
whitelist of numeric AST nodes. It does not launch an exporter or write assets.
"""

import argparse
import ast
from pathlib import Path
import re
import subprocess
import unittest


ROOT = Path(__file__).resolve().parents[2]
SOURCE_REF = None


def read_source(path):
    if SOURCE_REF:
        return subprocess.check_output(
            ["git", "show", f"{SOURCE_REF}:{path}"], cwd=ROOT, text=True
        )
    return (ROOT / path).read_text(encoding="utf-8")


def source_block(source, start, end):
    return source.split(start, 1)[1].split(end, 1)[0] if start in source else ""


def source_speed(expression, speed):
    for token, replacement in (
        ("static_cast<uint8_t>", "u8"),
        ("static_cast<int>", "integer"),
        ("tile->getGroundSpeed()", "speed"),
        ("std::clamp", "clamp"),
        ("/", "//"),
    ):
        expression = expression.replace(token, replacement)
    tree = ast.parse(expression, mode="eval")
    allowed = (ast.Expression, ast.Call, ast.Name, ast.Load, ast.Constant,
               ast.BinOp, ast.Add, ast.FloorDiv)
    if any(not isinstance(node, allowed) for node in ast.walk(tree)):
        raise ValueError("Unexpected speed expression; update constrained check")
    functions = dict(u8=lambda value: value & 255, integer=int,
                     clamp=lambda value, low, high: max(low, min(high, value)))
    return eval(compile(tree, "<navigation speed expression>", "eval"),
                {"__builtins__": {}, **functions}, {"speed": speed})


class CyclopediaNavigationContractTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = read_source("source/iomap_otbm.cpp")
        cls.schema = read_source("source/protobuf/mapdata.proto")
        cls.tile = source_block(cls.source, "std::array<uint8_t, 3> buildCyclopediaNavigationTile(",
                                "bool buildCyclopediaNavigationChunk(")
        cls.chunk = source_block(cls.source, "bool buildCyclopediaNavigationChunk(",
                                 "struct SatelliteTinyPixel")
        cls.export = source_block(cls.source, "bool IOMapOTBM::serializeCyclopediaMapData(",
                                  "bool IOMapOTBM::saveCyclopediaMapData(")

    def test_navigation_is_optional_field_six_outside_visual_field_three(self):
        self.assertRegex(self.schema, r"repeated MapAssets mapassets\s*=\s*3;")
        self.assertRegex(self.schema, r"repeated MapAssets navigationassets\s*=\s*6;")
        enum = source_block(self.schema, "enum AssetsType {", "}")
        self.assertEqual(re.findall(r"(\w+)\s*=\s*(\d+)", enum),
                         [("SUBAREA", "0"), ("SATELLITE", "1"), ("MINIMAP", "2")])
        self.assertIn("pendingAsset.config.navigation ? mapData.add_navigationassets() : mapData.add_mapassets()", self.export)
        self.assertIn("pendingAsset.config.minimap ? MapAssets_AssetsType_MINIMAP : MapAssets_AssetsType_SATELLITE", self.export)

    def test_exact_tile_geometry_uses_only_medium_sparse_jobs(self):
        self.assertTrue("CyclopediaNavigationLayer { true, 1.0 / 32.0, 512, 1.0, true }" in self.source,
                        "Navigation must use exactly one pixel per tile at medium scale")
        self.assertIn("area.width > CyclopediaNavigationLayer.chunkSize", self.chunk)
        self.assertIn("area.height > CyclopediaNavigationLayer.chunkSize", self.chunk)
        self.assertIn("area.startX + x, area.startY + y, area.floor", self.chunk)
        self.assertIn("floorPlan.chunkStartsBySize[", self.export)
        self.assertIn("minimapConfig.chunkSize == CyclopediaNavigationLayer.chunkSize", self.export)
        self.assertIn("appendCyclopediaAsset(CyclopediaNavigationLayer, job.area, navigationChunk", self.export)
        self.assertNotIn("resample", self.chunk)
        self.assertNotIn("map.begin", self.chunk)
        self.assertLess(self.export.index("buildCyclopediaNavigationChunk(map, job.area"),
                        self.export.index("buildCyclopediaMinimapChunk(map, job.area"))

    def test_channel_flags_and_default_groundless_cells_are_not_exploration(self):
        flags = dict(re.findall(r"constexpr uint8_t (\w+)\s*=\s*(\d+);", self.tile))
        self.assertEqual(flags, {"notPathable": "2", "notWalkable": "4", "empty": "8"})
        self.assertIn("channels { static_cast<uint8_t>(notWalkable | empty), 10, 255 }", self.tile)
        groundless = source_block(self.tile, "if (!tile->ground) {", "}")
        self.assertIn("return channels;", groundless)
        self.assertIn("channels[2] = tile->getMiniMapColor();", self.tile)
        self.assertIn("channels[0] = 0;", self.tile)

    def test_static_items_and_ground_drive_collision_independently_of_color(self):
        self.assertIn("includeItem(tile->ground);", self.tile)
        self.assertIn("for (const Item* item : tile->items)", self.tile)
        self.assertIn("if (item->isBlocking())", self.tile)
        self.assertIn("channels[0] |= notWalkable;", self.tile)
        self.assertIn("if (item->isAvoidable())", self.tile)
        self.assertIn("channels[0] |= notPathable;", self.tile)
        self.assertNotRegex(self.tile, r"if\s*\([^)]*(?:getMiniMapColor|channels\[2\])")
        self.assertNotIn("tile->isBlocking()", self.tile)

    def test_source_speed_rounds_up_and_clamps_without_overflow(self):
        match = re.search(r"channels\[1\]\s*=\s*([^;]+);", self.tile)
        self.assertIsNotNone(match, "Navigation speed source expression is absent")
        for speed, expected in ((0, 1), (1, 1), (9, 1), (10, 1), (11, 2),
                                (99, 10), (100, 10), (101, 11), (2549, 255),
                                (2550, 255), (2551, 255), (65535, 255)):
            with self.subTest(speed=speed):
                self.assertEqual(source_speed(match.group(1), speed), expected)

    def test_cip_bmp_encoding_keeps_alpha_opaque_and_geometry_unresampled(self):
        self.assertIn("std::memcpy(data + index, channels.data(), channels.size())", self.chunk)
        encoder = source_block(self.source, "bool encodeBmpV4(", "void write7BitEncodedInt(")
        self.assertIn("alpha ? alpha[sourceIndex] : 255", encoder)
        self.assertIn("encodeBmpV4(outputAssetChunk, bmpBytes)", self.export)
        self.assertIn("return !config.navigation;", self.export)
        self.assertIn("return !pendingAsset.config.navigation;", self.export)
        self.assertIn("pendingAssets.size() >= maxPendingAssets", self.export)

    def test_current_navigation_replaces_template_geometry(self):
        merge = source_block(self.source, "bool mergeCyclopediaTemplateMapData(",
                             "bool loadStaticDataTemplate(")
        self.assertIn("for (const auto &mapAsset : generatedMapData.navigationassets())", merge)
        self.assertIn("*mergedMapData.add_navigationassets() = mapAsset;", merge)
        self.assertNotIn("templateMapData.navigationassets()", merge)
        self.assertIn("if (mapAsset.type() != MapAssets_AssetsType_SUBAREA)", merge)

    def test_navigation_cannot_make_an_empty_visual_export_succeed(self):
        self.assertRegex(self.export,
                         r"if \(!pendingAsset\.config\.navigation\)\s*\{\s*hasAnyAsset = true;")
        self.assertEqual(self.export.count("hasAnyAsset = true;"), 1)
        self.assertIn("if (!hasAnyAsset)", self.export)

    def test_navigation_participates_in_backup_and_restore(self):
        backup = source_block(self.source, "bool backupMapAssetsFromPreviousMapData(",
                              "bool restoreCyclopediaMapAssetFile(")
        restore = source_block(self.source, "bool restoreCyclopediaMapAssets(",
                               "bool backupStaleCyclopediaAssetFiles(")
        self.assertIn("&previousMapData.navigationassets()", backup)
        self.assertIn("&mapData.navigationassets()", restore)
        self.assertTrue('filename.rfind("navigation-", 0)' in self.source,
                        "Navigation must have backup replacement keys")
        self.assertTrue('navigation ? "navigation-"' in self.source,
                        "Navigation filenames must use their own prefix")
        self.assertIn("isSafeRelativePath(assetRelativePath)", backup)
        self.assertIn("isSafeRelativePath(assetRelativePath)", restore)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-ref", help="Read tracked source at a Git revision")
    arguments, unittest_arguments = parser.parse_known_args()
    SOURCE_REF = arguments.source_ref
    unittest.main(argv=[__file__, *unittest_arguments])
