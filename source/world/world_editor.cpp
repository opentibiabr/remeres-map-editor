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

namespace {
	Position native(const world_layers::Position &p) {
		return Position(p.x, p.y, p.z);
	}
	world_layers::Position portable(const Position &p) {
		return { p.x, p.y, p.z };
	}

	class EditorMapView final : public world_layers::MapView {
	public:
		EditorMapView(Map &map, const std::unordered_map<uint64_t, std::vector<world_layers::UniqueOccurrence>> &ids) :
			map(map), ids(ids) { }
		bool nativeTeleport(uint16_t id) const override {
			return g_items.getItemType(id).isTeleport();
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
				const auto teleport = item->getTeleport();
				result.items.push_back({ reinterpret_cast<uintptr_t>(item), item->getID(), item->getUniqueID(), teleport != nullptr, teleport ? portable(teleport->getDestination()) : world_layers::Position {} });
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
			for (const auto &[position, entries] : ids) {
				for (const auto &entry : entries) {
					if (requested.contains(entry.uid)) {
						result.push_back(entry);
					}
				}
			}
			return result;
		}

	private:
		Map &map;
		const std::unordered_map<uint64_t, std::vector<world_layers::UniqueOccurrence>> &ids;
	};

	WorldLayerEditor* current() {
		const auto editor = g_gui.GetCurrentEditor();
		return editor ? editor->world.get() : nullptr;
	}
}

WorldLayerEditor::WorldLayerEditor(Editor &owner, WorldLayerDocument data) :
	document(std::move(data)), editor(owner) {
	pugi::xml_document catalog;
	const auto loaded = catalog.load_file(document.data().items.c_str());
	std::unordered_set<uint16_t> teleportIds;
	if (!loaded || !catalog.child("items")) {
		catalogDiagnostics.push_back({ document.data().items, "", "", "Cannot read the project's item catalog" });
	} else {
		for (const auto item : catalog.child("items").children("item")) {
			bool teleport = false;
			for (const auto attribute : item.children("attribute")) {
				if (std::string_view(attribute.attribute("key").value()) == "type" && std::string_view(attribute.attribute("value").value()) == "teleport") {
					teleport = true;
				}
			}
			if (!teleport) {
				continue;
			}
			const uint32_t first = item.attribute("id") ? item.attribute("id").as_uint() : item.attribute("fromid").as_uint();
			const uint32_t last = item.attribute("id") ? first : item.attribute("toid").as_uint();
			if (first == 0 || last > 65535 || first > last) {
				continue;
			}
			for (uint32_t id = first; id <= last; ++id) {
				teleportIds.insert(static_cast<uint16_t>(id));
			}
		}
	}
	for (auto it = editor.getMap().begin(); it != editor.getMap().end(); ++it) {
		if (const auto tile = (*it)->get()) {
			updateTile(tile->getPosition());
		}
	}
	acknowledgeMapChange();
	for (const auto &layer : document.data().layers) {
		for (const auto &object : layer.objects) {
			if (!teleportIds.contains(object.itemId)) {
				catalogDiagnostics.push_back({ layer.file, layer.id + "." + object.id, "/origin/itemId", "Project catalog does not declare this item as a native teleport" });
			}
			if (!sprites.contains(object.itemId)) {
				sprites.emplace(object.itemId, Item::Create(object.itemId));
			}
		}
	}
	validate();
}

WorldLayerEditor::~WorldLayerEditor() = default;

void WorldLayerEditor::validate() {
	++validationRevision;
	diagnostics.clear();
	plan = {};
	EditorMapView view(editor.getMap(), uniqueIds);
	world_layers::validateMap(document.data(), view, plan, diagnostics);
	diagnostics.insert(diagnostics.end(), catalogDiagnostics.begin(), catalogDiagnostics.end());
	if (!catalogDiagnostics.empty()) {
		plan = {};
	}
}

void WorldLayerEditor::refresh() {
	synchronizeMap();
	validate();
	g_gui.UpdateTitle();
	g_gui.root->UpdateMenubar();
	g_gui.RefreshView();
}

