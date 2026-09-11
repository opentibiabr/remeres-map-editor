#include "world/world_document.h"
#include <chrono>
#include <fstream>
#include <stdexcept>

namespace {
	void check(bool good, const char* message) {
		if (!good) {
			throw std::runtime_error(message);
		}
	}
	void write(const std::filesystem::path &file, const std::string &value) {
		std::ofstream stream(file, std::ios::binary | std::ios::trunc);
		stream << value;
		stream.close();
		check(!stream.fail(), "write external document");
	}
}

void runWorldExternalTests(const std::filesystem::path &scratch) {
	using namespace world_layers;
	const auto root = scratch / ("external-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	check(std::filesystem::create_directory(root), "reserve external editing fixture");
	write(root / "map.otbm", "unchanged otbm");
	write(root / "items.xml", "<items/>");
	WorldLayerDocument document;
	std::string error;
	check(document.create(root / "map.world.json", root / "map.otbm", root / "items.xml", "external", error), "create external editing project");
	auto project = document.data();
	for (const auto name : { "one", "two" }) {
		Layer layer;
		layer.id = name;
		layer.schemaVersion = 2;
		layer.file = root / (layer.id + ".layer.json");
		Object object;
		object.id = layer.id + ".anchor";
		object.kind = ObjectKind::Anchor;
		object.position = { 100, 100, 7 };
		layer.objects.push_back(object);
		project.layers.push_back(layer);
	}
	check(document.editProject(project, error) && document.save(error), "save two independent documents");
	const auto baseline = document.data();
	project = baseline;
	project.find("one.anchor")->position.x = 101;
	check(document.editProject(project, error), "edit first document locally");
	auto external = baseline.layers[1];
	external.objects[0].position.y = 102;
	write(external.file, serializeLayer(external));
	std::vector<WorldExternalChange> changes;
	check(document.reconcileExternal(false, changes, error) == WorldExternalResult::Reloaded, "automatically reload unrelated external file");
	check(document.data().find("one.anchor")->position.x == 101 && document.data().find("two.anchor")->position.y == 102 && document.dirty(), "external reload retains local edits");
	check(document.undo() && document.data().find("one.anchor")->position.x == 100 && document.data().find("two.anchor")->position.y == 102, "unrelated history survives external reload");
	check(document.redo() && document.save(error), "redo and save after external reload");
	project = document.data();
	project.find("one.anchor")->position.x = 103;
	WorldDocumentChange native;
	check(document.makeChange(project, native, error) && document.exchange(native, error), "record native action before conflict");
	external = baseline.layers[0];
	external.objects[0].position.x = 104;
	write(external.file, serializeLayer(external));
	check(document.reconcileExternal(false, changes, error) == WorldExternalResult::Conflict, "same-file local and external edits conflict");
	check(document.data().find("one.anchor")->position.x == 103, "conflict keeps local version");
	write(external.file, "{\"schemaVersion\":2,");
	check(document.reconcileExternal(false, changes, error) == WorldExternalResult::Invalid && document.data().find("one.anchor")->position.x == 103, "truncated JSON preserves the valid draft");
	std::filesystem::path draft;
	check(document.saveDraft(root / "draft", draft, error), "preserve local draft outside the active catalog");
	Project reopened;
	Diagnostics diagnostics;
	check(loadProject(draft, reopened, diagnostics) && reopened.find("one.anchor")->position.x == 103, "draft contains local values and relative references");
	write(external.file, serializeLayer(external));
	project = document.data();
	project.find("two.anchor")->position.y = 105;
	check(document.editProject(project, error), "another local change while conflict is pending");
	check(document.reconcileExternal(true, changes, error) == WorldExternalResult::Reloaded, "confirmed reload resolves conflicting file");
	check(document.data().find("one.anchor")->position.x == 104 && document.data().find("two.anchor")->position.y == 105, "confirmed discard is limited to conflicting documents");
	check(!document.canExchange(native, error), "retire commands for superseded revisions");
	check(document.save(error), "save remaining local edits after conflict resolution");
	std::filesystem::remove(external.file);
	check(document.reconcileExternal(false, changes, error) == WorldExternalResult::Invalid && document.data().find("one.anchor"), "file removal is not an empty document");
	write(external.file, serializeLayer(external));
	project = document.data();
	const auto renamed = root / "renamed.layer.json";
	std::filesystem::rename(project.layers[0].file, renamed);
	project.layers[0].file = renamed;
	write(project.file, serializeProject(project));
	check(document.reconcileExternal(false, changes, error) == WorldExternalResult::Reloaded && document.data().find("one.anchor") && document.data().layers[0].file == renamed, "catalog-driven rename preserves object identity");
	std::string bytes;
	check(readFile(root / "map.otbm", bytes, error) && bytes == "unchanged otbm", "external editing leaves OTBM byte-for-byte unchanged");
}
