#include "world/world_document.h"
#include "world/world_view_index.h"
#include "map_fixture.hpp"
#include "contract_v2.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

void runWorldExternalTests(const std::filesystem::path &scratch);
void runWorldFileTests(const std::filesystem::path &scratch);

namespace {
	void require(bool condition, const char* message) {
		if (!condition) {
			throw std::runtime_error(message);
		}
	}
	void viewIndexContract() {
		world_layers::Project project;
		project.schemaVersion = 2;
		world_layers::Layer active;
		active.id = "active";
		active.schemaVersion = 2;
		for (const auto &[id, position] : std::vector<std::pair<std::string, world_layers::Position>> {
				 { "first", { 100, 100, 7 } }, { "second", { 100, 100, 7 } }, { "far", { 105, 105, 7 } }, { "other_floor", { 100, 100, 8 } } }) {
			world_layers::Object object;
			object.id = id;
			object.kind = world_layers::ObjectKind::Anchor;
			object.position = position;
			active.objects.push_back(object);
		}
		auto contained = active.objects.front();
		contained.id = "contained";
		contained.container = "active.first";
		active.objects.push_back(contained);
		project.layers.push_back(active);
		auto disabled = active;
		disabled.id = "disabled";
		disabled.enabled = false;
		disabled.objects.front().id = "hidden";
		project.layers.push_back(disabled);

		WorldViewIndex index;
		index.rebuild(project, { { {}, "first", {}, "invalid fixture" } });
		const auto &sameTile = index.at({ 100, 100, 7 });
		require(sameTile.size() == 2, "spatial index excludes contained and inactive objects");
		require(index.entry(sameTile[0]).id == "first" && index.entry(sameTile[1]).id == "second", "spatial index preserves catalog order");
		require(index.entry(sameTile[0]).invalid && !index.entry(sameTile[1]).invalid, "spatial index retains per-object diagnostics");
		const auto &near = index.visible(7, 99, 99, 101, 101);
		require(near.size() == 2, "viewport query includes only visible objects on its floor");
		const auto &far = index.visible(7, 104, 104, 106, 106);
		require(far.size() == 1 && index.entry(far.front()).id == "far", "viewport bounds invalidate the cached query");
		require(index.find("other_floor") && !index.find("hidden") && !index.find("missing"), "identity lookup follows active top-level entries");
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
	void baseMovement() {
		using namespace world_layers;
		Project project;
		project.schemaVersion = 2;
		Layer layer;
		layer.id = "move";
		layer.schemaVersion = 2;
		layer.file = "move.layer.json";
		const Position source { 100, 100, 7 }, target { 101, 100, 7 };
		for (const auto id : { "move.bound", "move.original", "move.neighbor" }) {
			Object object;
			object.id = id;
			object.mode = SourceMode::Map;
			object.position = source;
			object.itemId = 2772;
			object.selector = Selector {};
			object.selector->itemId = 2772;
			object.selector->position = source;
			layer.objects.push_back(object);
		}
		layer.objects[1].mode = SourceMode::Replace;
		layer.objects[1].position = { 110, 100, 7 };
		project.layers.push_back(layer);
		Diagnostics diagnostics;
		require(project.rebuildIndex(diagnostics), "index movement declarations");
		WorldMapFixture map(project);
		map.tiles[WorldMapFixture::key(source)] = { true, true, false, false, { { 1, 2772 }, { 2, 2772 }, { 3, 2772 } } };
		map.tiles[WorldMapFixture::key(target)] = { true, true, false, false, { { 4, 2772 } } };
		ApplicationPlan plan;
		for (size_t i = 0; i < 3; ++i) {
			plan.objects.push_back({ project.layers[0].objects[i].id, i + 1 });
		}
		auto inactive = layer;
		inactive.id = "inactive";
		inactive.file = "inactive.layer.json";
		inactive.enabled = false;
		inactive.objects.resize(1);
		inactive.objects[0].id = "inactive.bound";
		std::string error;
		require(captureSelector(*inactive.objects[0].selector, map.tile(source).items, 1, error), "capture inactive binding before moving its original");
		project.layers.push_back(inactive);
		require(project.rebuildIndex(diagnostics), "index inactive movement declaration");
		WorldBaseMove move(project, plan, map, { -1, 0, 0 }, { 1, 2 });
		move.copied(1, 11);
		move.copied(2, 12);
		move.copied(3, 13);
		move.copied(11, 21); // Border processing takes another tile copy.
		move.copied(12, 22);
		map.tiles[WorldMapFixture::key(source)].items = { { 13, 2772 } };
		map.tiles[WorldMapFixture::key(target)].items = { { 4, 2772 }, { 21, 2772 }, { 22, 2772 } };
		require(move.finish(map, project, error), "native move updates exact originals across consecutive copies");
		require(project.find("move.bound")->position == target, "bound item's effective position follows the base item");
		require(project.find("move.original")->position == Position { 110, 100, 7 } && project.find("move.original")->selector->position == target, "replacement placement stays separate from its moved original");
		MapItem selected;
		require(project.find("inactive.bound")->position == target && resolveSelector(*project.find("inactive.bound")->selector, map.tile(target).items, selected, error) && selected.key == 21, "inactive binding follows the same original without activating its layer");
		require(!project.layers[1].enabled, "base movement preserves inactive layer status");
		require(resolveSelector(*project.find("move.bound")->selector, map.tile(target).items, selected, error) && selected.key == 21, "moved item selects its occurrence among identical destination items");
		require(resolveSelector(*project.find("move.original")->selector, map.tile(target).items, selected, error) && selected.key == 22, "second moved item keeps a distinct occurrence");
		require(resolveSelector(*project.find("move.neighbor")->selector, map.tile(source).items, selected, error) && selected.key == 13, "stationary neighbor receives refreshed occurrence preconditions");
		const auto before = serializeLayer(project.layers[0]);
		map.tiles[WorldMapFixture::key(target)].items.pop_back();
		require(!move.finish(map, project, error) && serializeLayer(project.layers[0]) == before, "overwritten managed original rejects the entire selector update");
	}

	void authoring(const std::filesystem::path &scratch) {
		using namespace world_layers;
		std::filesystem::path root;
		for (unsigned i = 0; i < 10000; ++i) {
			const auto path = scratch / ("authoring-" + std::to_string(i));
			if (std::filesystem::create_directory(path)) {
				root = path;
				break;
			}
		}
		require(!root.empty(), "reserve authoring fixture");
		write(root / "map.otbm", "unchanged base map");
		write(root / "items.xml", "<items/>");
		WorldLayerDocument document;
		std::string error;
		require(document.create(root / "map.world.json", root / "map.otbm", root / "items.xml", "authoring", error), "create unsaved catalog");
		require(document.dirty(), "new catalog is unsaved");
		auto project = document.data();
		Layer a, b;
		a.id = "quest";
		a.file = root / "quest.layer.json";
		a.schemaVersion = 2;
		b.id = "library";
		b.file = root / "library.layer.json";
		b.schemaVersion = 2;
		Object arrival;
		arrival.id = "quest.arrival";
		arrival.kind = ObjectKind::Anchor;
		arrival.position = { 100, 100, 7 };
		Object portal;
		portal.id = "quest.portal";
		portal.itemId = 1949;
		portal.position = { 101, 100, 7 };
		portal.teleport = Teleport { arrival.id, {} };
		a.objects = { arrival, portal };
		project.layers = { a, b };
		require(document.editProject(project, error), "create layers and declarations together");
		require(document.undo() && document.data().layers.empty(), "undo structural creation");
		require(document.redo() && document.data().find(arrival.id), "redo structural creation");
		require(document.save(error) && !document.dirty(), "save new catalog and layer files");
		const auto original = bytes(root / "map.otbm");
		project = document.data();
		require(!WorldLayerDocument::removeObject(project, arrival.id, error), "referenced object deletion must report dependencies");
		require(WorldLayerDocument::renameObject(project, arrival.id, "library.arrival", error), "rename stable identity explicitly");
		require(project.find(portal.id)->teleport->destination == "library.arrival", "rename updates references");
		require(WorldLayerDocument::moveToLayer(project, "library.arrival", b.file, error), "move declaration to another file without changing identity");
		require(document.editProject(project, error), "record cross-document rename and move");
		require(document.undo() && document.data().find(arrival.id) && !document.dirty(), "undo restores identities, references and destination files");
		require(document.redo() && document.save(error), "redo and save cross-document operation");
		require(bytes(root / "map.otbm") == original, "structural authoring never modifies OTBM");
		project = document.data();
		project.find(portal.id)->attributes["text"] = Value { std::string("Unicode: biblioteca ç 漢字\nsegunda linha") };
		project.find(portal.id)->aidOverride = true;
		project.find(portal.id)->aid = 0;
		WorldDocumentChange old;
		require(document.makeChange(project, old, error), "prepare native action with source revisions");
		require(document.exchange(old, error), "apply native action");
		require(document.exchange(old, error), "exchange reverses native action");
		require(document.open(root / "map.world.json", error), "reload valid external revision");
		require(!document.exchange(old, error) && !document.dirty(), "old commands cannot overwrite a reloaded document");
		project = document.data();
		project.find(portal.id)->teleport->destination = "missing.object";
		require(document.editProject(project, error), "retain a temporarily invalid reference in an editable draft");
		require(!document.save(error), "invalid active configuration cannot be saved");
		require(document.undo() && !document.dirty(), "undo invalid reference without losing saved data");
		project = document.data();
		require(WorldLayerDocument::removeObject(project, portal.id, error), "delete unreferenced object");
		require(WorldLayerDocument::removeObject(project, "library.arrival", error), "delete former target after dependency removal");
		project.layers.erase(project.layers.begin());
		require(document.editProject(project, error) && document.save(error), "remove a layer from the active catalog");
		require(std::filesystem::exists(a.file), "removing a layer preserves its previous disk file for recovery");
		require(document.undo() && document.data().find(portal.id), "undo layer and object removal");
		const auto sourceCatalog = bytes(root / "map.world.json"), sourceLayer = bytes(a.file);
		project = document.data();
		project.find(portal.id)->attributes["text"] = Value { std::string("native history") };
		WorldDocumentChange native;
		require(document.makeChange(project, native, error) && document.exchange(native, error), "record native action before Save As");
		write(root / "copy.otbm", original);
		require(document.copyForMap(root / "copy.otbm", error, { &native, &old }), "copy and rebase both native and document histories");
		require(document.exchange(native, error), "native World action remains reversible in copied files");
		require(!document.exchange(old, error), "Save As must not revive an action invalidated by external reload");
		require(document.redo() && !document.data().find(portal.id), "redo structural deletion after Save As");
		require(document.undo() && document.data().find(portal.id), "undo restores deleted layers under copied paths");
		require(document.save(error), "save changes from rebased history");
		require(bytes(root / "map.world.json") == sourceCatalog && bytes(a.file) == sourceLayer, "rebased structural actions never write to original documents");
	}
}

int main(int argc, char** argv) {
	try {
		if (argc == 3 && std::string(argv[1]) == "--benchmark-project") {
			WorldLayerDocument document;
			std::string error;
			const auto measure = [&](const char* stage, const auto &operation) {
				const auto before = std::chrono::steady_clock::now();
				operation();
				const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - before).count();
				std::cout << stage << ": " << elapsed << " ms" << std::endl;
			};
			measure("open", [&] {
				if (!document.open(std::filesystem::u8path(argv[2]), error)) {
					throw std::runtime_error(error);
				}
			});
			std::cout << "objects: " << document.data().objects.size() << std::endl;
			for (int iteration = 0; iteration < 3; ++iteration) {
				measure("observe", [&] { require(!document.observedFiles().empty(), "observed catalog must not be empty"); });
				measure("poll", [&] {
					std::vector<WorldExternalChange> changes;
					require(document.reconcileExternal(false, changes, error) == WorldExternalResult::Unchanged, "benchmark requires unchanged input files");
				});
			}
			return 0;
		}
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
		viewIndexContract();
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
		baseMovement();
		authoring(scratch);
		runWorldFileTests(scratch);
		runWorldExternalTests(scratch);
		std::cout << "World layer identity, validation, undo, persistence and OTBM preservation passed\n";
		return 0;
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
