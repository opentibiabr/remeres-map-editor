#include "world/world_document.h"
#include "world/world_files.hpp"

#include <exception>
#include <fstream>
#include <functional>
#include <algorithm>
#include <set>
#include <nlohmann/json.hpp>
namespace {
	using namespace world_layers;
	using Files = std::map<std::filesystem::path, std::string>;
	Files documents(const Project &project) {
		Files result;
		result.emplace(project.file, serializeProject(project));
		for (const auto &layer : project.layers) {
			result.emplace(layer.file, serializeLayer(layer));
		}
		for (const auto &record : project.migrationRecords) {
			result.emplace(record.file, serializeMigration(record));
		}
		return result;
	}
	Project header(const Project &project) {
		auto result = project;
		result.objects.clear();
		result.migrationRecords.clear();
		for (auto &layer : result.layers) {
			layer.objects.clear();
		}
		return result;
	}
	const Layer* layerAt(const Project &project, const std::filesystem::path &file) {
		const auto found = std::find_if(project.layers.begin(), project.layers.end(), [&](const Layer &layer) { return layer.file == file; });
		return found == project.layers.end() ? nullptr : &*found;
	}
	bool sameLayer(const Layer &a, const Layer &b) {
		return a.file == b.file && a.schema == b.schema && a.schemaVersion == b.schemaVersion && a.id == b.id && a.name == b.name && a.objects == b.objects;
	}
	void errors(const Diagnostics &diagnostics, std::string &error) {
		for (const auto &entry : diagnostics) {
			error += entry.describe() + "\n";
		}
	}
	bool structure(Project &project, std::string &error) {
		Diagnostics diagnostics;
		if (project.schemaVersion != 1 && project.schemaVersion != 2) {
			error = "Unsupported catalog version";
			return false;
		}
		if (project.schemaVersion == 2) {
			Layer identifier;
			const nlohmann::ordered_json probe = { { "schemaVersion", 2 }, { "id", project.id }, { "objects", nlohmann::ordered_json::array() } };
			if (!parseLayer(probe.dump(), project.file, identifier, diagnostics)) {
				errors(diagnostics, error);
				return false;
			}
		}
		std::set<std::filesystem::path> paths { project.file, project.map, project.items };
		std::set<std::string> ids;
		for (auto &layer : project.layers) {
			if (!paths.insert(layer.file).second || !ids.insert(layer.id).second || (project.schemaVersion == 1 && layer.schemaVersion != 1)) {
				error = "Duplicate layer path/identity or incompatible catalog version";
				return false;
			}
			Layer parsed;
			if (!parseLayer(serializeLayer(layer), layer.file, parsed, diagnostics)) {
				errors(diagnostics, error);
				return false;
			}
			parsed.enabled = layer.enabled;
			layer = std::move(parsed);
		}
		if (!project.rebuildIndex(diagnostics)) {
			errors(diagnostics, error);
			return false;
		}
		return true;
	}
	void visitValue(Value &value, const Parameter &schema, const std::function<void(std::string &)> &visit) {
		if (schema.type == "objectRef") {
			if (auto record = std::get_if<Value::Record>(&value.data)) {
				const auto found = record->find("object");
				if (found != record->end()) {
					if (auto id = std::get_if<std::string>(&found->second.data)) {
						visit(*id);
					}
				}
			}
		} else if (schema.type == "list" && schema.element.size() == 1) {
			if (auto list = std::get_if<Value::List>(&value.data)) {
				for (auto &entry : *list) {
					visitValue(entry, schema.element.front(), visit);
				}
			}
		} else if (schema.type == "record") {
			if (auto record = std::get_if<Value::Record>(&value.data)) {
				for (auto &[name, entry] : *record) {
					const auto type = schema.fields.find(name);
					if (type != schema.fields.end()) {
						visitValue(entry, type->second, visit);
					}
				}
			}
		}
	}
	void references(Object &object, const Project &project, const std::function<void(std::string &)> &visit) {
		if (!object.container.empty()) {
			visit(object.container);
		}
		if (object.selector && !object.selector->container.empty()) {
			visit(object.selector->container);
		}
		if (object.teleport) {
			visit(object.teleport->destination);
		}
		for (auto &[name, values] : object.relations) {
			for (auto &value : values) {
				visit(value.object);
			}
		}
		for (auto &binding : object.behaviors) {
			for (auto &[name, values] : binding.relations) {
				for (auto &value : values) {
					visit(value.object);
				}
			}
			const auto descriptor = project.behavior(binding.id);
			if (!descriptor) {
				continue;
			}
			for (auto &[name, value] : binding.parameters) {
				const auto type = descriptor->parameters.find(name);
				if (type != descriptor->parameters.end()) {
					visitValue(value, type->second, visit);
				}
			}
		}
	}
}

