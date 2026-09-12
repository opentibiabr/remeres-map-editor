#include "main.h"

#include "world/world_editor.h"
#include "application.h"
#include "complexitem.h"
#include "editor.h"
#include "gui.h"
#include "map_tab.h"
#include "map_display.h"
#include "palette_window.h"
#include "properties_window.h"
#include "raw_brush.h"
#include <wx/choicdlg.h>
#include <wx/numdlg.h>
#include <wx/fswatcher.h>
#include <wx/listctrl.h>
#include <wx/notebook.h>
#include <wx/dirdlg.h>

namespace {
	Position native(const world_layers::Position &p) {
		return Position(p.x, p.y, p.z);
	}
	world_layers::Position portable(const Position &p) {
		return { p.x, p.y, p.z };
	}
	world_layers::MapItem snapshot(Item* item, bool ground = false) {
		using world_layers::Value;
		world_layers::MapItem result;
		result.key = reinterpret_cast<uintptr_t>(item);
		result.itemId = item->getID();
		result.uid = item->getUniqueID();
		result.aid = item->getActionID();
		result.ground = ground;
		Value::Record custom;
		for (const auto &[name, attribute] : item->getAttributes()) {
			if (name == "aid" || name == "uid" || name.starts_with("__world.")) {
				continue;
			}
			Value value;
			if (const auto text = attribute.getString()) {
				value.data = *text;
			} else if (const auto number = attribute.getInteger()) {
				value.data = int64_t(*number);
			} else if (const auto number = attribute.getFloat()) {
				value.data = *number;
			} else if (const auto boolean = attribute.getBoolean()) {
				value.data = *boolean;
			} else {
				continue;
			}
			if (name == "desc") {
				result.attributes["description"] = value;
			} else if (name == "text" || name == "name" || name == "article" || name == "plural" || name == "writer" || name == "date") {
				result.attributes[name] = value;
			} else {
				custom[name] = value;
			}
		}
		if (!custom.empty()) {
			result.attributes["custom"] = Value { std::move(custom) };
		}
		if (const auto teleport = item->getTeleport()) {
			result.teleport = true;
			result.destination = portable(teleport->getDestination());
		}
		if (const auto container = item->getContainer()) {
			result.container = true;
			for (auto child : container->getVector()) {
				result.children.push_back(snapshot(child));
			}
		}
		return result;
	}

	class EditorMapView final : public world_layers::MapView {
	public:
		EditorMapView(Map &map, const std::bitset<65536> &knownItems) :
			map(map), knownItems(knownItems) { }
		bool nativeTeleport(uint16_t id) const override {
			return g_items.isValidID(id) && g_items.getItemType(id).isTeleport();
		}
		bool knownItem(uint16_t id) const override {
			return knownItems.test(id) || g_items.isValidID(id);
		}
		bool capability(uint16_t id, const std::string &name) const override {
			if (!knownItem(id)) {
				return false;
			}
			if (!g_items.isValidID(id)) {
				return MapView::capability(id, name);
			}
			const auto &type = g_items.getItemType(id);
			if (name == "container") {
				return type.isContainer();
			}
			if (name == "door") {
				return type.isDoor();
			}
			if (name == "ground") {
				return type.isGroundTile();
			}
			if (name == "movable") {
				return type.moveable;
			}
			if (name == "stackable") {
				return type.stackable;
			}
			if (name == "readable") {
				return type.canReadText || type.canWriteText;
			}
			if (name == "blocking") {
				return type.unpassable;
			}
			return MapView::capability(id, name);
		}
		world_layers::MapTile tile(const world_layers::Position &position) override {
			world_layers::MapTile result;
			const auto tile = map.getTile(native(position));
			if (!tile) {
				return result;
			}
			result.exists = true;
			result.ground = tile->ground != nullptr;
			result.house = tile->isHouseTile();
			const auto append = [&](Item* item) {
				if (!item) {
					return;
				}
				result.items.push_back(snapshot(item, tile->ground == item));
				result.blocked = result.blocked || item->isBlocking();
			};
			append(tile->ground);
			for (auto item : tile->items) {
				append(item);
			}
			return result;
		}
		std::vector<world_layers::UniqueOccurrence> uniqueIds(const std::unordered_set<uint16_t> &requested) override {
			std::vector<world_layers::UniqueOccurrence> result;
			for (const auto &entry : map.uniqueItems()) {
				if (requested.empty() || requested.contains(entry.uid)) {
					result.push_back({ entry.uid, reinterpret_cast<uintptr_t>(entry.item), portable(entry.position) });
				}
			}
			return result;
		}

	private:
		Map &map;
		const std::bitset<65536> &knownItems;
	};

	WorldLayerEditor* current() {
		const auto editor = g_gui.GetCurrentEditor();
		return editor ? editor->world.get() : nullptr;
	}
}

class WorldFileMonitor final : public wxEvtHandler {
public:
	explicit WorldFileMonitor(WorldLayerEditor &editor) :
		editor(editor), timer(this) {
		Bind(wxEVT_TIMER, &WorldFileMonitor::tick, this);
#if wxUSE_FSWATCHER
		watcher.SetOwner(this);
		Bind(wxEVT_FSWATCHER, &WorldFileMonitor::changed, this);
#endif
		timer.Start(500);
	}
	~WorldFileMonitor() override {
		timer.Stop();
#if wxUSE_FSWATCHER
		watcher.RemoveAll();
		watcher.SetOwner(nullptr);
		Unbind(wxEVT_FSWATCHER, &WorldFileMonitor::changed, this);
#endif
		Unbind(wxEVT_TIMER, &WorldFileMonitor::tick, this);
		DeletePendingEvents();
	}
	WorldFileMonitor(const WorldFileMonitor &) = delete;
	WorldFileMonitor &operator=(const WorldFileMonitor &) = delete;

private:
	WorldLayerEditor &editor;
	wxTimer timer;
#if wxUSE_FSWATCHER
	wxFileSystemWatcher watcher;
	void changed(wxFileSystemWatcherEvent &) {
		eventTime = wxGetUTCTimeMillis();
		needsCheck = true;
	}
#endif
	std::set<std::filesystem::path> directories;
	uint64_t revision = UINT64_MAX;
	wxLongLong eventTime = 0, checkedAt = 0;
	bool needsCheck = true, active = false;
	void tick(wxTimerEvent &) {
		// A properties dialog owns an editable draft and synchronous base-item
		// pointers. Publish external revisions only after modal editing finishes.
		for (auto node = wxTopLevelWindows.GetFirst(); node; node = node->GetNext()) {
			const auto dialog = dynamic_cast<wxDialog*>(node->GetData());
			if (dialog && dialog->IsModal()) {
				return;
			}
		}
		if (editor.drag) {
			return;
		}
		const bool foreground = wxTheApp && wxTheApp->IsActive();
		if (foreground && !active) {
			needsCheck = true;
		}
		active = foreground;
		const auto now = wxGetUTCTimeMillis();
		if (revision != editor.document.revision()) {
			revision = editor.document.revision();
			std::set<std::filesystem::path> next;
			for (const auto &file : editor.document.observedFiles()) {
				auto directory = file.parent_path();
				std::error_code error;
				while (!directory.empty() && !std::filesystem::exists(directory, error)) {
					const auto parent = directory.parent_path();
					if (parent == directory) {
						directory.clear();
						break;
					}
					directory = parent;
				}
				if (!directory.empty()) {
					next.insert(directory);
				}
			}
#if wxUSE_FSWATCHER
			if (next != directories) {
				watcher.RemoveAll();
				for (const auto &directory : next) {
					watcher.Add(wxFileName::DirName(wxstr(directory.generic_string())), wxFSW_EVENT_CREATE | wxFSW_EVENT_DELETE | wxFSW_EVENT_RENAME | wxFSW_EVENT_MODIFY);
				}
			}
#endif
			directories = std::move(next);
		}
		if (((needsCheck && now - eventTime >= 400) || now - checkedAt >= 5000)) {
			needsCheck = false;
			checkedAt = now;
			editor.checkExternal(false);
		}
	}
};

