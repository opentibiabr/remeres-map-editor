#include "world/world_document.h"
#include "map_fixture.hpp"
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
		std::cout << "World layer identity, validation, undo, persistence and OTBM preservation passed\n";
		return 0;
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