size_t WorldDocumentChange::memorySize() const {
	size_t result = sizeof(*this);
	for (const auto &[file, change] : layers) {
		if (change.expected) {
			result += world_layers::serializeLayer(*change.expected).size();
		}
		if (change.replacement) {
			result += world_layers::serializeLayer(*change.replacement).size();
		}
	}
	if (catalog) {
		result += world_layers::serializeProject(catalog->expected).size() + world_layers::serializeProject(catalog->replacement).size();
	}
	for (const auto &[file, change] : migrations) {
		result += world_layers::serializeMigration(change.expected).size() + world_layers::serializeMigration(change.replacement).size();
	}
	return result;
}

bool WorldLayerDocument::open(const std::filesystem::path &file, std::string &error) {
	try {
		WorldLayerDocument loaded;
		world_layers::Diagnostics diagnostics;
		if (!world_layers::loadProject(std::filesystem::absolute(file).lexically_normal(), loaded.project, diagnostics, &loaded.source)) {
			errors(diagnostics, error);
			return false;
		}
		loaded.saved = documents(loaded.project);
		for (const auto &[path, content] : loaded.source) {
			loaded.fileRevisions[path] = generation + 1;
		}
		loaded.generation = generation + 1;
		loaded.selected = selected;
		loaded.visible = visible;
		*this = std::move(loaded);
		return true;
	} catch (const std::exception &exception) {
		error = exception.what();
		return false;
	}
}

bool WorldLayerDocument::create(const std::filesystem::path &file, const std::filesystem::path &map, const std::filesystem::path &items, const std::string &id, std::string &error) {
	if (std::filesystem::exists(file)) {
		error = "A catalog already exists at that path";
		return false;
	}
	WorldLayerDocument created;
	created.project.file = std::filesystem::absolute(file).lexically_normal();
	created.project.map = std::filesystem::absolute(map).lexically_normal();
	created.project.items = std::filesystem::absolute(items).lexically_normal();
	created.project.id = id;
	created.project.schemaVersion = 2;
	if (!structure(created.project, error)) {
		return false;
	}
	created.generation = generation + 1;
	created.unsaved = true;
	*this = std::move(created);
	return true;
}

bool WorldLayerDocument::observe(const std::filesystem::path &file, std::string &error) {
	std::string bytes;
	if (!world_layers::readFile(file, bytes, error)) {
		return false;
	}
	if (const auto before = source.find(file); before != source.end()) {
		if (before->second != bytes) {
			error = "File changed outside RME: " + file.generic_string();
			return false;
		}
		return true;
	}
	source[file] = std::move(bytes);
	fileRevisions[file] = generation + 1;
	return true;
}

std::vector<std::filesystem::path> WorldLayerDocument::observedFiles() const {
	std::set<std::filesystem::path> paths;
	for (const auto &[file, content] : source) {
		paths.insert(file);
	}
	for (const auto &[file, content] : documents(project)) {
		paths.insert(file);
	}
	for (const auto &descriptor : project.behaviors) {
		paths.insert(descriptor.file);
		paths.insert(descriptor.script);
	}
	return { paths.begin(), paths.end() };
}

bool WorldLayerDocument::externalChanges(std::vector<WorldExternalChange> &changes, std::string &error) const {
	std::vector<WorldExternalChange> found;
	const auto current = documents(project);
	for (const auto &file : observedFiles()) {
		world_files::Revision disk;
		if (!world_files::revision(file, disk, error)) {
			return false;
		}
		const auto original = source.find(file);
		const auto base = original == source.end() ? world_files::Revision() : world_files::Revision(original->second);
		if (disk == base) {
			continue;
		}
		const auto value = current.find(file);
		found.push_back({ file, base, value == current.end() ? base : world_files::Revision(value->second), disk });
	}
	changes = std::move(found);
	return true;
}