bool WorldLayerEditor::save() {
	if (drag) {
		finishDrag(true);
	}
	std::string error;
	if (!document.save(error)) {
		g_gui.PopupDialog("Cannot save world layers", wxstr(error), wxOK);
		return false;
	}
	refresh();
	return true;
}

bool WorldLayerEditor::suppressed(const Item* item) const {
	return document.visible && plan.originals.contains(reinterpret_cast<uintptr_t>(item));
}

const Item* WorldLayerEditor::sprite(uint16_t id) const {
	const auto it = sprites.find(id);
	return it == sprites.end() ? nullptr : it->second.get();
}

std::string WorldLayerEditor::at(const world_layers::Position &position) const {
	if (!document.visible) {
		return {};
	}
	for (const auto &layer : document.data().layers) {
		for (const auto &object : layer.objects) {
			if (object.position == position) {
				return layer.id + "." + object.id;
			}
		}
	}
	return {};
}

world_layers::Position WorldLayerEditor::position(const std::string &id) const {
	if (drag && document.selected == id) {
		return *drag;
	}
	const auto object = document.data().find(id);
	return object ? object->position : world_layers::Position {};
}

void WorldLayerEditor::finishDrag(bool commit) {
	if (drag && commit) {
		if (const auto object = document.data().find(document.selected)) {
			auto changed = *object;
			changed.position = *drag;
			edit(document.selected, changed);
		}
	}
	drag.reset();
	refresh();
}

void WorldLayerEditor::acknowledgeMapChange() {
	mapRevision = editor.getMap().revision();
}

void WorldLayerEditor::synchronizeMap() {
	if (mapRevision == editor.getMap().revision()) {
		return;
	}
	uniqueIds.clear();
	for (auto it = editor.getMap().begin(); it != editor.getMap().end(); ++it) {
		if (const auto tile = (*it)->get()) {
			updateTile(tile->getPosition());
		}
	}
	acknowledgeMapChange();
	validate();
}

void WorldLayerEditor::updateTile(const Position &position) {
	const uint64_t key = (uint64_t(position.x) << 20) | (uint64_t(position.y) << 4) | uint64_t(position.z);
	uniqueIds.erase(key);
	const auto tile = editor.getMap().getTile(position);
	if (!tile) {
		return;
	}
	std::vector<world_layers::UniqueOccurrence> entries;
	const auto scan = [&](const auto &self, Item* item) -> void {
		if (!item) {
			return;
		}
		if (item->getUniqueID()) {
			entries.push_back({ item->getUniqueID(), reinterpret_cast<uintptr_t>(item), portable(position) });
		}
		if (const auto container = item->getContainer()) {
			for (auto child : container->getVector()) {
				self(self, child);
			}
		}
	};
	scan(scan, tile->ground);
	for (auto item : tile->items) {
		scan(scan, item);
	}
	if (!entries.empty()) {
		uniqueIds.emplace(key, std::move(entries));
	}
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
	auto action = editor.createAction(ACTION_WORLD_OBJECT);
	action->addChange(Change::CreateWorldObject(id, value));
	editor.addAction(action);
	editor.updateActions();
	refresh();
}

void WorldLayerEditor::editProperties(wxWindow* parent) {
	finishDrag(false);
	const auto object = document.data().find(document.selected);
	if (!object) {
		return;
	}
	const auto id = document.selected;
	auto value = *object;
	std::unique_ptr<Item> item(Item::Create(value.itemId));
	if (!item) {
		return;
	}
	item->setActionID(value.aid);
	item->setUniqueID(value.uid);
	PropertiesWindow dialog(parent, &editor.getMap(), editor.getMap().getTile(native(value.position)), item.get(), wxDefaultPosition, &value, &document.data(), id);
	if (dialog.ShowModal() == 1) {
		edit(id, value);
	}
}

