#include "world/world_document.h"
#include "map_fixture.hpp"
#include "contract_v2.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
	void require(bool condition, const char* message) {
		if (!condition) {
			throw std::runtime_error(message);
		}
	}
	std::string bytes(const std::filesystem::path &file) {
		std::string value, error;
		require(world_layers::readFile(file, value, error), "read fixture");
		return value;
	}
	void write(const std::filesystem::path &file, const std::string &value) {
		std::ofstream stream(file, std::ios::binary | std::ios::trunc);
		stream << value;
		require(stream.good(), "write fixture");
	}
}

int main(int argc, char** argv) {
	try {
		require(argc == 3, "expected fixture and scratch directories");
		const auto fixture = std::filesystem::path(argv[1]);
		const auto scratch = std::filesystem::path(argv[2]);
		std::filesystem::create_directories(scratch);
		write(scratch / "world.world.json", bytes(fixture / "world.world.json"));
		write(scratch / "black_knight.layer.json", bytes(fixture / "black_knight.layer.json"));
		write(scratch / "world.otbm", std::string { 'O', 'T', 'B', 'M', '\0', static_cast<char>(0xff), '\r', '\n' });
		write(scratch / "items.xml", "<items/>");
		const auto originalMap = bytes(scratch / "world.otbm");
		const auto originalProject = bytes(scratch / "world.world.json");
		const auto originalLayer = bytes(scratch / "black_knight.layer.json");
		WorldLayerDocument document;
		std::string error;
		require(document.open(scratch / "world.world.json", error), "open project");
		require(!document.dirty(), "open must be clean");
		auto object = *document.data().find("black_knight.exit");
		const auto replaces = object.replaces;
		object.position.x += 4;
		require(document.edit("black_knight.exit", object), "move object");
		require(document.dirty(), "move must dirty layer");
		require(document.data().find("black_knight.exit")->replaces == replaces, "replacement selector must stay anchored");
		require(world_layers::destination(document.data(), *document.data().find("black_knight.entry"))->x == object.position.x, "arrival must follow object");
		require(document.undo() && !document.dirty(), "undo returns to saved state");
		require(document.redo() && document.dirty(), "redo restores change");
		require(document.save(error), "save layer");
		require(bytes(scratch / "world.otbm") == originalMap, "OTBM bytes must not change");
		require(bytes(scratch / "world.world.json") == originalProject, "project bytes must not change");
		require(bytes(scratch / "black_knight.layer.json") != originalLayer, "layer must change");
		WorldLayerDocument reopened;
		require(reopened.open(scratch / "world.world.json", error), "reopen saved layer");
		require(*reopened.data().find("black_knight.exit") == object, "round trip must preserve object");
		object.aid = 24873;
		require(document.edit("black_knight.exit", object), "edit AID");
		write(scratch / "black_knight.layer.json", originalLayer);
		require(!document.save(error), "external write must block save");
		require(bytes(scratch / "black_knight.layer.json") == originalLayer, "external changes must remain intact");
		require(document.dirty(), "failed save must retain draft");

		WorldLayerDocument integrated;
		error.clear();
		require(integrated.open(scratch / "world.world.json", error), "open catalog for normal map editing");
		require(integrated.matchesMap(scratch / "." / "world.otbm"), "associate catalog with its OTBM");
		write(scratch / "unrelated.otbm", originalMap);
		require(!integrated.matchesMap(scratch / "unrelated.otbm"), "identical map bytes do not imply the same map file");
		require(!integrated.matchesMap(scratch / "missing.otbm"), "missing map cannot be associated");
		const auto before = *integrated.data().find("black_knight.exit");
		auto snapshot = before;
		snapshot.position.x += 2;
		snapshot.aid = 24873;
		require(integrated.exchange("black_knight.exit", snapshot), "native action commits a snapshot");
		require(snapshot == before && integrated.dirty(), "native action owns the previous value");
		require(!integrated.canUndo(), "native actions must not also populate the document history");
		require(integrated.exchange("black_knight.exit", snapshot) && !integrated.dirty(), "native undo returns to saved state");
		require(integrated.exchange("black_knight.exit", snapshot) && integrated.dirty(), "native redo restores the world edit");
		auto invalid = *integrated.data().find("black_knight.exit");
		invalid.position.z = 16;
		require(!integrated.exchange("black_knight.exit", invalid), "invalid native snapshot is rejected");
		invalid = *integrated.data().find("black_knight.exit");
		invalid.id = "renamed";
		require(!integrated.exchange("black_knight.exit", invalid), "native snapshots cannot change identity");
		std::filesystem::path copyRoot;
		for (unsigned i = 0; i < 10000; ++i) {
			const auto candidate = scratch / ("map-copy-" + std::to_string(i));
			if (std::filesystem::create_directory(candidate)) {
				copyRoot = candidate;
				break;
			}
		}
		require(!copyRoot.empty(), "reserve isolated map-copy fixture");
		const auto copyMap = copyRoot / "copy.otbm";
		write(copyMap, originalMap);
		const auto edited = *integrated.data().find("black_knight.exit");
		require(integrated.copyForMap(copyMap, error), "Save As copies the catalog and all layers");
		require(integrated.matchesMap(copyMap) && !integrated.dirty(), "copied catalog attaches to the new OTBM");
		require(*integrated.data().find("black_knight.exit") == edited, "Save As includes unsaved world edits");
		require(bytes(scratch / "black_knight.layer.json") == originalLayer && bytes(scratch / "world.world.json") == originalProject, "Save As preserves source layers and catalog");
		require(bytes(scratch / "world.otbm") == originalMap && bytes(copyMap) == originalMap, "catalog copy never serializes either OTBM");
		require(integrated.exchange("black_knight.exit", snapshot) && integrated.dirty(), "native undo remains valid after Save As");
		require(integrated.save(error), "save undo only into the copied layer");
		require(bytes(scratch / "black_knight.layer.json") == originalLayer, "later copied-map edits cannot modify source layers");
		const auto copiedLayer = bytes(copyRoot / "copy.world-layers" / "black_knight.layer.json");
		require(!integrated.copyForMap(copyMap, error), "Save As must not overwrite an existing catalog");
		require(bytes(copyRoot / "copy.world-layers" / "black_knight.layer.json") == copiedLayer, "rejected copy preserves destination");
		WorldLayerDocument copied;
		require(copied.open(copyRoot / "copy.world.json", error) && copied.matchesMap(copyMap), "normal OTBM reopening can discover its sibling catalog");
		std::error_code equivalentError;
		require(std::filesystem::equivalent(copied.data().items, scratch / "items.xml", equivalentError), "copied item catalog paths stay relative and resolve correctly");
		world_layers::Project project;
		world_layers::Diagnostics diagnostics;
		require(world_layers::loadProject(fixture / "world.world.json", project, diagnostics), "load shared fixture");
		WorldMapFixture map(project);
		world_layers::ApplicationPlan plan;
		require(world_layers::validateMap(project, map, plan, diagnostics), "pilot map validation");
		project.find("black_knight.entry")->aid = project.find("black_knight.exit")->aid = 24873;
		require(world_layers::validateMap(project, map, plan, diagnostics), "AID may repeat");
		project.find("black_knight.exit")->uid = 38012;
		require(!world_layers::validateMap(project, map, plan, diagnostics), "UID cannot repeat");
		project.find("black_knight.exit")->uid = 38013;
		map.ids.push_back({ 38012, 999, { 100, 100, 7 } });
		diagnostics.clear();
		require(!world_layers::validateMap(project, map, plan, diagnostics), "unconsumed original UID collision");
		world_layers::Layer layer;
		diagnostics.clear();
		require(!world_layers::parseLayer(R"({"schemaVersion":1,"id":"a","id":"b","objects":[]})", "duplicate.json", layer, diagnostics), "duplicate JSON property");
		diagnostics.clear();
		require(!world_layers::parseLayer(R"({"schemaVersion":3,"id":"future","objects":[]})", "future.json", layer, diagnostics), "future schema must be rejected");
		diagnostics.clear();
		require(!world_layers::parseLayer(R"({"schemaVersion":1,"id":"future","objects":[{"id":"entry","position":{"x":1,"y":1,"z":7},"origin":{"type":"layer","itemId":1949},"components":[{"type":"future"}]}]})", "future.json", layer, diagnostics), "unknown component must be rejected");
		auto entry = project.find("black_knight.entry");
		const auto originalTeleport = entry->teleport;
		entry->teleport->destination = "missing.object";
		diagnostics.clear();
		world_layers::validateProject(project, diagnostics);
		require(!diagnostics.empty() && diagnostics.front().field == "/components/destination", "missing destination must be diagnosed");
		entry->teleport = originalTeleport;
		entry->teleport->destinationOffset.z = -15;
		diagnostics.clear();
		world_layers::validateProject(project, diagnostics);
		require(!diagnostics.empty() && diagnostics.front().field == "/components/destinationOffset", "arrival overflow must be diagnosed");
		entry->teleport = originalTeleport;
		WorldMapFixture conflictMap(project);
		auto &originTile = conflictMap.tiles[WorldMapFixture::key(entry->position)];
		originTile.items.push_back({ 999, 1949, 0, true, {} });
		plan = {};
		diagnostics.clear();
		require(!world_layers::validateMap(project, conflictMap, plan, diagnostics) && plan.objects.empty(), "ambiguous original must not produce a partial plan");
		originTile.items.pop_back();
		originTile.house = true;
		diagnostics.clear();
		require(!world_layers::validateMap(project, conflictMap, plan, diagnostics), "house replacement must be rejected");
		originTile.house = false;
		const auto arrival = *world_layers::destination(project, *entry);
		auto &arrivalTile = conflictMap.tiles[WorldMapFixture::key(arrival)];
		arrivalTile.ground = false;
		diagnostics.clear();
		require(!world_layers::validateMap(project, conflictMap, plan, diagnostics), "arrival without ground must be rejected");
		arrivalTile.ground = true;
		arrivalTile.items.push_back({ 999, 1949, 0, true, entry->position });
		diagnostics.clear();
		require(!world_layers::validateMap(project, conflictMap, plan, diagnostics), "cycle through base-map teleport must be rejected");
		runWorldV2Tests(scratch);
		std::cout << "World layer identity, validation, undo, persistence and OTBM preservation passed\n";
		return 0;
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