WorldExternalResult WorldLayerDocument::reconcileExternal(bool discardConflicts, std::vector<WorldExternalChange> &changes, std::string &error) {
	try {
		if (!externalChanges(changes, error)) {
			return WorldExternalResult::Invalid;
		}
		if (world_files::pending(project.file)) {
			error = "An interrupted World save requires recovery. Use Review external changes to finish or restore it.";
			return WorldExternalResult::Invalid;
		}
		if (changes.empty()) {
			return WorldExternalResult::Unchanged;
		}
		WorldLayerDocument disk;
		if (!disk.open(project.file, error)) {
			return WorldExternalResult::Invalid;
		}
		if (disk.project.map != project.map) {
			error = "The external catalog changed its base map. Reopen the corresponding OTBM to use it.";
			return WorldExternalResult::Invalid;
		}
		std::set<std::filesystem::path> changed;
		const auto current = documents(project);
		for (const auto &entry : changes) {
			changed.insert(entry.file);
		}
		for (const auto &[path, value] : disk.source) {
			const auto before = source.find(path);
			if (before == source.end() || before->second != value) {
				changed.insert(path);
			}
		}
		for (const auto &[path, value] : source) {
			if (!disk.source.contains(path) && !current.contains(path)) {
				changed.insert(path);
			}
		}
		const auto locallyChanged = [&](const std::filesystem::path &file) {
			const auto before = saved.find(file);
			const auto now = current.find(file);
			return (before == saved.end()) != (now == current.end()) || (before != saved.end() && now != current.end() && before->second != now->second);
		};
		std::set<std::filesystem::path> conflicts;
		for (const auto &file : changed) {
			const auto local = current.find(file);
			const auto external = disk.saved.find(file);
			const bool same = local != current.end() && external != disk.saved.end() && local->second == external->second;
			if (locallyChanged(file) && !same) {
				conflicts.insert(file);
			}
		}
		const bool catalogChanged = changed.contains(project.file);
		if (catalogChanged) {
			for (const auto &layer : project.layers) {
				if (!layerAt(disk.project, layer.file) && locallyChanged(layer.file)) {
					conflicts.insert(layer.file);
				}
			}
		}
		if (!conflicts.empty() && !discardConflicts) {
			error = "External and local changes overlap:\n";
			for (const auto &file : conflicts) {
				error += file.generic_string() + "\n";
			}
			return WorldExternalResult::Conflict;
		}
		auto merged = catalogChanged ? disk.project : project;
		std::vector<world_layers::Layer> layers;
		for (const auto &metadata : merged.layers) {
			const auto local = layerAt(project, metadata.file), external = layerAt(disk.project, metadata.file);
			const bool retain = local && locallyChanged(metadata.file) && !conflicts.contains(metadata.file);
			if (!retain && !external && local && source.contains(metadata.file)) {
				error = "A layer disappeared from the external catalog; reconcile the catalog first.";
				return WorldExternalResult::Conflict;
			}
			const auto chosen = retain ? local : external ? external
														  : local;
			if (!chosen) {
				error = "Missing layer during external reconciliation";
				return WorldExternalResult::Invalid;
			}
			layers.push_back(*chosen);
			layers.back().enabled = metadata.enabled;
		}
		merged.layers = std::move(layers);
		for (auto &descriptor : merged.behaviors) {
			const auto found = std::find_if(disk.project.behaviors.begin(), disk.project.behaviors.end(), [&](const auto &entry) { return entry.file == descriptor.file; });
			if (found != disk.project.behaviors.end()) {
				descriptor = *found;
			}
		}
		for (auto &record : merged.migrationRecords) {
			const auto local = std::find_if(project.migrationRecords.begin(), project.migrationRecords.end(), [&](const auto &entry) { return entry.file == record.file; });
			const auto found = std::find_if(disk.project.migrationRecords.begin(), disk.project.migrationRecords.end(), [&](const auto &entry) { return entry.file == record.file; });
			if (local != project.migrationRecords.end() && locallyChanged(record.file) && !conflicts.contains(record.file)) {
				record = *local;
			} else if (found != disk.project.migrationRecords.end()) {
				record = *found;
			}
		}
		if (!structure(merged, error)) {
			return WorldExternalResult::Invalid;
		}
		// Keep the exact parsed bytes as the save baseline, not a second read
		// that could acknowledge a newer version without loading its contents.
		for (const auto &[file, bytes] : source) {
			if (!disk.source.contains(file) && current.contains(file) && !changed.contains(file)) {
				disk.source.emplace(file, bytes);
			}
		}
		for (const auto &file : changed) {
			fileRevisions[file] = generation + 1;
		}
		project = std::move(merged);
		source = std::move(disk.source);
		saved = std::move(disk.saved);
		unsaved = documents(project) != saved;
		if (!project.find(selected)) {
			selected.clear();
		}
		++generation;
		return WorldExternalResult::Reloaded;
	} catch (const std::exception &exception) {
		error = exception.what();
		return WorldExternalResult::Invalid;
	}
}

