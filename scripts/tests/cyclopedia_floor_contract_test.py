#!/usr/bin/env python3
"""Check the Cyclopedia floor/asset source contract without compiling RME."""

import argparse
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


def constant(source, name, map_constants):
    value = re.search(rf"constexpr int {name}\s*=\s*([^;]+);", source).group(1).strip()
    if value.startswith("rme::"):
        return constant(map_constants, value.removeprefix("rme::"), map_constants)
    return int(value)


class CyclopediaFloorContractTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = read_source("source/iomap_otbm.cpp")
        cls.map_constants = read_source("source/const.h")
        cls.plan = cls.source.split("bool collectCyclopediaFloorPlans(", 1)[1].split(
            "int computeCyclopediaChunkPixelDimension(", 1
        )[0]
        cls.export = cls.source.split("bool IOMapOTBM::serializeCyclopediaMapData(", 1)[1]

    def test_export_range_includes_surface_and_deepest_basement(self):
        self.assertEqual(constant(self.source, "CyclopediaMinFloor", self.map_constants),
                         constant(self.map_constants, "MapMinLayer", self.map_constants))
        self.assertEqual(constant(self.source, "CyclopediaMaxFloor", self.map_constants),
                         constant(self.map_constants, "MapMaxLayer", self.map_constants))

    def test_planning_and_map_bounds_use_the_same_floor_range(self):
        floor_guard = ("tile->getZ() < CyclopediaMinFloor || "
                       "tile->getZ() > CyclopediaMaxFloor || !hasCyclopediaTileData(tile)")
        self.assertEqual(self.plan.count(floor_guard), 2)
        self.assertIn("CyclopediaFloorCount = CyclopediaMaxFloor - CyclopediaMinFloor + 1", self.source)
        self.assertIn("topLeftEdge->set_posz(CyclopediaMinFloor)", self.export)
        self.assertIn("bottomRightEdge->set_posz(CyclopediaMaxFloor)", self.export)
        self.assertIn("if (!floorPlan.bounds.hasData) {\n\t\t\tcontinue;", self.export)
        self.assertIn("floorPlan.chunkStartsBySize[", self.export)
        self.assertIn("for (const auto &floorPlan : floorPlans)", self.plan)
        for edge in ("minX", "minY", "maxX", "maxY"):
            operation = "min" if edge.startswith("min") else "max"
            self.assertIn(f"{edge} = std::{operation}({edge}, floorPlan.bounds.{edge})", self.plan)

    def test_minimap_and_satellite_share_scales_and_chunk_floor(self):
        def layers(name):
            block = self.source.split(f"{name} {{ {{", 1)[1].split("} };", 1)[0]
            return re.findall(r"\{ (?:true|false), ([^}]+) \}", block)

        expected = ["1.0 / 64.0, 1024, 0.5", "1.0 / 32.0, 512, 1.0", "1.0 / 16.0, 256, 2.0"]
        self.assertEqual(layers("CyclopediaMinimapLayers"), expected)
        self.assertEqual(layers("CyclopediaSatelliteLayers"), expected)
        self.assertIn("appendCyclopediaAsset(minimapConfig, job.area,", self.export)
        self.assertIn("appendCyclopediaAsset(satelliteConfig, job.area,", self.export)
        self.assertIn("topLeft->set_posz(static_cast<uint32_t>(std::max(pendingAsset.area.floor, 0)))", self.export)

    def test_template_merge_keeps_subareas_and_generated_floor_assets(self):
        merge = self.source.split("bool mergeCyclopediaTemplateMapData(", 1)[1].split(
            "bool loadStaticDataTemplate(", 1
        )[0]
        self.assertIn("if (mapAsset.type() != MapAssets_AssetsType_SUBAREA)", merge)
        self.assertIn("for (const auto &mapAsset : generatedMapData.mapassets())", merge)
        self.assertEqual(merge.count("*mergedMapData.add_mapassets() = mapAsset;"), 2)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-ref", help="Read tracked source at a Git revision instead of the working tree")
    arguments, unittest_arguments = parser.parse_known_args()
    SOURCE_REF = arguments.source_ref
    unittest.main(argv=[__file__, *unittest_arguments])
