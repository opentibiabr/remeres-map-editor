#!/usr/bin/env python3
"""Check source wiring and origin arithmetic without compiling or rendering RME."""

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


def parenthesized(source, prefix):
    start = source.index(prefix) + len(prefix)
    depth = 1
    for index in range(start, len(source)):
        depth += (source[index] == "(") - (source[index] == ")")
        if depth == 0:
            return source[start:index]
    raise ValueError(f"Unclosed expression: {prefix}")


def evaluate_expression(expression, values):
    """Interpret only the numeric expressions used by the production origin code."""
    for token, replacement in (
        ("std::numeric_limits<uint16_t>::max()", "65535"),
        ("static_cast<int64_t>", "int"),
        ("static_cast<uint32_t>", "u32"),
        ("rme::MapGroundLayer", "ground"),
        ("pendingAsset.area.startX", "world_x"),
        ("pendingAsset.area.startY", "world_y"),
        ("area.startX", "world_x"),
        ("area.startY", "world_y"),
        ("area.floor", "floor"),
        ("std::max", "max"),
        ("||", " or "),
        ("&&", " and "),
    ):
        expression = expression.replace(token, replacement)
    tree = ast.parse(f"({expression})", mode="eval")
    allowed = (ast.Expression, ast.BinOp, ast.Add, ast.Sub, ast.Compare,
               ast.Lt, ast.Gt, ast.LtE, ast.GtE, ast.BoolOp, ast.Or, ast.And,
               ast.Call, ast.Name, ast.Load, ast.Constant)
    if any(not isinstance(node, allowed) for node in ast.walk(tree)):
        raise ValueError("Unexpected origin expression; update this constrained check")
    functions = {"int": int, "u32": lambda value: value & 0xFFFFFFFF, "max": max}
    return eval(compile(tree, "<origin source expression>", "eval"),
                {"__builtins__": {}, **functions}, values)


class CyclopediaOriginContractTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = read_source("source/iomap_otbm.cpp")
        constants = read_source("source/const.h")
        cls.ground = int(re.search(r"MapGroundLayer\s*=\s*(\d+)", constants).group(1))
        marker = "bool getCyclopediaAssetOrigin("
        cls.helper = (cls.source.split(marker, 1)[1].split("bool hasCyclopediaTileData(", 1)[0]
                      if marker in cls.source else None)
        cls.export = cls.source.split("bool IOMapOTBM::serializeCyclopediaMapData(", 1)[1]

    def encode_origin(self, world_x, world_y, floor):
        values = dict(world_x=world_x, world_y=world_y, floor=floor, ground=self.ground)
        if self.helper is None:
            # Read the old serializer too, so --source-ref demonstrates its misalignment.
            return tuple(evaluate_expression(parenthesized(self.export, f"topLeft->set_pos{axis}("), values)
                         for axis in ("x", "y"))
        for name, expression in re.findall(r"const int64_t (\w+)\s*=\s*([^;]+);", self.helper):
            values[name] = evaluate_expression(expression, values)
        if evaluate_expression(parenthesized(self.helper, "if ("), values):
            return None
        return tuple(evaluate_expression(re.search(rf"origin{axis}\s*=\s*([^;]+);", self.helper).group(1), values)
                     for axis in ("X", "Y"))

    def test_origin_round_trip_preserves_world_position_on_all_floor_groups(self):
        for floor, expected in ((0, (107, 207)), (7, (100, 200)),
                                (8, (99, 199)), (15, (92, 192))):
            with self.subTest(floor=floor):
                encoded = self.encode_origin(100, 200, floor)
                self.assertEqual(encoded, expected)
                self.assertEqual(tuple(coordinate - (7 - floor) for coordinate in encoded), (100, 200))

    def test_projected_domain_boundaries_remain_exact(self):
        for floor, low, high in ((0, 0, 65528), (7, 0, 65535),
                                 (8, 1, 65535), (15, 8, 65535)):
            for coordinate in (low, high):
                with self.subTest(floor=floor, coordinate=coordinate):
                    encoded = self.encode_origin(coordinate, coordinate, floor)
                    self.assertIsNotNone(encoded)
                    self.assertTrue(all(0 <= value <= 65535 for value in encoded))
                    self.assertEqual(tuple(value - (7 - floor) for value in encoded),
                                     (coordinate, coordinate))

    def test_out_of_domain_projected_x_or_y_rejects_instead_of_clamping(self):
        for floor, coordinate in ((0, 65529), (8, 0), (15, 7)):
            for x, y in ((coordinate, 100), (100, coordinate)):
                with self.subTest(floor=floor, x=x, y=y):
                    self.assertIsNone(self.encode_origin(x, y, floor))

    def test_shared_serializer_validates_before_rendering_and_writes_projected_origin(self):
        planning = self.export.split("if (jobs.empty())", 1)[0]
        guard = "if (!getCyclopediaAssetOrigin(area, originX, originY))"
        self.assertIn(guard, planning)
        rejection = planning.split(guard, 1)[1].split("jobs.emplace_back", 1)[0]
        self.assertIn("warning(", rejection)
        self.assertIn("return false;", rejection)
        self.assertIn("getCyclopediaAssetOrigin(pendingAsset.area, originX, originY)", self.export)
        self.assertIn("topLeft->set_posx(originX);", self.export)
        self.assertIn("topLeft->set_posy(originY);", self.export)
        self.assertIn("pendingAsset.config.minimap ? MapAssets_AssetsType_MINIMAP : MapAssets_AssetsType_SATELLITE", self.export)
        self.assertIn("pendingAsset.area.startX, pendingAsset.area.startY, pendingAsset.area.floor, encodedAsset.hashHex", self.export)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-ref", help="Read tracked source at a Git revision instead of the working tree")
    arguments, unittest_arguments = parser.parse_known_args()
    SOURCE_REF = arguments.source_ref
    unittest.main(argv=[__file__, *unittest_arguments])
