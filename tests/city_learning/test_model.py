import unittest

from tools.city_learning.model import DistrictAccumulator, building_role, is_street_tile, semantic_signature


class CityLearningModelTests(unittest.TestCase):
	def test_semantic_signature_orders_relevant_tags(self):
		tile = {
			"tags": ["house_tile", "protection_zone"],
			"ground": {"id": 100, "tags": ["item", "ground", "grass"]},
			"items": [
				{"id": 200, "tags": ["item", "door", "wall"]},
				{"id": 201, "tags": ["item", "roof"]},
			],
		}

		self.assertEqual(
			semantic_signature(tile),
			"house_tile|wall|door|roof|grass|protection_zone",
		)

	def test_layout_merges_adjacent_tiles_with_the_same_signature(self):
		rows = []
		accumulator = DistrictAccumulator(
			source="synthetic.json",
			town={"id": 8, "name": "Thais"},
			district={"index": 1, "role": "main", "bounds": {"minX": 10, "maxX": 13, "minY": 20, "maxY": 20}},
			layout_sink=rows.append,
		)
		accumulator.add_tile({"position": {"x": 10, "y": 20, "z": 7}, "tags": ["grass"], "items": []})
		accumulator.add_tile({"position": {"x": 11, "y": 20, "z": 7}, "tags": ["grass"], "items": []})
		accumulator.add_tile({"position": {"x": 13, "y": 20, "z": 7}, "tags": ["water"], "items": []})

		profile, templates = accumulator.finalize()

		self.assertEqual(templates, [])
		self.assertEqual(profile["tileCount"], 3)
		self.assertEqual(
			rows,
			[{
				"source": "synthetic.json",
				"townId": 8,
				"districtIndex": 1,
				"z": 7,
				"y": 20,
				"runs": [[10, 2, "grass"], [13, 1, "water"]],
			}],
		)

	def test_anchor_house_produces_relative_multi_floor_template(self):
		accumulator = DistrictAccumulator(
			source="synthetic.json",
			town={"id": 8, "name": "Thais"},
			district={"index": 1, "role": "main", "bounds": {"minX": 9, "maxX": 15, "minY": 19, "maxY": 24}},
			layout_sink=lambda row: None,
		)
		accumulator.add_house({
			"id": 77,
			"name": "Harbour House",
			"townId": 8,
			"anchor": True,
			"exit": {"x": 9, "y": 20, "z": 7},
		})
		accumulator.add_house({"id": 78, "name": "Context Only", "townId": 8, "anchor": False})
		accumulator.add_tile({
			"position": {"x": 10, "y": 20, "z": 7},
			"houseId": 77,
			"tags": ["house_tile"],
			"items": [{"id": 300, "tags": ["item", "wall", "door"]}],
		})
		accumulator.add_tile({
			"position": {"x": 11, "y": 20, "z": 7},
			"houseId": 77,
			"tags": ["house_tile"],
			"items": [{"id": 301, "tags": ["item", "wall"]}],
		})
		accumulator.add_tile({
			"position": {"x": 10, "y": 20, "z": 6},
			"houseId": 77,
			"tags": ["house_tile"],
			"items": [{"id": 302, "tags": ["item", "roof"]}],
		})
		accumulator.add_tile({
			"position": {"x": 14, "y": 22, "z": 7},
			"houseId": 78,
			"tags": ["house_tile"],
			"items": [{"id": 303, "tags": ["item", "wall"]}],
		})

		_, templates = accumulator.finalize()

		self.assertEqual(len(templates), 1)
		template = templates[0]
		self.assertEqual(template["house"]["id"], 77)
		self.assertEqual(template["dimensions"], {"width": 2, "height": 1, "floorCount": 2, "tileCount": 3})
		self.assertEqual(
			template["footprint"],
			[
				{"z": 6, "rows": [[0, [[0, 1]]]]},
				{"z": 7, "rows": [[0, [[0, 2]]]]},
			],
		)
		self.assertEqual(template["tagCounts"], {"door": 1, "house_tile": 3, "roof": 1, "wall": 2})

	def test_street_graph_recognizes_urban_brushes_and_building_access(self):
		accumulator = DistrictAccumulator(
			source="synthetic.json",
			town={"id": 4, "name": "Ankrahmun"},
			district={
				"index": 1,
				"role": "main",
				"confidence": "high",
				"inferenceMethod": "house_clusters_with_temple_anchor",
				"bounds": {"minX": 10, "maxX": 14, "minY": 20, "maxY": 22},
			},
			layout_sink=lambda row: None,
		)
		accumulator.add_house({
			"id": 91,
			"name": "Ankrahmun Depot",
			"townId": 4,
			"anchor": True,
			"exit": {"x": 12, "y": 20, "z": 7},
		})
		accumulator.add_tile({
			"position": {"x": 12, "y": 20, "z": 7},
			"houseId": 91,
			"tags": ["house_tile"],
			"ground": {"id": 500, "name": "stone floor", "tags": ["ground", "floor"]},
			"items": [{"id": 501, "tags": ["item", "door", "wall"]}],
		})
		for x, brush in ((10, "sandstone"), (11, "sandstone"), (12, "drawbridge"), (13, "sandstone"), (14, "sandstone")):
			accumulator.add_tile({
				"position": {"x": x, "y": 21, "z": 7},
				"ground": {"id": 923, "name": brush, "brush": brush, "tags": ["item", "ground", "terrain"]},
				"items": [],
			})

		profile, templates = accumulator.finalize()

		self.assertTrue(is_street_tile({
			"position": {"x": 10, "y": 21, "z": 7},
			"ground": {"name": "sandstone", "brush": "sandstone", "tags": ["ground", "terrain"]},
			"items": [],
		}))
		self.assertFalse(is_street_tile({
			"position": {"x": 10, "y": 21, "z": 7},
			"ground": {"name": "dark tiled sandstone", "brush": "dark tiled sandstone", "tags": ["ground", "terrain"]},
			"items": [],
		}))
		self.assertFalse(is_street_tile({
			"position": {"x": 10, "y": 21, "z": 7},
			"ground": {"name": "sandstone", "brush": "sandstone mountain", "tags": ["ground", "terrain"]},
			"items": [{"id": 12, "tags": ["item", "road"]}],
		}))
		self.assertEqual(templates[0]["role"], "depot")
		self.assertEqual(profile["buildingRoles"], {"depot": 1})
		self.assertEqual(profile["streetGraph"]["primaryFloor"], 7)
		self.assertEqual(profile["streetGraph"]["roadTileCount"], 5)
		self.assertEqual(profile["streetGraph"]["componentCount"], 1)
		self.assertEqual(profile["streetGraph"]["accessibleBuildingCount"], 1)
		self.assertTrue(profile["urbanQuality"]["eligibleForGeneration"])

	def test_service_item_does_not_turn_a_named_residence_into_a_depot(self):
		self.assertEqual(building_role({"name": "Harbour House"}, {"depot": 1}), "residence")
		self.assertEqual(building_role({"name": "Central Depot"}, {}), "depot")

	def test_fragmented_observation_keeps_a_usable_main_street_as_training_data(self):
		accumulator = DistrictAccumulator(
			source="synthetic.json",
			town={"id": 1, "name": "Complex City"},
			district={
				"index": 1,
				"role": "main",
				"confidence": "high",
				"inferenceMethod": "house_clusters_with_temple_anchor",
				"bounds": {"minX": 0, "maxX": 20, "minY": 0, "maxY": 20},
			},
			layout_sink=lambda row: None,
		)
		for x in range(5):
			accumulator.add_tile({"position": {"x": x, "y": 3, "z": 7}, "ground": {"id": 100, "name": "road", "tags": ["ground", "road"]}, "items": []})
		for x in range(4):
			accumulator.add_tile({"position": {"x": x + 10, "y": 15, "z": 7}, "ground": {"id": 100, "name": "road", "tags": ["ground", "road"]}, "items": []})

		profile, _ = accumulator.finalize()

		self.assertTrue(profile["urbanQuality"]["eligibleForGeneration"])
		self.assertIn("fragmented observed street network", profile["urbanQuality"]["warnings"])

	def test_low_confidence_fallback_is_not_generation_training_material(self):
		accumulator = DistrictAccumulator(
			source="synthetic.json",
			town={"id": 99, "name": "Fallback"},
			district={
				"index": 1,
				"role": "main",
				"confidence": "low",
				"inferenceMethod": "temple_fallback",
				"bounds": {"minX": 0, "maxX": 20, "minY": 0, "maxY": 20},
			},
			layout_sink=lambda row: None,
		)
		for x in range(16):
			accumulator.add_tile({
				"position": {"x": x, "y": 10, "z": 7},
				"ground": {"id": 100, "name": "road", "tags": ["ground", "road"]},
				"items": [],
			})

		profile, _ = accumulator.finalize()

		self.assertFalse(profile["urbanQuality"]["eligibleForGeneration"])
		self.assertIn("low-confidence district inference", profile["urbanQuality"]["reasons"])

	def test_small_road_fragment_in_a_large_capture_is_not_an_urban_core(self):
		accumulator = DistrictAccumulator(
			source="synthetic.json",
			town={"id": 3, "name": "Forest"},
			district={
				"index": 1,
				"role": "main",
				"confidence": "high",
				"inferenceMethod": "house_clusters_with_temple_anchor",
				"bounds": {"minX": 0, "maxX": 150, "minY": 0, "maxY": 150},
			},
			layout_sink=lambda row: None,
		)
		for x in range(15):
			accumulator.add_tile({
				"position": {"x": x, "y": 5, "z": 7},
				"ground": {"id": 101, "name": "drawbridge", "brush": "drawbridge", "tags": ["ground", "terrain"]},
				"items": [],
			})

		profile, _ = accumulator.finalize()

		self.assertFalse(profile["urbanQuality"]["eligibleForGeneration"])
		self.assertIn("street core is too small for captured region", profile["urbanQuality"]["reasons"])


if __name__ == "__main__":
	unittest.main()