bool WorldLayerDocument::saveDraft(const std::filesystem::path &directory, std::filesystem::path &catalog, std::string &error) const {
	try {
		auto copy = project;
		copy.file = std::filesystem::absolute(directory / "project.draft.json").lexically_normal();
		for (size_t i = 0; i < copy.layers.size(); ++i) {
			copy.layers[i].file = copy.file.parent_path() / ("layer-" + std::to_string(i) + ".draft.json");
		}
		for (size_t i = 0; i < copy.migrationRecords.size(); ++i) {
			const auto old = copy.migrationRecords[i].file;
			const auto file = copy.file.parent_path() / ("migration-" + std::to_string(i) + ".draft.json");
			for (auto &path : copy.migrations) {
				if (path == old) {
					path = file;
				}
			}
			copy.migrationRecords[i].file = file;
		}
		world_files::Publication publication { copy.file, copy.file.parent_path(), {}, {} };
		const auto files = documents(copy);
		for (const auto &[file, bytes] : files) {
			if (file != copy.file) {
				publication.changes.push_back({ file, {}, bytes });
			}
		}
		publication.changes.push_back({ copy.file, {}, files.at(copy.file) });
		if (!world_files::publish(publication, error)) {
			return false;
		}
		catalog = copy.file;
		return true;
	} catch (const std::exception &exception) {
		error = exception.what();
		return false;
	}
}

bool WorldLayerDocument::makeChange(const world_layers::Project &value, WorldDocumentChange &change, std::string &error) const {
	try {
		auto proposed = value;
		if (proposed.file != project.file || proposed.map != project.map || !structure(proposed, error)) {
			return false;
		}
		WorldDocumentChange result;
		result.selected = selected;
		if (world_layers::serializeProject(project) != world_layers::serializeProject(proposed)) {
			result.catalog = WorldDocumentChange::Catalog { header(project), header(proposed) };
		}
		std::set<std::filesystem::path> paths;
		for (const auto &layer : project.layers) {
			paths.insert(layer.file);
		}
		for (const auto &layer : proposed.layers) {
			paths.insert(layer.file);
		}
		for (const auto &path : paths) {
			const auto before = layerAt(project, path), after = layerAt(proposed, path);
			if (before && after && sameLayer(*before, *after)) {
				continue;
			}
			result.layers[path] = { before ? std::optional(*before) : std::nullopt, after ? std::optional(*after) : std::nullopt };
		}
		for (const auto &record : proposed.migrationRecords) {
			const auto before = std::find_if(project.migrationRecords.begin(), project.migrationRecords.end(), [&](const auto &entry) { return entry.file == record.file; });
			if (before == project.migrationRecords.end()) {
				error = "Migration records must be applied through the migration tool";
				return false;
			}
			if (world_layers::serializeMigration(*before) != world_layers::serializeMigration(record)) {
				result.migrations.emplace(record.file, WorldDocumentChange::Migration { *before, record });
			}
		}
		if (proposed.migrationRecords.size() != project.migrationRecords.size()) {
			error = "Revert migration ownership through the migration tool";
			return false;
		}
		if (!result.catalog && result.layers.empty() && result.migrations.empty()) {
			return false;
		}
		const auto guard = [&](const std::filesystem::path &path) { const auto revision = fileRevisions.find(path); result.revisions[path] = revision == fileRevisions.end() ? 0 : revision->second; };
		guard(project.file);
		for (const auto &[path, delta] : result.layers) {
			guard(path);
		}
		for (const auto &[path, delta] : result.migrations) {
			guard(path);
		}
		for (const auto &descriptor : project.behaviors) {
			guard(descriptor.file);
		}
		change = std::move(result);
		return true;
	} catch (const std::exception &exception) {
		error = exception.what();
		return false;
	}
}