WorldLayerEditor::WorldLayerEditor(Editor &owner, WorldLayerDocument data) :
	document(std::move(data)), editor(owner) {
	pugi::xml_document catalog;
	const auto loaded = catalog.load_file(document.data().items.c_str());
	if (!loaded || !catalog.child("items")) {
		catalogDiagnostics.push_back({ document.data().items, "", "", "Cannot read the project's item catalog" });
	} else {
		for (const auto item : catalog.child("items").children("item")) {
			const auto first = item.attribute("id") ? item.attribute("id").as_uint() : item.attribute("fromid").as_uint();
			const auto last = item.attribute("id") ? first : item.attribute("toid").as_uint();
			if (!first || first > last || last >= knownItems.size()) {
				catalogDiagnostics.push_back({ document.data().items, "", "", "Invalid item ID range in the project's item catalog" });
				knownItems.reset();
				break;
			}
			for (auto id = first; id <= last; ++id) {
				knownItems.set(id);
			}
		}
	}
	acknowledgeMapChange();
	validate();
	fileMonitor = std::make_unique<WorldFileMonitor>(*this);
}

WorldLayerEditor::~WorldLayerEditor() = default;

bool WorldLayerEditor::ensureV2() {
	if (document.data().schemaVersion == 2) {
		return true;
	}
	if (wxMessageBox("These authoring tools use World v2. Convert this project's declarations in the editor? The conversion is undoable and is written only when you save.", "Convert World project", wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION, g_gui.root) != wxYES) {
		return false;
	}
	auto project = document.data();
	world_layers::Diagnostics diagnostics;
	if (!world_layers::convertToV2(project, diagnostics)) {
		g_gui.PopupDialog("Cannot convert World", wxstr(diagnostics.front().describe()), wxOK);
		return false;
	}
	return editProject(project, document.selected);
}

std::optional<std::filesystem::path> WorldLayerEditor::chooseLayer() {
	if (document.data().layers.empty()) {
		manageLayers();
	}
	if (document.data().layers.empty()) {
		return std::nullopt;
	}
	wxArrayString labels;
	for (const auto &layer : document.data().layers) {
		labels.Add(wxstr(layer.id + (layer.enabled ? "" : " (disabled)")));
	}
	wxSingleChoiceDialog dialog(g_gui.root, "Save this declaration in", "World layer", labels);
	if (dialog.ShowModal() != wxID_OK) {
		return std::nullopt;
	}
	return document.data().layers[dialog.GetSelection()].file;
}