namespace {
	class WorldPalettePanel final : public PalettePanel {
	public:
		explicit WorldPalettePanel(wxWindow* parent) : PalettePanel(parent) {
			SetExtraStyle(GetExtraStyle() | wxWS_EX_PROCESS_IDLE);
			auto sizer = new wxBoxSizer(wxVERTICAL);
			auto load = new wxButton(this, wxID_ANY, "Load server worlds...");
			load->SetToolTip("Associate a server .world.json catalog with the open OTBM.");
			load->Bind(wxEVT_BUTTON, [](wxCommandEvent &) { g_gui.LoadServerWorlds(); });
			sizer->Add(load, 0, wxEXPAND | wxALL, 5);
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
			objects = new wxListBox(this, wxID_ANY);
			objects->Bind(wxEVT_LISTBOX, [this](wxCommandEvent &) {
				const auto index = objects->GetSelection();
				if (auto world = current(); world && index != wxNOT_FOUND && size_t(index) < ids.size()) {
					world->document.visible = true;
					world->select(ids[index]);
					navigate(false);
				}
			});
			objects->Bind(wxEVT_LISTBOX_DCLICK, [this](wxCommandEvent &) { properties(); });
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
			update(true);
		}
		void OnUpdate() override {
			update(true);
		}

	private:
		wxStaticText* catalog;
		wxCheckBox* visible;
		wxTextCtrl* filter;
		wxListBox* objects;
		wxTextCtrl* status;
		wxButton* propertiesButton;
		wxButton* arrivalButton;
		std::vector<std::string> ids;
		const WorldLayerDocument* active = nullptr;
		uint64_t revision = 0;
		uint64_t validationRevision = 0;
		std::string selected;

		void update(bool force) {
			const auto world = current();
			if (!force && world && active == &world->document && revision == world->document.revision() && validationRevision == world->validationRevision && selected == world->document.selected && visible->GetValue() == world->document.visible) {
				return;
			}
			if (!force && !world && !active) {
				return;
			}
			active = world ? &world->document : nullptr;
			selected = world ? world->document.selected : "";
			revision = world ? world->document.revision() : 0;
			validationRevision = world ? world->validationRevision : 0;
			visible->Enable(world != nullptr);
			visible->SetValue(world && world->document.visible);
			filter->Enable(world != nullptr);
			objects->Freeze();
			objects->Clear();
			ids.clear();
			if (world) {
				const auto &project = world->document.data();
				catalog->SetLabel(wxstr(project.file.filename().generic_string()));
				catalog->SetToolTip(wxstr(project.file.generic_string()));
				const auto search = filter->GetValue().Lower();
				for (const auto &layer : project.layers) {
					for (const auto &object : layer.objects) {
						const auto id = layer.id + "." + object.id;
						const auto label = wxstr(id + (object.name.empty() ? "" : " - " + object.name));
						if (!search.empty() && !(label + wxstr(" " + std::to_string(object.itemId) + " " + std::to_string(object.aid) + " " + std::to_string(object.uid))).Lower().Contains(search)) {
							continue;
						}
						ids.push_back(id);
						const auto index = objects->Append(label);
						if (selected == id) {
							objects->SetSelection(index);
						}
					}
				}
				std::string text = world->diagnostics.empty() ? "World objects are valid.\n" : "World validation needs attention:\n";
				for (const auto &error : world->diagnostics) {
					text += error.describe() + "\n";
				}
				text += "\nSelect and drag objects on the map. Double-click for properties. Ctrl+S saves map and layer changes.";
				status->ChangeValue(wxstr(text));
			} else {
				catalog->SetLabel("No world catalog loaded.");
				catalog->UnsetToolTip();
				status->ChangeValue("Open an OTBM, then load its server world catalog. You can also configure the catalog in Preferences > Directories.");
			}
			objects->Thaw();
			propertiesButton->Enable(world && world->document.data().find(selected));
			arrivalButton->Enable(world && world->document.data().find(selected) && world->document.data().find(selected)->teleport.has_value());
			Layout();
		}
		void properties() {
			if (auto world = current()) {
				world->editProperties(g_gui.root);
			}
			update(true);
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
			const auto position = arrival ? world_layers::destination(world->document.data(), *object) : std::optional(object->position);
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