bool WorldLayerDocument::canExchange(const WorldDocumentChange &change, std::string &error) const {
	for (const auto &[path, expected] : change.revisions) {
		const auto found = fileRevisions.find(path);
		if ((found == fileRevisions.end() ? 0 : found->second) != expected) {
			error = "This World action belongs to a document revision replaced outside the editor: " + path.generic_string();
			return false;
		}
	}
	if (change.catalog && world_layers::serializeProject(project) != world_layers::serializeProject(change.catalog->expected)) {
		error = "The catalog changed since this action was recorded";
		return false;
	}
	for (const auto &[path, delta] : change.layers) {
		const auto current = layerAt(project, path);
		if (bool(current) != delta.expected.has_value() || (current && !sameLayer(*current, *delta.expected))) {
			error = "Layer no longer matches this action: " + path.generic_string();
			return false;
		}
	}
	for (const auto &[path, delta] : change.migrations) {
		const auto record = std::find_if(project.migrationRecords.begin(), project.migrationRecords.end(), [&](const auto &entry) { return entry.file == path; });
		if (record == project.migrationRecords.end() || world_layers::serializeMigration(*record) != world_layers::serializeMigration(delta.expected)) {
			error = "Migration ownership no longer matches this action";
			return false;
		}
	}
	return true;
}

bool WorldLayerDocument::exchange(WorldDocumentChange &change, std::string &error) {
	if (!canExchange(change, error)) {
		return false;
	}
	auto next = project;
	for (const auto &[path, delta] : change.migrations) {
		const auto record = std::find_if(next.migrationRecords.begin(), next.migrationRecords.end(), [&](const auto &entry) { return entry.file == path; });
		if (record == next.migrationRecords.end() || world_layers::serializeMigration(*record) != world_layers::serializeMigration(delta.expected)) {
			error = "Migration ownership no longer matches this action";
			return false;
		}
		*record = delta.replacement;
	}
	for (const auto &[path, delta] : change.layers) {
		std::erase_if(next.layers, [&](const auto &layer) { return layer.file == path; });
		if (delta.replacement) {
			next.layers.push_back(*delta.replacement);
		}
	}
	const auto &layout = change.catalog ? change.catalog->replacement : project;
	std::vector<world_layers::Layer> ordered;
	for (const auto &metadata : layout.layers) {
		const auto layer = layerAt(next, metadata.file);
		if (!layer) {
			error = "Incomplete catalog action";
			return false;
		}
		ordered.push_back(*layer);
		ordered.back().enabled = metadata.enabled;
	}
	if (change.catalog) {
		next.schemaVersion = layout.schemaVersion;
		next.schema = layout.schema;
		next.id = layout.id;
		next.items = layout.items;
		next.behaviors = layout.behaviors;
		next.migrations = layout.migrations;
	}
	next.layers = std::move(ordered);
	if (!structure(next, error)) {
		return false;
	}
	project = std::move(next);
	unsaved = documents(project) != saved;
	for (auto &[file, delta] : change.layers) {
		std::swap(delta.expected, delta.replacement);
	}
	for (auto &[file, delta] : change.migrations) {
		std::swap(delta.expected, delta.replacement);
	}
	if (change.catalog) {
		std::swap(change.catalog->expected, change.catalog->replacement);
	}
	std::swap(selected, change.selected);
	if (!project.find(selected)) {
		selected.clear();
	}
	++generation;
	return true;
}

bool WorldLayerDocument::editProject(const world_layers::Project &value, std::string &error) {
	WorldDocumentChange change;
	if (!makeChange(value, change, error) || !exchange(change, error)) {
		return false;
	}
	history.resize(cursor);
	history.push_back(std::move(change));
	++cursor;
	return true;
}