void WorldLayerEditor::manageLayers() {
	if (!ensureV2()) {
		return;
	}
	wxArrayString operations;
	for (const auto* label : { "Create layer", "Include existing layer", "Rename layer", "Enable / disable layer", "Remove layer from catalog" }) {
		operations.Add(label);
	}
	wxSingleChoiceDialog operation(g_gui.root, "Layer operation", "World layers", operations);
	if (operation.ShowModal() != wxID_OK) {
		return;
	}
	auto next = document.data();
	std::string error;
	const auto selected = operation.GetSelection();
	if (selected == 0) {
		const auto id = nstr(wxGetTextFromUser("Stable layer identity", "New World layer", "new_layer", g_gui.root));
		if (id.empty()) {
			return;
		}
		world_layers::Layer layer;
		layer.id = id;
		layer.name = id;
		layer.schemaVersion = 2;
		layer.file = next.file.parent_path() / "layers" / (id + ".layer.json");
		if (std::filesystem::exists(layer.file)) {
			g_gui.PopupDialog("Layer exists", "Use Include existing layer to retain that file.", wxOK);
			return;
		}
		next.layers.push_back(std::move(layer));
	} else if (selected == 1) {
		wxFileDialog dialog(g_gui.root, "Include layer", wxstr(next.file.parent_path().generic_string()), "", "World layer (*.layer.json)|*.layer.json", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
		if (dialog.ShowModal() != wxID_OK) {
			return;
		}
		const auto file = std::filesystem::absolute(std::filesystem::u8path(nstr(dialog.GetPath()))).lexically_normal();
		std::string content;
		world_layers::Layer layer;
		world_layers::Diagnostics diagnostics;
		if (!world_layers::readFile(file, content, error) || !world_layers::parseLayer(content, file, layer, diagnostics) || !document.observe(file, error)) {
			g_gui.PopupDialog("Cannot include layer", wxstr(error.empty() ? diagnostics.front().describe() : error), wxOK);
			return;
		}
		if (layer.schemaVersion != 2) {
			g_gui.PopupDialog("Layer version", "Convert its v1 project explicitly before including this layer in a v2 catalog.", wxOK);
			return;
		}
		next.layers.push_back(std::move(layer));
	} else {
		if (next.layers.empty()) {
			return;
		}
		wxArrayString names;
		for (const auto &layer : next.layers) {
			names.Add(wxstr(layer.id));
		}
		wxSingleChoiceDialog dialog(g_gui.root, "Layer", "World layers", names);
		if (dialog.ShowModal() != wxID_OK) {
			return;
		}
		const auto index = dialog.GetSelection();
		auto &layer = next.layers[index];
		if (selected == 2) {
			const auto id = nstr(wxGetTextFromUser("New layer identity (object identities stay unchanged)", "Rename layer", wxstr(layer.id), g_gui.root));
			if (id.empty()) {
				return;
			}
			layer.id = id;
		} else if (selected == 3) {
			layer.enabled = !layer.enabled;
		} else {
			std::set<std::string> removed;
			for (const auto &object : layer.objects) {
				removed.insert(world_layers::objectId(layer, object));
			}
			for (const auto &id : removed) {
				for (const auto &dependent : WorldLayerDocument::dependents(next, id)) {
					if (!removed.contains(dependent)) {
						error += dependent + "\n";
					}
				}
			}
			if (!error.empty()) {
				g_gui.PopupDialog("Layer dependencies", wxstr("Remove or reassign these dependencies first:\n" + error), wxOK);
				return;
			}
			next.layers.erase(next.layers.begin() + index);
		}
	}
	editProject(next, document.selected);
}

void WorldLayerEditor::manageDescriptors() {
	if (!ensureV2()) {
		return;
	}
	wxFileDialog dialog(g_gui.root, "Include behavior descriptor", wxstr(document.data().file.parent_path().generic_string()), "", "Behavior descriptor (*.behavior.json)|*.behavior.json", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
	if (dialog.ShowModal() != wxID_OK) {
		return;
	}
	const auto file = std::filesystem::absolute(std::filesystem::u8path(nstr(dialog.GetPath()))).lexically_normal();
	std::string content, error;
	world_layers::Diagnostics diagnostics;
	world_layers::BehaviorDescriptor descriptor;
	if (!world_layers::readFile(file, content, error) || !world_layers::parseBehavior(content, file, descriptor, diagnostics) || !document.observe(file, error) || !document.observe(descriptor.script, error)) {
		g_gui.PopupDialog("Cannot include behavior", wxstr(error.empty() ? diagnostics.front().describe() : error), wxOK);
		return;
	}
	if (document.data().behavior(descriptor.id)) {
		g_gui.PopupDialog("Behavior exists", "That behavior identity is already in this catalog.", wxOK);
		return;
	}
	auto next = document.data();
	next.behaviors.push_back(std::move(descriptor));
	editProject(next, document.selected);
}

void WorldLayerEditor::createObject(world_layers::ObjectKind kind, world_layers::SourceMode mode) {
	if (!ensureV2()) {
		return;
	}
	using namespace world_layers;
	Object object;
	object.kind = kind;
	object.mode = mode;
	MapItem original;
	if (mode != SourceMode::Create) {
		std::vector<std::pair<Tile*, Item*>> selected;
		wxArrayString labels;
		for (const auto tile : editor.getSelection()) {
			for (auto item : tile->getSelectedItems()) {
				selected.emplace_back(tile, item);
				labels.Add(wxString::Format("Item %u at %d, %d, %d", item->getID(), tile->getPosition().x, tile->getPosition().y, tile->getPosition().z));
			}
		}
		if (selected.empty()) {
			g_gui.PopupDialog("Select a base item", "Select the original item on the normal map first, then choose Configure base item or Replace base item.", wxOK);
			return;
		}
		size_t index = 0;
		if (selected.size() > 1) {
			wxSingleChoiceDialog choose(g_gui.root, "Choose the exact original", "World base item", labels);
			if (choose.ShowModal() != wxID_OK) {
				return;
			}
			index = choose.GetSelection();
		}
		const auto [tile, item] = selected[index];
		original = snapshot(item, tile->ground == item);
		object.position = portable(tile->getPosition());
		object.itemId = item->getID();
		object.selector = Selector {};
		object.selector->position = object.position;
		object.selector->itemId = object.itemId;
		object.selector->ground = tile->ground == item;
		std::vector<MapItem> candidates;
		if (tile->ground) {
			candidates.push_back(snapshot(tile->ground, true));
		}
		for (auto entry : tile->items) {
			candidates.push_back(snapshot(entry));
		}
		std::string error;
		if (!captureSelector(*object.selector, candidates, original.key, error)) {
			g_gui.PopupDialog("Cannot select original", wxstr(error), wxOK);
			return;
		}
		object.lifecycle = mode == SourceMode::Map ? Lifecycle::Native : Lifecycle::Fixture;
	} else if (const auto tab = g_gui.GetCurrentMapTab()) {
		object.position = portable(tab->GetScreenCenterPosition());
	}
	if (kind == ObjectKind::Item && mode != SourceMode::Map) {
		const auto brush = g_gui.GetCurrentBrush();
		const auto initial = brush && brush->isRaw() ? brush->asRaw()->getItemID() : object.itemId ? object.itemId
																								   : 1949;
		const auto id = wxGetNumberFromUser("Choose the external item", "Item ID", "World item", initial, 1, 65535, g_gui.root);
		if (id < 1) {
			return;
		}
		object.itemId = static_cast<uint16_t>(id);
	}
	const auto file = chooseLayer();
	if (!file) {
		return;
	}
	auto next = document.data();
	const auto layer = std::find_if(next.layers.begin(), next.layers.end(), [&](const auto &entry) { return entry.file == *file; });
	object.id = nstr(wxGetTextFromUser("Stable object identity", "New World object", wxstr(layer->id + (kind == ObjectKind::Anchor ? ".point" : ".item_" + std::to_string(object.itemId))), g_gui.root));
	if (object.id.empty()) {
		return;
	}
	if (next.find(object.id)) {
		g_gui.PopupDialog("Duplicate identity", "Choose another object identity.", wxOK);
		return;
	}
	layer->objects.push_back(object);
	Diagnostics diagnostics;
	if (!next.rebuildIndex(diagnostics)) {
		g_gui.PopupDialog("World identity", wxstr(diagnostics.front().describe()), wxOK);
		return;
	}
	std::unique_ptr<Item> preview(kind == ObjectKind::Item ? Item::Create(object.itemId) : nullptr);
	PropertiesWindow dialog(g_gui.root, &editor.getMap(), editor.getMap().getTile(native(object.position)), preview.get(), wxDefaultPosition, &object, &next, object.id, &next, &original);
	if (dialog.ShowModal() != 1) {
		return;
	}
	*next.find(object.id) = object;
	editProject(next, object.id);
}

void WorldLayerEditor::renameSelected() {
	if (!ensureV2() || document.selected.empty()) {
		return;
	}
	const auto id = nstr(wxGetTextFromUser("New identity. All references will be updated together.", "Rename World object", wxstr(document.selected), g_gui.root));
	if (id.empty() || id == document.selected) {
		return;
	}
	auto next = document.data();
	std::string error;
	if (!WorldLayerDocument::renameObject(next, document.selected, id, error)) {
		g_gui.PopupDialog("Cannot rename World object", wxstr(error), wxOK);
		return;
	}
	editProject(next, id);
}

void WorldLayerEditor::moveSelectedToLayer() {
	if (!ensureV2() || document.selected.empty()) {
		return;
	}
	const auto layer = chooseLayer();
	if (!layer) {
		return;
	}
	auto next = document.data();
	std::string error;
	if (!WorldLayerDocument::moveToLayer(next, document.selected, *layer, error)) {
		g_gui.PopupDialog("Cannot move declaration", wxstr(error), wxOK);
		return;
	}
	editProject(next, document.selected);
}

bool WorldLayerEditor::isPicking() const {
	return mapPick.has_value();
}

void WorldLayerEditor::cancelPick() {
	mapPick.reset();
	g_gui.SetStatusText("");
}

void WorldLayerEditor::pickOriginal() {
	if (!ensureV2()) {
		return;
	}
	const auto object = document.data().find(document.selected);
	if (!object || !object->selector) {
		g_gui.PopupDialog("Choose a World declaration", "Select a declaration bound to a base item or an external replacement first.", wxOK);
		return;
	}
	mapPick = MapPick { PickKind::Original, document.selected, {}, 0, document.revision() };
	g_gui.SetSelectionMode();
	g_gui.SetStatusText("Click the tile containing the new original. Choose its exact item; Escape cancels.");
}

void WorldLayerEditor::pickRelation() {
	if (!ensureV2()) {
		return;
	}
	const auto object = document.data().find(document.selected);
	if (!object) {
		g_gui.PopupDialog("Choose a World declaration", "Select the source object in the Worlds palette or on the map first.", wxOK);
		return;
	}
	std::vector<MapPick> choices;
	wxArrayString labels;
	const auto add = [&](PickKind kind, const std::string &name, size_t behavior, const std::string &label) {
		choices.push_back({ kind, document.selected, name, behavior, document.revision() });
		labels.Add(wxstr(label));
	};
	if (object->kind == world_layers::ObjectKind::Item && g_items.getItemType(object->itemId).isTeleport()) {
		add(PickKind::Teleport, {}, 0, "Teleport destination");
	}
	for (size_t i = 0; i < object->behaviors.size(); ++i) {
		const auto &binding = object->behaviors[i];
		if (const auto descriptor = document.data().behavior(binding.id)) {
			for (const auto &[name, relation] : descriptor->relations) {
				add(PickKind::BehaviorRelation, name, i, binding.id + ": " + (relation.label.empty() ? name : relation.label));
			}
		}
	}
	for (const auto &[name, values] : object->relations) {
		add(PickKind::ObjectRelation, name, 0, "Relation: " + name);
	}
	add(PickKind::ObjectRelation, {}, 0, "New named relation...");
	wxSingleChoiceDialog choose(g_gui.root, "Choose which relationship to set, then click its target on the normal map.", "Choose relation on map", labels);
	if (choose.ShowModal() != wxID_OK) {
		return;
	}
	auto request = choices.at(choose.GetSelection());
	if (request.kind == PickKind::ObjectRelation && request.relation.empty()) {
		request.relation = nstr(wxGetTextFromUser("Relation name", "New relation", "", g_gui.root));
		if (request.relation.empty()) {
			return;
		}
	}
	mapPick = std::move(request);
	document.visible = true;
	g_gui.SetSelectionMode();
	g_gui.SetStatusText("Click the related World object. Multiple objects on one tile will be listed. Escape cancels.");
}

bool WorldLayerEditor::pickAt(const world_layers::Position &position) {
	if (!mapPick) {
		return false;
	}
	const auto request = *mapPick;
	if (document.revision() != request.revision || !document.data().find(request.source)) {
		cancelPick();
		g_gui.PopupDialog("World changed", "The source document changed while choosing a target. Start the operation again with the current revision.", wxOK);
		return true;
	}
	synchronizeMap();
	auto next = document.data();
	auto object = next.find(request.source);
	if (request.kind == PickKind::Original) {
		const auto tile = editor.getMap().getTile(native(position));
		if (!tile) {
			g_gui.SetStatusText("This tile has no base items. Choose another tile or press Escape.");
			return true;
		}
		struct Candidate {
			world_layers::MapItem item;
			world_layers::Selector selector;
			std::vector<world_layers::MapItem> peers;
		};
		std::vector<Candidate> candidates;
		wxArrayString labels;
		EditorMapView view(editor.getMap(), knownItems);
		const auto collect = [&](const auto &self, const std::vector<world_layers::MapItem> &items, const std::string &container) -> void {
			for (const auto &item : items) {
				world_layers::Selector selector;
				selector.position = position;
				selector.container = container;
				selector.itemId = item.itemId;
				selector.ground = item.ground;
				candidates.push_back({ item, selector, items });
				labels.Add(wxstr((container.empty() ? std::string() : container + " / ") + (item.ground ? "Ground " : "Item ") + std::to_string(item.itemId) + ", AID " + std::to_string(item.aid) + ", UID " + std::to_string(item.uid)));
				if (item.container) {
					const auto parent = std::find_if(plan.objects.begin(), plan.objects.end(), [&](const auto &entry) { return entry.original == item.key; });
					if (parent != plan.objects.end()) {
						self(self, item.children, parent->id);
					}
				}
			}
		};
		collect(collect, view.tile(position).items, {});
		if (candidates.empty()) {
			return true;
		}
		wxSingleChoiceDialog choose(g_gui.root, "Choose the exact original. Contained items are listed when their container has a World binding.", "Reassociate original", labels);
		if (choose.ShowModal() != wxID_OK) {
			return true;
		}
		auto &chosen = candidates.at(choose.GetSelection());
		std::string error;
		if (!world_layers::captureSelector(chosen.selector, chosen.peers, chosen.item.key, error)) {
			g_gui.PopupDialog("Cannot select original", wxstr(error), wxOK);
			return true;
		}
		object->selector = std::move(chosen.selector);
		object->replaces.reset();
		if (object->mode == world_layers::SourceMode::Map) {
			object->position = position;
			object->itemId = chosen.item.itemId;
		}
	} else {
		const auto &found = spatialIndex.at(position);
		std::vector<std::string> ids;
		wxArrayString labels;
		const world_layers::RelationType* type = nullptr;
		if (request.kind == PickKind::BehaviorRelation) {
			const auto descriptor = next.behavior(object->behaviors.at(request.behavior).id);
			if (!descriptor || !descriptor->relations.contains(request.relation)) {
				cancelPick();
				return true;
			}
			type = &descriptor->relations.at(request.relation);
		}
		EditorMapView view(editor.getMap(), knownItems);
		if (!found.empty()) {
			for (const auto index : found) {
				const auto &id = spatialIndex.entry(index).id;
				const auto target = next.find(id);
				if (type && ((type->targetKind == "item") != (target->kind == world_layers::ObjectKind::Item) || std::any_of(type->capabilities.begin(), type->capabilities.end(), [&](const auto &capability) { return !view.capability(target->itemId, capability); }))) {
					continue;
				}
				ids.push_back(id);
				labels.Add(wxstr(target->name.empty() ? id : target->name + " (" + id + ")"));
			}
		}
		if (ids.empty()) {
			g_gui.SetStatusText("No compatible World object here. Create a World binding or reference point first, or choose another tile.");
			return true;
		}
		size_t selected = 0;
		if (ids.size() > 1) {
			wxSingleChoiceDialog choose(g_gui.root, "Choose the exact related object", "World objects on this tile", labels);
			if (choose.ShowModal() != wxID_OK) {
				return true;
			}
			selected = choose.GetSelection();
		}
		if (request.kind == PickKind::Teleport) {
			if (!object->teleport) {
				object->teleport = world_layers::Teleport {};
			}
			object->teleport->destination = ids[selected];
		} else {
			auto &references = request.kind == PickKind::ObjectRelation ? object->relations[request.relation] : object->behaviors.at(request.behavior).relations[request.relation];
			if (!type || type->maximum == 1) {
				const auto offset = references.size() == 1 ? references.front().offset : world_layers::Position {};
				references = { { ids[selected], offset } };
			} else if (references.size() < type->maximum) {
				references.push_back({ ids[selected], {} });
			} else {
				g_gui.PopupDialog("Relation is full", "Remove an existing related object in Properties before adding another.", wxOK);
				return true;
			}
		}
	}
	// Modal target choosers may process a file notification while they are open.
	if (document.revision() != request.revision) {
		cancelPick();
		g_gui.PopupDialog("World changed", "The document changed while choosing. Your existing work was preserved; choose again.", wxOK);
		return true;
	}
	cancelPick();
	editProject(next, request.source);
	return true;
}

void WorldLayerEditor::validate() {
	++validationRevision;
	validatedRevision = document.revision();
	diagnostics.clear();
	plan = {};
	EditorMapView view(editor.getMap(), knownItems);
	world_layers::validateMap(document.data(), view, plan, diagnostics);
	diagnostics.insert(diagnostics.end(), catalogDiagnostics.begin(), catalogDiagnostics.end());
	if (!catalogDiagnostics.empty()) {
		plan = {};
	}
	spatialIndex.rebuild(document.data(), diagnostics);
}

void WorldLayerEditor::refresh() {
	synchronizeMap();
	if (validatedRevision != document.revision()) {
		validate();
	}
	g_gui.UpdateTitle();
	g_gui.root->UpdateMenubar();
	g_gui.RefreshView();
}

bool WorldLayerEditor::save() {
	if (drag) {
		finishDrag(true);
	}
	checkExternal(false);
	validate();
	if (!diagnostics.empty()) {
		g_gui.PopupDialog("Cannot save World", wxstr(diagnostics.front().describe() + "\nResolve the diagnostics or save a draft copy."), wxOK);
		return false;
	}
	std::string error;
	if (!document.save(error)) {
		g_gui.PopupDialog("Cannot save world layers", wxstr(error), wxOK);
		return false;
	}
	refresh();
	return true;
}

void WorldLayerEditor::saveDraft() {
	wxDirDialog directory(g_gui.root, "Choose where to preserve a World draft", wxEmptyString, wxDD_DEFAULT_STYLE);
	if (directory.ShowModal() != wxID_OK) {
		return;
	}
	const auto root = std::filesystem::u8path(nstr(directory.GetPath())) / ("world-draft-" + std::to_string(wxGetUTCTimeMillis().GetValue()));
	std::filesystem::path file;
	std::string error;
	if (!document.saveDraft(root, file, error)) {
		g_gui.PopupDialog("Cannot preserve draft", wxstr(error), wxOK);
		return;
	}
	g_gui.PopupDialog("World draft preserved", wxstr(file.generic_string() + "\nThis copy is outside the active catalog. The current edits are still open."), wxOK);
}

bool RecoverWorldPublication(const std::filesystem::path &catalog, wxWindow* parent) {
	if (!world_files::pending(catalog)) {
		return true;
	}
	wxMessageDialog prompt(parent, "An earlier save was interrupted. Finish publishing that version, or restore the files from before that save? Later edits are preserved and will be reported as conflicts.", "Recover World files", wxYES_NO | wxCANCEL | wxICON_WARNING);
	prompt.SetYesNoLabels("Finish save", "Restore previous files");
	const auto choice = prompt.ShowModal();
	if (choice == wxID_CANCEL) {
		return false;
	}
	std::string error;
	if (!world_files::recover(catalog, catalog.parent_path(), choice == wxID_NO, error)) {
		wxMessageBox(wxstr(error), "World recovery needs attention", wxOK | wxICON_ERROR, parent);
		return false;
	}
	return true;
}

void WorldLayerEditor::checkExternal(bool interactive) {
	if (interactive && !RecoverWorldPublication(document.data().file, g_gui.root)) {
		return;
	}
	std::vector<WorldExternalChange> changes;
	std::string error;
	const auto result = document.reconcileExternal(false, changes, error);
	const auto previous = externalStatus;
	if (result == WorldExternalResult::Reloaded) {
		externalStatus = "External files reloaded; unrelated local edits were preserved.";
		for (const auto &entry : changes) {
			if (entry.file.extension() == ".lua") {
				externalStatus += " Lua implementation changed; the editor does not execute it.";
				break;
			}
		}
		refresh();
	} else if (result == WorldExternalResult::Conflict || result == WorldExternalResult::Invalid) {
		externalStatus = error + "\nUse Manage > Review external changes to compare or preserve a draft.";
	} else if (!externalStatus.empty() && externalStatus.find("Review external changes") != std::string::npos) {
		externalStatus.clear();
	}
	if (previous != externalStatus) {
		++validationRevision;
		if (!externalStatus.empty() && current() == this) {
			g_gui.SetStatusText(wxstr(externalStatus.substr(0, externalStatus.find('\n'))));
		}
	}
	if (!interactive || result == WorldExternalResult::Unchanged || result == WorldExternalResult::Reloaded) {
		return;
	}
	wxDialog dialog(g_gui.root, wxID_ANY, "Changes outside RME", wxDefaultPosition, wxSize(920, 650), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
	auto layout = new wxBoxSizer(wxVERTICAL);
	auto explanation = new wxStaticText(&dialog, wxID_ANY, wxstr(error));
	explanation->Wrap(860);
	layout->Add(explanation, 0, wxEXPAND | wxALL, 10);
	auto files = new wxChoice(&dialog, wxID_ANY);
	for (const auto &entry : changes) {
		files->Append(wxstr(entry.file.generic_string()));
	}
	layout->Add(files, 0, wxEXPAND | wxLEFT | wxRIGHT, 10);
	auto pages = new wxNotebook(&dialog, wxID_ANY);
	std::vector<wxTextCtrl*> versions;
	for (const auto label : { "Base when loaded", "Local version", "Current file" }) {
		auto text = new wxTextCtrl(pages, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxTE_READONLY | wxTE_DONTWRAP);
		text->SetFont(wxFontInfo(10).Family(wxFONTFAMILY_TELETYPE));
		pages->AddPage(text, label);
		versions.push_back(text);
	}
	const auto show = [&] {
		const auto index = files->GetSelection();
		if (index == wxNOT_FOUND) {
			return;
		}
		const auto &entry = changes.at(index);
		const world_files::Revision values[] = { entry.base, entry.local, entry.disk };
		for (size_t i = 0; i < versions.size(); ++i) {
			versions[i]->ChangeValue(values[i] ? wxstr(*values[i]) : wxString("[File absent]"));
		}
	};
	files->Bind(wxEVT_CHOICE, [&](wxCommandEvent &) { show(); });
	if (!changes.empty()) {
		files->SetSelection(0);
		show();
	}
	layout->Add(pages, 1, wxEXPAND | wxALL, 10);
	auto buttons = new wxBoxSizer(wxHORIZONTAL);
	for (const auto &[id, label] : std::vector<std::pair<int, wxString>> { { wxID_SAVE, "Preserve local draft..." }, { wxID_APPLY, "Reload conflicting files..." }, { wxID_CANCEL, "Keep working" } }) {
		auto button = new wxButton(&dialog, id, label);
		if (id == wxID_APPLY) {
			button->Enable(result == WorldExternalResult::Conflict);
		}
		button->Bind(wxEVT_BUTTON, [&dialog, id](wxCommandEvent &) { dialog.EndModal(id); });
		buttons->Add(button, 0, wxRIGHT, 8);
	}
	layout->Add(buttons, 0, wxALIGN_RIGHT | wxALL, 10);
	dialog.SetSizer(layout);
	const auto answer = dialog.ShowModal();
	if (answer == wxID_SAVE) {
		saveDraft();
		return;
	}
	if (answer != wxID_APPLY) {
		return;
	}
	if (wxMessageBox("Discard local changes in the conflicting files and load the displayed external versions? Unrelated edits will be retained.", "Reload World files", wxYES_NO | wxNO_DEFAULT | wxICON_WARNING, g_gui.root) != wxYES) {
		return;
	}
	std::vector<WorldExternalChange> latest;
	if (!document.externalChanges(latest, error)) {
		g_gui.PopupDialog("Cannot reload World", wxstr(error), wxOK);
		return;
	}
	if (latest.size() != changes.size() || !std::equal(latest.begin(), latest.end(), changes.begin(), [](const auto &a, const auto &b) { return a.file == b.file && a.disk == b.disk; })) {
		g_gui.PopupDialog("Files changed again", "Review the new external versions before discarding any local edits.", wxOK);
		return;
	}
	if (document.reconcileExternal(true, latest, error) != WorldExternalResult::Reloaded) {
		g_gui.PopupDialog("Cannot reload World", wxstr(error), wxOK);
		return;
	}
	externalStatus.clear();
	refresh();
}

bool WorldLayerEditor::suppressed(const Item* item) const {
	return document.visible && plan.originals.contains(reinterpret_cast<uintptr_t>(item));
}

const Item* WorldLayerEditor::sprite(uint16_t id) const {
	if (!g_items.isValidID(id)) {
		return nullptr;
	}
	const auto [it, inserted] = sprites.try_emplace(id);
	if (inserted) {
		it->second.reset(Item::Create(id));
	}
	return it->second.get();
}

std::string WorldLayerEditor::at(const world_layers::Position &position) const {
	if (!document.visible) {
		return {};
	}
	const auto &found = spatialIndex.at(position);
	return found.empty() ? std::string() : spatialIndex.entry(found.front()).id;
}

world_layers::Position WorldLayerEditor::position(const std::string &id) const {
	if (drag && document.selected == id) {
		return *drag;
	}
	if (const auto indexed = spatialIndex.find(id)) {
		return indexed->position;
	}
	const auto object = document.data().find(id);
	return object ? world_layers::objectPosition(document.data(), *object).value_or(world_layers::Position {}) : world_layers::Position {};
}

void WorldLayerEditor::finishDrag(bool commit) {
	if (!drag) {
		return;
	}
	const auto position = *drag;
	drag.reset();
	if (commit) {
		if (const auto object = document.data().find(document.selected)) {
			auto changed = *object;
			changed.position = position;
			edit(document.selected, changed);
		}
	}
	g_gui.RefreshView();
}

void WorldLayerEditor::acknowledgeMapChange() {
	mapRevision = editor.getMap().revision();
}

void WorldLayerEditor::synchronizeMap() {
	if (mapRevision == editor.getMap().revision()) {
		return;
	}
	acknowledgeMapChange();
	validate();
}

void WorldLayerEditor::select(const std::string &id) {
	if (!document.data().find(id)) {
		return;
	}
	auto &selection = editor.getSelection();
	selection.start(Selection::NONE, ACTION_UNSELECT);
	selection.clear();
	selection.finish();
	selection.updateSelectionCount();
	document.selected = id;
	g_gui.SetSelectionMode();
	g_gui.RefreshView();
}

void WorldLayerEditor::edit(const std::string &id, const world_layers::Object &value) {
	const auto object = document.data().find(id);
	if (!object || *object == value) {
		return;
	}
	if (object->mode == world_layers::SourceMode::Map && object->position != value.position) {
		moveBaseItem(id, value);
		return;
	}
	auto next = document.data();
	*next.find(id) = value;
	editProject(next, id);
}

world_layers::MapItem WorldLayerEditor::baseItem(const std::string &id) const {
	const auto found = std::find_if(plan.objects.begin(), plan.objects.end(), [&](const auto &entry) { return entry.id == id; });
	if (found == plan.objects.end() || !found->original) {
		return {};
	}
	const auto definition = document.data().find(id);
	return snapshot(reinterpret_cast<Item*>(static_cast<uintptr_t>(found->original)), definition && definition->selector && definition->selector->ground);
}

bool WorldLayerEditor::moveBaseItem(const std::string &id, const world_layers::Object &value, const world_layers::Project* draft) {
	const auto definition = document.data().find(id);
	if (!definition || !definition->selector || !definition->selector->container.empty() || !world_layers::isValidPosition(value.position)) {
		return false;
	}
	const auto base = baseItem(id);
	const auto origin = editor.getMap().getTile(native(definition->position));
	const auto destination = editor.getMap().getTile(native(value.position));
	if (!base.key || !origin || !destination || (base.ground && destination->ground)) {
		g_gui.PopupDialog("Cannot move base item", "Resolve the base selector and choose an existing destination tile. Moving ground requires an empty ground slot.", wxOK);
		return false;
	}
	std::unique_ptr<Tile> from(origin->deepCopy(editor.getMap())), to(destination->deepCopy(editor.getMap()));
	std::unordered_map<uint64_t, Item*> copied;
	const auto pair = [&](const auto &self, Item* before, Item* after) -> void {
		if (!before || !after) {
			return;
		}
		copied[reinterpret_cast<uintptr_t>(before)] = after;
		if (auto a = before->getContainer()) {
			if (auto b = after->getContainer()) {
				for (size_t i = 0; i < a->getVector().size(); ++i) {
					self(self, a->getVector()[i], b->getVector()[i]);
				}
			}
		}
	};
	const auto pairTile = [&](Tile* before, Tile* after) {
		pair(pair, before->ground, after->ground);
		for (size_t i = 0; i < before->items.size(); ++i) {
			pair(pair, before->items[i], after->items[i]);
		}
	};
	pairTile(origin, from.get());
	pairTile(destination, to.get());
	Item* moving = copied.at(base.key);
	if (from->ground == moving) {
		from->ground = nullptr;
	} else {
		std::erase(from->items, moving);
	}
	to->addItem(moving);
	auto next = draft ? *draft : document.data();
	*next.find(id) = value;
	next.find(id)->selector->position = value.position;
	std::string error;
	for (const auto &resolved : plan.objects) {
		auto object = next.find(resolved.id);
		if (!object || !object->selector || !object->selector->container.empty() || !copied.contains(resolved.original)) {
			continue;
		}
		const auto tile = resolved.id == id || object->selector->position == value.position ? to.get() : from.get();
		std::vector<world_layers::MapItem> candidates;
		if (tile->ground) {
			candidates.push_back(snapshot(tile->ground, true));
		}
		for (auto item : tile->items) {
			candidates.push_back(snapshot(item));
		}
		if (!world_layers::captureSelector(*object->selector, candidates, reinterpret_cast<uintptr_t>(copied.at(resolved.original)), error)) {
			g_gui.PopupDialog("Cannot move base item", wxstr(error), wxOK);
			return false;
		}
	}
	WorldDocumentChange change;
	if (!document.makeChange(next, change, error)) {
		if (!error.empty()) {
			g_gui.PopupDialog("Cannot move base item", wxstr(error), wxOK);
		}
		return false;
	}
	change.selected = id;
	auto action = editor.createAction(ACTION_WORLD_OBJECT);
	action->addChange(Change::CreateWorldDocument(std::move(change)));
	action->addChange(new Change(from.release()));
	action->addChange(new Change(to.release()));
	editor.addAction(action);
	editor.updateActions();
	refresh();
	return true;
}

std::unique_ptr<WorldBaseMove> WorldLayerEditor::beginBaseMove(const Position &offset) {
	synchronizeMap();
	std::set<uint64_t> selected;
	for (const auto tile : editor.getSelection()) {
		if (tile->ground && tile->ground->isSelected()) {
			selected.insert(reinterpret_cast<uintptr_t>(tile->ground));
		}
		for (const auto item : tile->items) {
			if (item->isSelected()) {
				selected.insert(reinterpret_cast<uintptr_t>(item));
			}
		}
	}
	EditorMapView map(editor.getMap(), knownItems);
	return std::make_unique<WorldBaseMove>(document.data(), plan, map, portable(offset), selected);
}

void TrackWorldTileCopy(WorldBaseMove* move, const Tile &before, const Tile &after) {
	if (!move) {
		return;
	}
	if (before.ground && after.ground) {
		move->copied(reinterpret_cast<uintptr_t>(before.ground), reinterpret_cast<uintptr_t>(after.ground));
	}
	for (size_t i = 0; i < before.items.size(); ++i) {
		move->copied(reinterpret_cast<uintptr_t>(before.items[i]), reinterpret_cast<uintptr_t>(after.items[i]));
	}
}

bool WorldLayerEditor::finishBaseMove(WorldBaseMove &move, BatchAction &batch, std::string &error) {
	EditorMapView map(editor.getMap(), knownItems);
	auto next = document.data();
	if (!move.finish(map, next, error)) {
		return false;
	}
	WorldDocumentChange change;
	if (!document.makeChange(next, change, error)) {
		return error.empty(); // No World selector was affected.
	}
	auto action = editor.createAction(ACTION_MOVE);
	action->addChange(Change::CreateWorldDocument(std::move(change)));
	batch.addAndCommitAction(action);
	return true;
}

bool WorldLayerEditor::editProject(const world_layers::Project &value, const std::string &selection) {
	WorldDocumentChange change;
	std::string error;
	if (!document.makeChange(value, change, error)) {
		if (!error.empty()) {
			g_gui.PopupDialog("Cannot edit World", wxstr(error), wxOK);
		}
		return false;
	}
	change.selected = selection;
	auto action = editor.createAction(ACTION_WORLD_OBJECT);
	action->addChange(Change::CreateWorldDocument(std::move(change)));
	editor.addAction(action);
	editor.updateActions();
	refresh();
	return true;
}

void WorldLayerEditor::removeSelected() {
	auto next = document.data();
	std::string error;
	if (!WorldLayerDocument::removeObject(next, document.selected, error)) {
		g_gui.PopupDialog("World dependencies", wxstr(error), wxOK);
		return;
	}
	editProject(next);
}

void WorldLayerEditor::editProperties(wxWindow* parent) {
	finishDrag(false);
	const auto object = document.data().find(document.selected);
	if (!object) {
		return;
	}
	const auto id = document.selected;
	auto value = *object;
	auto draft = document.data();
	const auto original = baseItem(id);
	std::unique_ptr<Item> item(value.kind == world_layers::ObjectKind::Item ? Item::Create(value.itemId) : nullptr);
	if (!item && value.kind == world_layers::ObjectKind::Item) {
		return;
	}
	if (item) {
		item->setActionID(value.aid);
		item->setUniqueID(value.uid);
	}
	PropertiesWindow dialog(parent, &editor.getMap(), editor.getMap().getTile(native(value.position)), item.get(), wxDefaultPosition, &value, &draft, id, &draft, &original);
	if (dialog.ShowModal() == 1) {
		*draft.find(id) = value;
		if (object->mode == world_layers::SourceMode::Map && object->position != value.position) {
			moveBaseItem(id, value, &draft);
		} else {
			editProject(draft, id);
		}
	}
}

namespace {
	class WorldObjectList final : public wxListCtrl {
	public:
		explicit WorldObjectList(wxWindow* parent) :
			wxListCtrl(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL | wxLC_NO_HEADER | wxLC_VIRTUAL) {
			InsertColumn(0, "World object");
			Bind(wxEVT_SIZE, [this](wxSizeEvent &event) {
				SetColumnWidth(0, std::max(1, GetClientSize().GetWidth()));
				event.Skip();
			});
		}
		void setRows(std::vector<wxString> values) {
			rows = std::move(values);
			SetItemCount(static_cast<long>(rows.size()));
			if (!rows.empty()) {
				RefreshItems(0, static_cast<long>(rows.size() - 1));
			}
		}

	protected:
		wxString OnGetItemText(long item, long) const override {
			return item >= 0 && static_cast<size_t>(item) < rows.size() ? rows[item] : wxString {};
		}

	private:
		std::vector<wxString> rows;
	};

	class WorldPalettePanel final : public PalettePanel {
	public:
		explicit WorldPalettePanel(wxWindow* parent) :
			PalettePanel(parent) {
			SetExtraStyle(GetExtraStyle() | wxWS_EX_PROCESS_IDLE);
			auto sizer = new wxBoxSizer(wxVERTICAL);
			load = new wxButton(this, wxID_ANY, "Load server worlds...");
			load->SetToolTip("Associate a server .world.json catalog with the open OTBM.");
			load->Bind(wxEVT_BUTTON, [](wxCommandEvent &) { g_gui.LoadServerWorlds(); });
			sizer->Add(load, 0, wxEXPAND | wxALL, 5);
			createCatalog = new wxButton(this, wxID_ANY, "Create world catalog...");
			createCatalog->Bind(wxEVT_BUTTON, [](wxCommandEvent &) { CreateWorldCatalog(); });
			sizer->Add(createCatalog, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 5);
			auto commands = new wxBoxSizer(wxHORIZONTAL);
			auto add = new wxButton(this, wxID_ANY, "Add...");
			add->Bind(wxEVT_BUTTON, [this, add](wxCommandEvent &) {
				if (!current()) {
					return;
				}
				wxMenu menu;
				const auto entry = [&](const wxString &label, auto callback) { const auto item = menu.Append(wxID_ANY, label); menu.Bind(wxEVT_MENU, callback, item->GetId()); };
				entry("Configure selected base item", [](wxCommandEvent &) { if (auto world = current()){ world->createObject(world_layers::ObjectKind::Item, world_layers::SourceMode::Map);
} });
				entry("Create external item", [](wxCommandEvent &) { if (auto world = current()){ world->createObject(world_layers::ObjectKind::Item, world_layers::SourceMode::Create);
} });
				entry("Replace selected base item", [](wxCommandEvent &) { if (auto world = current()){ world->createObject(world_layers::ObjectKind::Item, world_layers::SourceMode::Replace);
} });
				entry("Create reference point", [](wxCommandEvent &) { if (auto world = current()){ world->createObject(world_layers::ObjectKind::Anchor, world_layers::SourceMode::Create);
} });
				PopupMenu(&menu, add->GetPosition() + wxPoint(0, add->GetSize().y));
				update(false);
			});
			commands->Add(add, 1, wxRIGHT, 5);
			auto manage = new wxButton(this, wxID_ANY, "Manage...");
			manage->Bind(wxEVT_BUTTON, [this, manage](wxCommandEvent &) {
				if (!current()) {
					return;
				}
				wxMenu menu;
				const auto entry = [&](const wxString &label, auto callback) { const auto item = menu.Append(wxID_ANY, label); menu.Bind(wxEVT_MENU, callback, item->GetId()); };
				entry("Layers...", [](wxCommandEvent &) { if (auto world = current()){ world->manageLayers();
} });
				entry("Review external changes...", [](wxCommandEvent &) { if (auto world = current()){ world->checkExternal(true);
} });
				entry("Preserve World draft...", [](wxCommandEvent &) { if (auto world = current()){ world->saveDraft();
} });
				entry("Include behavior descriptor...", [](wxCommandEvent &) { if (auto world = current()){ world->manageDescriptors();
} });
				entry("Rename selected object...", [](wxCommandEvent &) { if (auto world = current()){ world->renameSelected();
} });
				entry("Move selected declaration to layer...", [](wxCommandEvent &) { if (auto world = current()){ world->moveSelectedToLayer();
} });
				entry("Choose related object on map...", [](wxCommandEvent &) { if (auto world = current()){ world->pickRelation();
} });
				entry("Reassociate original on map...", [](wxCommandEvent &) { if (auto world = current()){ world->pickOriginal();
} });
				entry("Delete selected declaration", [](wxCommandEvent &) { if (auto world = current()){ world->removeSelected();
} });
				PopupMenu(&menu, manage->GetPosition() + wxPoint(0, manage->GetSize().y));
				update(false);
			});
			commands->Add(manage, 1);
			sizer->Add(commands, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 5);
			catalog = new wxStaticText(this, wxID_ANY, "No world catalog loaded.");
			sizer->Add(catalog, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 5);
			visible = new wxCheckBox(this, wxID_ANY, "Show world objects");
			visible->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent &) {
				if (auto world = current()) {
					world->finishDrag(false);
					world->document.visible = visible->GetValue();
					world->document.selected.clear();
					g_gui.RefreshView();
				}
			});
			sizer->Add(visible, 0, wxEXPAND | wxALL, 5);
			filter = new wxTextCtrl(this, wxID_ANY);
			filter->SetHint("Find a world or object...");
			filter->Bind(wxEVT_TEXT, [this](wxCommandEvent &) { update(true); });
			sizer->Add(filter, 0, wxEXPAND | wxALL, 5);
			objects = new WorldObjectList(this);
			objects->Bind(wxEVT_LIST_ITEM_SELECTED, [this](wxListEvent &event) {
				const auto index = event.GetIndex();
				if (auto world = current(); !updatingSelection && world && index >= 0 && size_t(index) < ids.size()) {
					world->document.visible = true;
					world->select(ids[index]);
					navigate(false);
				}
			});
			objects->Bind(wxEVT_LIST_ITEM_ACTIVATED, [this](wxListEvent &) { properties(); });
			sizer->Add(objects, 1, wxEXPAND | wxALL, 5);
			auto row = new wxBoxSizer(wxHORIZONTAL);
			propertiesButton = new wxButton(this, wxID_ANY, "Properties...");
			propertiesButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { properties(); });
			row->Add(propertiesButton, 1, wxRIGHT, 4);
			arrivalButton = new wxButton(this, wxID_ANY, "Go to arrival");
			arrivalButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { navigate(true); });
			row->Add(arrivalButton, 1);
			sizer->Add(row, 0, wxEXPAND | wxALL, 5);
			status = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxSize(-1, 120), wxTE_MULTILINE | wxTE_READONLY);
			sizer->Add(status, 0, wxEXPAND | wxALL, 5);
			SetSizer(sizer);
			Bind(wxEVT_IDLE, [this](wxIdleEvent &event) { if (IsShownOnScreen()){ update(false);
} event.Skip(); });
			update(true);
		}
		wxString GetName() const override {
			return "Worlds";
		}
		PaletteType GetType() const override {
			return TILESET_WORLD;
		}
		void OnSwitchIn() override {
			PalettePanel::OnSwitchIn();
			g_gui.SetSelectionMode();
			update(false);
		}
		void OnUpdate() override {
			update(false);
		}

	private:
		wxStaticText* catalog;
		wxButton* load;
		wxButton* createCatalog;
		wxCheckBox* visible;
		wxTextCtrl* filter;
		WorldObjectList* objects;
		wxTextCtrl* status;
		wxButton* propertiesButton;
		wxButton* arrivalButton;
		std::vector<std::string> ids;
		std::unordered_map<std::string, long> rows;
		const WorldLayerDocument* active = nullptr;
		uint64_t revision = 0;
		uint64_t validationRevision = 0;
		std::string selected;
		std::string externalStatus;
		bool updatingSelection = false;

		void update(bool forceList) {
			const auto world = current();
			const auto nextActive = world ? &world->document : nullptr;
			const auto nextRevision = world ? world->document.revision() : 0;
			const auto nextValidation = world ? world->validationRevision : 0;
			const auto nextSelected = world ? world->document.selected : std::string {};
			const auto nextExternalStatus = world ? world->externalStatus : std::string {};
			const bool activeChanged = active != nextActive;
			const bool listChanged = forceList || activeChanged || revision != nextRevision;
			const bool statusChanged = forceList || activeChanged || validationRevision != nextValidation || externalStatus != nextExternalStatus;
			const bool selectionChanged = activeChanged || selected != nextSelected;
			const bool visibilityChanged = visible->GetValue() != (world && world->document.visible);
			if (!listChanged && !statusChanged && !selectionChanged && !visibilityChanged) {
				return;
			}
			active = nextActive;
			selected = nextSelected;
			revision = nextRevision;
			validationRevision = nextValidation;
			externalStatus = nextExternalStatus;
			visible->Enable(world != nullptr);
			visible->SetValue(world && world->document.visible);
			filter->Enable(world != nullptr);
			load->Show(world == nullptr);
			createCatalog->Show(world == nullptr);
			if (world && listChanged) {
				const auto &project = world->document.data();
				catalog->SetLabel(wxstr("World catalog loaded: " + project.file.filename().generic_string()));
				catalog->SetToolTip(wxstr(project.file.generic_string()));
				const auto search = filter->GetValue().Lower();
				std::vector<wxString> labels;
				ids.clear();
				rows.clear();
				for (const auto &layer : project.layers) {
					for (const auto &object : layer.objects) {
						const auto id = world_layers::objectId(layer, object);
						const auto label = wxstr(id + (object.name.empty() ? "" : " - " + object.name));
						if (!search.empty() && !(label + wxstr(" " + std::to_string(object.itemId) + " " + std::to_string(object.aid) + " " + std::to_string(object.uid))).Lower().Contains(search)) {
							continue;
						}
						rows.emplace(id, static_cast<long>(ids.size()));
						ids.push_back(id);
						labels.push_back(label);
					}
				}
				objects->setRows(std::move(labels));
			} else if (!world && listChanged) {
				catalog->SetLabel("No world catalog loaded.");
				catalog->UnsetToolTip();
				ids.clear();
				rows.clear();
				objects->setRows({});
			}
			if (selectionChanged || listChanged) {
				updatingSelection = true;
				objects->SetItemState(-1, 0, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
				if (const auto row = rows.find(selected); row != rows.end()) {
					objects->SetItemState(row->second, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
					objects->EnsureVisible(row->second);
				}
				updatingSelection = false;
			}
			if (world && statusChanged) {
				std::string text = world->diagnostics.empty() ? "World objects are valid.\n" : "World validation needs attention:\n";
				if (!world->externalStatus.empty()) {
					text = world->externalStatus + "\n\n" + text;
				}
				for (const auto &error : world->diagnostics) {
					text += error.describe() + "\n";
				}
				text += "\nSelect and drag objects on the map. Double-click for properties. Ctrl+S saves map and layer changes.";
				status->ChangeValue(wxstr(text));
			} else if (!world && statusChanged) {
				status->ChangeValue("Open an OTBM, then load its server world catalog. You can also configure the catalog in Preferences > Directories.");
			}
			propertiesButton->Enable(world && world->document.data().find(selected));
			arrivalButton->Enable(world && world->document.data().find(selected) && world->document.data().find(selected)->teleport.has_value());
			if (activeChanged || listChanged || statusChanged) {
				Layout();
			}
		}
		void properties() {
			if (auto world = current()) {
				world->editProperties(g_gui.root);
			}
			update(false);
		}
		void navigate(bool arrival) {
			const auto world = current();
			const auto tab = g_gui.GetCurrentMapTab();
			if (!world || !tab) {
				return;
			}
			const auto object = world->document.data().find(world->document.selected);
			if (!object) {
				return;
			}
			const auto position = arrival ? world_layers::destination(world->document.data(), *object) : world_layers::objectPosition(world->document.data(), *object);
			if (position) {
				tab->SetScreenCenterPosition(native(*position));
			}
		}
	};
}