bool WorldLayerDocument::edit(const std::string &id, const world_layers::Object &value) {
	auto next = project;
	const auto object = next.find(id);
	if (!object || object->id != value.id || !world_layers::isValidPosition(value.position)) {
		return false;
	}
	*object = value;
	std::string error;
	return editProject(next, error);
}

bool WorldLayerDocument::exchange(const std::string &id, world_layers::Object &value) {
	const auto current = project.find(id);
	if (!current || current->id != value.id || !world_layers::isValidPosition(value.position)) {
		return false;
	}
	const auto before = *current;
	auto next = project;
	*next.find(id) = value;
	WorldDocumentChange change;
	std::string error;
	if (!makeChange(next, change, error) || !exchange(change, error)) {
		return false;
	}
	value = before;
	return true;
}

bool WorldLayerDocument::undo() {
	std::string error;
	if (!canUndo() || !exchange(history[cursor - 1], error)) {
		return false;
	}
	--cursor;
	return true;
}
bool WorldLayerDocument::redo() {
	std::string error;
	if (!canRedo() || !exchange(history[cursor], error)) {
		return false;
	}
	++cursor;
	return true;
}
bool WorldLayerDocument::dirty() const {
	return unsaved;
}

bool WorldLayerDocument::save(std::string &error) {
	try {
		world_layers::Diagnostics diagnostics;
		world_layers::validateProject(project, diagnostics);
		if (!diagnostics.empty()) {
			errors(diagnostics, error);
			return false;
		}
		const auto pending = documents(project);
		world_files::Publication publication;
		publication.catalog = project.file;
		publication.root = project.file.parent_path();
		for (const auto &[file, content] : source) {
			publication.guards.emplace(file, content);
		}
		const auto add = [&](const std::filesystem::path &file) {
			const auto &content = pending.at(file);
			if (const auto before = saved.find(file); before != saved.end() && before->second == content) {
				return;
			}
			const auto original = source.find(file);
			if (original != source.end() && !saved.contains(file)) {
				world_layers::Layer imported;
				world_layers::Diagnostics ignored;
				if (world_layers::parseLayer(original->second, file, imported, ignored)
				    && world_layers::serializeLayer(imported) == content) {
					return;
				}
			}
			publication.changes.push_back({ file, original == source.end() ? world_files::Revision() : world_files::Revision(original->second), content });
		};
		for (const auto &[file, content] : pending) {
			if (file != project.file) {
				add(file);
			}
		}
		add(project.file);
		if (!world_files::publish(publication, error)) {
			return false;
		}
		for (const auto &change : publication.changes) {
			if (change.after) {
				source[change.file] = *change.after;
			}
		}
		saved = pending;
		unsaved = false;
		return true;
	} catch (const std::exception &exception) {
		error = exception.what();
		return false;
	}
}

bool WorldLayerDocument::matchesMap(const std::filesystem::path &file) const {
	std::error_code error;
	return std::filesystem::equivalent(project.map, file, error);
}

std::vector<std::string> WorldLayerDocument::dependents(const world_layers::Project &value, const std::string &id) {
	std::set<std::string> found;
	for (const auto &layer : value.layers) {
		for (const auto &entry : layer.objects) {
			auto object = entry;
			references(object, value, [&](std::string &target) { if (target == id){ found.insert(world_layers::objectId(layer, entry));
} });
		}
	}
	for (const auto &record : value.migrationRecords) {
		for (const auto &claim : record.claims) {
			if (claim.object == id) {
				found.insert("Migration ownership: " + record.id);
			}
		}
	}
	return { found.begin(), found.end() };
}

bool WorldLayerDocument::renameObject(world_layers::Project &value, const std::string &id, const std::string &replacement, std::string &error) {
	if (value.schemaVersion != 2) {
		error = "Convert this project to v2 before changing identities";
		return false;
	}
	if (!value.find(id) || value.find(replacement)) {
		error = "Missing original or duplicate new identity";
		return false;
	}
	auto next = value;
	next.find(id)->id = replacement;
	for (auto &layer : next.layers) {
		for (auto &object : layer.objects) {
			references(object, next, [&](std::string &target) { if (target == id){ target = replacement;
} });
		}
	}
	for (auto &record : next.migrationRecords) {
		for (auto &claim : record.claims) {
			if (claim.object == id) {
				claim.object = replacement;
			}
		}
	}
	if (!structure(next, error)) {
		return false;
	}
	value = std::move(next);
	return true;
}

bool WorldLayerDocument::removeObject(world_layers::Project &value, const std::string &id, std::string &error) {
	if (!value.find(id)) {
		error = "Object no longer exists";
		return false;
	}
	const auto dependencies = dependents(value, id);
	if (!dependencies.empty()) {
		error = "Remove or reassign these dependencies first:\n";
		for (const auto &entry : dependencies) {
			error += entry + "\n";
		}
		return false;
	}
	auto next = value;
	for (auto &layer : next.layers) {
		std::erase_if(layer.objects, [&](const auto &object) { return world_layers::objectId(layer, object) == id; });
	}
	if (!structure(next, error)) {
		return false;
	}
	value = std::move(next);
	return true;
}

bool WorldLayerDocument::moveToLayer(world_layers::Project &value, const std::string &id, const std::filesystem::path &layer, std::string &error) {
	if (value.schemaVersion != 2) {
		error = "Convert this project to v2 before moving declarations between layers";
		return false;
	}
	const auto source = value.objects.find(id);
	if (source == value.objects.end() || !layerAt(value, layer)) {
		error = "Missing object or destination layer";
		return false;
	}
	auto next = value;
	auto object = *value.find(id);
	auto &original = next.layers[source->second.first].objects;
	original.erase(original.begin() + source->second.second);
	for (auto &entry : next.layers) {
		if (entry.file == layer) {
			entry.objects.push_back(std::move(object));
		}
	}
	if (!structure(next, error)) {
		return false;
	}
	value = std::move(next);
	return true;
}

namespace {
	std::filesystem::path copiedCatalog(std::filesystem::path map) {
		return map.replace_extension("world.json");
	}
	std::filesystem::path copiedLayers(std::filesystem::path map) {
		return map.replace_extension("world-layers");
	}
}
bool WorldLayerDocument::canCopyForMap(const std::filesystem::path &map, std::string &error) const {
	try {
		if (map.extension() != ".otbm") {
			error = "Maps with world catalogs must be saved as .otbm";
			return false;
		}
		if (std::filesystem::exists(copiedCatalog(map)) || std::filesystem::exists(copiedLayers(map))) {
			error = "A world catalog or layer directory already exists for that name";
			return false;
		}
		if (project.items.lexically_relative(map.parent_path()).empty()) {
			error = "The item catalog must remain reachable through a relative path";
			return false;
		}
		return true;
	} catch (const std::exception &exception) {
		error = exception.what();
		return false;
	}
}
bool WorldLayerDocument::copyForMap(const std::filesystem::path &map, std::string &error) {
	if (!canCopyForMap(map, error)) {
		return false;
	}
	try {
		WorldLayerDocument copy;
		copy.project = project;
		copy.project.file = copiedCatalog(std::filesystem::absolute(map).lexically_normal());
		copy.project.map = std::filesystem::absolute(map).lexically_normal();
		std::map<std::filesystem::path, std::filesystem::path> renamed;
		for (auto &layer : copy.project.layers) {
			const auto file = copiedLayers(copy.project.map) / (layer.id + ".layer.json");
			renamed[layer.file] = file;
			layer.file = file;
		}
		for (size_t i = 0; i < copy.project.migrationRecords.size(); ++i) {
			auto &record = copy.project.migrationRecords[i];
			const auto file = copiedLayers(copy.project.map) / ("migration-" + std::to_string(i) + ".json");
			renamed[record.file] = file;
			for (auto &path : copy.project.migrations) {
				if (path == record.file) {
					path = file;
				}
			}
			record.file = file;
		}
		if (!copy.save(error)) {
			return false;
		}
		// Existing object actions remain usable; document actions whose file paths
		// changed are intentionally rejected until they are rebased by the editor.
		copy.selected = selected;
		copy.visible = visible;
		copy.generation = generation + 1;
		*this = std::move(copy);
		return true;
	} catch (const std::exception &exception) {
		error = exception.what();
		return false;
	}
}