PalettePanel* CreateWorldPalette(wxWindow* parent) {
	return new WorldPalettePanel(parent);
}

void ShowWorldPalette() {
	g_gui.SelectPalettePage(TILESET_WORLD);
	g_gui.SetSelectionMode();
}

void CreateWorldCatalog() {
	const auto editor = g_gui.GetCurrentEditor();
	if (!editor || editor->world || editor->IsLiveClient() || editor->IsLiveServer()) {
		return;
	}
	if (!editor->getMap().hasFile()) {
		g_gui.PopupDialog("Save the base map", "Save this map as OTBM before creating its World catalog.", wxOK);
		return;
	}
	const auto map = std::filesystem::absolute(std::filesystem::u8path(editor->getMap().getFilename())).lexically_normal();
	auto file = map;
	file.replace_extension("world.json");
	auto items = map.parent_path().parent_path().parent_path() / "data" / "items" / "items.xml";
	if (!std::filesystem::exists(items)) {
		wxFileDialog dialog(g_gui.root, "Server item definitions", "", "items.xml", "Item definitions (items.xml)|items.xml", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
		if (dialog.ShowModal() != wxID_OK) {
			return;
		}
		items = std::filesystem::absolute(std::filesystem::u8path(nstr(dialog.GetPath()))).lexically_normal();
	}
	std::string id = map.stem().generic_string();
	for (auto &c : id) {
		if (c >= 'A' && c <= 'Z') {
			c += 'a' - 'A';
		} else if (!(c >= 'a' && c <= 'z') && !(c >= '0' && c <= '9') && c != '_' && c != '-') {
			c = '_';
		}
	}
	if (id.empty() || id.front() < 'a' || id.front() > 'z') {
		id = "world_" + id;
	}
	WorldLayerDocument document;
	std::string error;
	if (!document.create(file, map, items, id, error)) {
		g_gui.PopupDialog("Cannot create World catalog", wxstr(error), wxOK);
		return;
	}
	editor->world = std::make_unique<WorldLayerEditor>(*editor, std::move(document));
	ShowWorldPalette();
	editor->world->refresh();
	editor->world->manageLayers();
}
