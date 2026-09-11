#include "main.h"

#include "world/world_editor.h"
#include "application.h"
#include "complexitem.h"
#include "editor.h"
#include "gui.h"
#include "map_tab.h"
#include "map_display.h"
#include <wx/file.h>
#include <wx/scrolwin.h>

namespace {
	Position native(const world_layers::Position &p) {
		return Position(p.x, p.y, p.z);
	}
	world_layers::Position portable(const Position &p) {
		return { p.x, p.y, p.z };
	}

	class EditorMapView final : public world_layers::MapView {
	public:
		EditorMapView(Map &map, const std::vector<world_layers::UniqueOccurrence> &ids) :
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
			for (const auto &entry : ids) {
				if (requested.contains(entry.uid)) {
					result.push_back(entry);
				}
			}
			return result;
		}

	private:
		Map &map;
		const std::vector<world_layers::UniqueOccurrence> &ids;
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
	const auto scan = [&](const auto &self, Item* item, const Position &position) -> void {
		if (!item) {
			return;
		}
		if (item->getUniqueID()) {
			uniqueIds.push_back({ item->getUniqueID(), reinterpret_cast<uintptr_t>(item), portable(position) });
		}
		if (auto container = item->getContainer()) {
			for (auto child : container->getVector()) {
				self(self, child, position);
			}
		}
	};
	for (auto it = editor.getMap().begin(); it != editor.getMap().end(); ++it) {
		const auto tile = (*it)->get();
		if (!tile) {
			continue;
		}
		scan(scan, tile->ground, tile->getPosition());
		for (auto item : tile->items) {
			scan(scan, item, tile->getPosition());
		}
	}
	for (const auto &layer : document.data().layers) {
		for (const auto &object : layer.objects) {
			if (!teleportIds.contains(object.itemId)) {
				catalogDiagnostics.push_back({ layer.file, layer.id + "." + object.id, "/origin/itemId", "Project catalog does not declare this item as a native teleport" });
			}
			if (!sprites.contains(object.itemId)) {
				sprites.emplace(object.itemId, Item::Create(object.itemId));
			}
			if (document.selected.empty()) {
				document.selected = layer.id + "." + object.id;
			}
		}
	}
	validate();
}

WorldLayerEditor::~WorldLayerEditor() = default;

void WorldLayerEditor::validate() {
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
			document.edit(document.selected, changed);
		}
	}
	drag.reset();
	refresh();
}

namespace {
	class WorldLayerPanel final : public wxScrolledWindow {
	public:
		explicit WorldLayerPanel(wxWindow* parent) :
			wxScrolledWindow(parent) {
			SetExtraStyle(GetExtraStyle() | wxWS_EX_PROCESS_IDLE);
			SetScrollRate(0, FromDIP(12));
			auto root = new wxBoxSizer(wxVERTICAL);
			auto introduction = new wxStaticText(this, wxID_ANY, "Edit external objects on the map.\nYour base OTBM stays unchanged.");
			root->Add(introduction, 0, wxEXPAND | wxALL, FromDIP(8));
			auto projectButtons = new wxBoxSizer(wxHORIZONTAL);
			auto open = new wxButton(this, wxID_ANY, "Open project...");
			open->SetToolTip("Choose the .world.json file supplied with your map or server.");
			open->Bind(wxEVT_BUTTON, [](wxCommandEvent &) { g_gui.OpenWorldProject(); });
			projectButtons->Add(open, 1, wxRIGHT, FromDIP(4));
			save = new wxButton(this, wxID_ANY, "Save layers");
			save->SetToolTip("Save applied edits to layer files (Ctrl+S).");
			save->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
				if (current()) {
					g_gui.SaveMap();
					update(false);
				}
			});
			projectButtons->Add(save, 1);
			root->Add(projectButtons, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(8));
			empty = new wxStaticText(this, wxID_ANY, "Open a world project to get started.\n\nChoose a .world.json file, which links the\nOTBM map and its external layer files.\n\nIf you opened an OTBM, use Open project\nto load its world project in another tab.");
			root->Add(empty, 0, wxEXPAND | wxALL, FromDIP(8));

			content = new wxPanel(this);
			auto layout = new wxBoxSizer(wxVERTICAL);
			project = new wxStaticText(content, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
			project->SetFont(project->GetFont().Bold());
			layout->Add(project, 0, wxEXPAND | wxALL, FromDIP(6));
			saveState = new wxStaticText(content, wxID_ANY, "");
			layout->Add(saveState, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(6));
			visible = new wxCheckBox(content, wxID_ANY, "Show layer objects on map");
			visible->SetToolTip("Uncheck to inspect the original OTBM items. This only changes the preview.");
			layout->Add(visible, 0, wxEXPAND | wxALL, FromDIP(6));
			objectCount = new wxStaticText(content, wxID_ANY, "Objects");
			layout->Add(objectCount, 0, wxLEFT | wxRIGHT, FromDIP(6));
			search = new wxTextCtrl(content, wxID_ANY);
			search->SetHint("Search by object ID...");
			search->SetName("Search world objects");
			search->Bind(wxEVT_TEXT, [this](wxCommandEvent &) { filterObjects(); });
			layout->Add(search, 0, wxEXPAND | wxALL, FromDIP(6));
			objects = new wxListBox(content, wxID_ANY, wxDefaultPosition, FromDIP(wxSize(-1, 112)));
			objects->SetToolTip("Select an object to edit it. Double-click to find it on the map.");
			layout->Add(objects, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(6));
			auto buttons = new wxBoxSizer(wxHORIZONTAL);
			goObject = new wxButton(content, wxID_ANY, "Go to object");
			goObject->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { navigate(false); });
			buttons->Add(goObject, 1, wxRIGHT, FromDIP(4));
			goArrival = new wxButton(content, wxID_ANY, "Go to arrival");
			goArrival->SetToolTip("Show the destination object's position plus the arrival offset.");
			goArrival->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { navigate(true); });
			buttons->Add(goArrival, 1);
			layout->Add(buttons, 0, wxEXPAND | wxALL, FromDIP(6));
			details = new wxStaticText(content, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
			layout->Add(details, 0, wxEXPAND | wxALL, FromDIP(6));
			auto grid = new wxFlexGridSizer(2, 4, 6);
			grid->AddGrowableCol(1);
			const auto number = [&](const wxString &label, int minimum, int maximum) {
				grid->Add(new wxStaticText(content, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL);
				auto input = new wxSpinCtrl(content, wxID_ANY);
				input->SetRange(minimum, maximum);
				grid->Add(input, 1, wxEXPAND);
				return input;
			};
			xyz[0] = number("X", 0, 65535);
			xyz[1] = number("Y", 0, 65535);
			xyz[2] = number("Floor", 0, 15);
			aid = number("AID (may repeat)", 0, 65535);
			uid = number("UID (unique)", 0, 65535);
			aid->SetToolTip("Action IDs may be shared by several objects. 0 means no AID.");
			uid->SetToolTip("Unique IDs must not be shared with another object or map item. 0 means no UID.");
			grid->Add(new wxStaticText(content, wxID_ANY, "Destination object"), 0, wxALIGN_CENTER_VERTICAL);
			destination = new wxComboBox(content, wxID_ANY);
			destination->SetToolTip("The arrival follows this object when it moves. Leave empty for an inactive portal.");
			grid->Add(destination, 1, wxEXPAND);
			offset[0] = number("Arrival offset X", -65535, 65535);
			offset[1] = number("Arrival offset Y", -65535, 65535);
			offset[2] = number("Arrival offset floor", -15, 15);
			offset[0]->SetToolTip("Tiles from the destination object: positive is east, negative is west.");
			offset[1]->SetToolTip("Tiles from the destination object: positive is south, negative is north.");
			offset[2]->SetToolTip("Floors from the destination object: positive is down, negative is up.");
			layout->Add(grid, 0, wxEXPAND | wxALL, FromDIP(6));
			applyButton = new wxButton(content, wxID_ANY, "Apply changes");
			applyButton->SetToolTip("Apply the fields to the preview and undo history, then use Save layers or Ctrl+S.");
			applyButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { apply(); });
			layout->Add(applyButton, 0, wxEXPAND | wxALL, FromDIP(6));
			auto help = new wxStaticText(content, wxID_ANY, "Drag an object on the map, or edit these fields.\nApply field edits, then Save layers (Ctrl+S).\nCtrl+Z / Ctrl+Y: undo / redo.");
			layout->Add(help, 0, wxEXPAND | wxALL, FromDIP(6));
			validation = new wxStaticText(content, wxID_ANY, "");
			layout->Add(validation, 0, wxEXPAND | wxALL, FromDIP(6));
			errors = new wxTextCtrl(content, wxID_ANY, "", wxDefaultPosition, FromDIP(wxSize(-1, 100)), wxTE_MULTILINE | wxTE_READONLY);
			layout->Add(errors, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(6));
			copy = new wxButton(content, wxID_ANY, "Save selected layer copy...");
			copy->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { saveCopy(); });
			layout->Add(copy, 0, wxEXPAND | wxALL, FromDIP(6));
			content->SetSizer(layout);
			root->Add(content, 0, wxEXPAND | wxALL, FromDIP(2));
			SetSizer(root);
			objects->Bind(wxEVT_LISTBOX, [this](wxCommandEvent &) {
				if (auto world = current()) {
					world->finishDrag(false);
					world->document.selected = nstr(objects->GetStringSelection());
					update(true);
					g_gui.RefreshView();
				}
			});
			objects->Bind(wxEVT_LISTBOX_DCLICK, [this](wxCommandEvent &) { navigate(false); });
			visible->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent &) {
				if (auto world = current()) {
					world->finishDrag(false);
					world->document.visible = visible->GetValue();
					g_gui.RefreshView();
				}
			});
			Bind(wxEVT_IDLE, [this](wxIdleEvent &event) { update(false); event.Skip(); });
			update(true);
		}

	private:
		wxPanel* content;
		wxStaticText* empty;
		wxStaticText* project;
		wxStaticText* saveState;
		wxStaticText* objectCount;
		wxStaticText* validation;
		wxTextCtrl* search;
		wxButton* save;
		wxButton* applyButton;
		wxButton* goObject;
		wxButton* goArrival;
		wxButton* copy;
		wxCheckBox* visible;
		wxListBox* objects;
		wxStaticText* details;
		wxSpinCtrl* xyz[3];
		wxSpinCtrl* offset[3];
		wxSpinCtrl* aid;
		wxSpinCtrl* uid;
		wxComboBox* destination;
		wxTextCtrl* errors;
		const WorldLayerDocument* activeDocument = nullptr;
		std::string identity, selection;
		uint64_t revision = 0;
		bool dirty = false;
		bool previewVisible = true;

		void filterObjects() {
			const auto world = current();
			if (!world) {
				return;
			}
			const auto query = search->GetValue().Lower();
			objects->Freeze();
			objects->Clear();
			for (const auto &layer : world->document.data().layers) {
				for (const auto &object : layer.objects) {
					const auto id = wxstr(layer.id + "." + object.id);
					if (query.empty() || id.Lower().Contains(query)) {
						objects->Append(id);
					}
				}
			}
			objects->SetStringSelection(wxstr(world->document.selected));
			objects->Thaw();
			if (world->document.data().objects.empty()) {
				objectCount->SetLabel("No objects defined in this project's layer files.");
			} else if (objects->IsEmpty()) {
				objectCount->SetLabel("No matches. Clear the search to see all objects.");
			} else {
				objectCount->SetLabel(wxString::Format("Objects (%zu of %zu)", static_cast<size_t>(objects->GetCount()), world->document.data().objects.size()));
			}
		}

		void update(bool force) {
			if (!force && !IsShownOnScreen()) {
				return;
			}
			auto world = current();
			save->Enable(world != nullptr);
			if (!world) {
				if (!force && !activeDocument && empty->IsShown()) {
					return;
				}
				content->Hide();
				empty->Show();
				activeDocument = nullptr;
				identity.clear();
				Layout();
				FitInside();
				return;
			}
			content->Show();
			empty->Hide();
			const auto &document = world->document;
			const auto path = document.data().file.generic_string();
			const bool modified = document.dirty();
			if (!force && activeDocument == &document && identity == path && revision == document.revision() && selection == document.selected) {
				if (dirty != modified) {
					dirty = modified;
					saveState->SetLabel(dirty ? "Unsaved layer changes - Ctrl+S to save." : "No unsaved layer changes.");
				}
				if (previewVisible != document.visible) {
					previewVisible = document.visible;
					visible->SetValue(previewVisible);
				}
				return;
			}
			if (activeDocument != &document || identity != path) {
				search->ChangeValue("");
			}
			activeDocument = &document;
			identity = path;
			revision = document.revision();
			selection = document.selected;
			dirty = modified;
			previewVisible = document.visible;
			project->SetLabel("Project: " + wxstr(document.data().file.filename().generic_string()));
			project->SetToolTip(wxstr(path));
			saveState->SetLabel(dirty ? "Unsaved layer changes - Ctrl+S to save." : "No unsaved layer changes.");
			visible->SetValue(document.visible);
			filterObjects();
			destination->Clear();
			destination->Append("");
			for (const auto &layer : document.data().layers) {
				for (const auto &object : layer.objects) {
					const auto id = wxstr(layer.id + "." + object.id);
					destination->Append(id);
				}
			}
			const auto object = document.data().find(selection);
			applyButton->Enable(object != nullptr);
			goObject->Enable(object != nullptr);
			goArrival->Enable(object && world_layers::destination(document.data(), *object).has_value());
			copy->Enable(object != nullptr);
			for (auto input : xyz) {
				input->Enable(object != nullptr);
			}
			for (auto input : offset) {
				input->Enable(object != nullptr);
			}
			aid->Enable(object != nullptr);
			uid->Enable(object != nullptr);
			destination->Enable(object != nullptr);
			details->SetToolTip(wxstr(path));
			if (object) {
				xyz[0]->SetValue(object->position.x);
				xyz[1]->SetValue(object->position.y);
				xyz[2]->SetValue(object->position.z);
				aid->SetValue(object->aid);
				uid->SetValue(object->uid);
				const auto p = object->teleport ? object->teleport->destinationOffset : world_layers::Position {};
				offset[0]->SetValue(p.x);
				offset[1]->SetValue(p.y);
				offset[2]->SetValue(p.z);
				destination->SetValue(object->teleport ? wxstr(object->teleport->destination) : wxString());
				std::string text = selection + "\nItem " + std::to_string(object->itemId) + " | external layer";
				if (object->replaces) {
					const auto &p = object->replaces->position;
					text += "\nReplaces original at " + std::to_string(p.x) + ", " + std::to_string(p.y) + ", " + std::to_string(p.z);
				}
				details->SetLabel(wxstr(text));
			} else {
				details->SetLabel("Select an external object in the list or on the map.");
			}
			validation->SetLabel(world->diagnostics.empty() ? "Ready: no validation errors in this map." : "Needs attention: fix the issues below.");
			std::string text;
			for (const auto &error : world->diagnostics) {
				text += error.describe() + "\n";
			}
			errors->ChangeValue(wxstr(text));
			errors->Show(!world->diagnostics.empty());
			content->Layout();
			Layout();
			FitInside();
		}

		void apply() {
			auto world = current();
			if (!world || activeDocument != &world->document || selection != world->document.selected) {
				update(true);
				return;
			}
			const auto object = world->document.data().find(world->document.selected);
			if (!object) {
				return;
			}
			auto changed = *object;
			changed.position = { xyz[0]->GetValue(), xyz[1]->GetValue(), xyz[2]->GetValue() };
			changed.aid = static_cast<uint16_t>(aid->GetValue());
			changed.uid = static_cast<uint16_t>(uid->GetValue());
			const auto target = nstr(destination->GetValue());
			if (target.empty()) {
				changed.teleport.reset();
			} else {
				changed.teleport = world_layers::Teleport { target, { offset[0]->GetValue(), offset[1]->GetValue(), offset[2]->GetValue() } };
			}
			world->document.edit(world->document.selected, changed);
			world->refresh();
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

		void saveCopy() {
			const auto world = current();
			if (!world) {
				return;
			}
			const auto entry = world->document.data().objects.find(world->document.selected);
			if (entry == world->document.data().objects.end()) {
				return;
			}
			wxFileDialog dialog(this, "Save layer copy", "", "copy.layer.json", "World layer (*.layer.json)|*.layer.json", wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
			if (dialog.ShowModal() != wxID_OK || !dialog.GetPath().EndsWith(".layer.json")) {
				return;
			}
			std::error_code code;
			const auto target = std::filesystem::weakly_canonical(std::filesystem::u8path(nstr(dialog.GetPath())), code);
			if (code) {
				g_gui.PopupDialog("Save copy", wxstr(code.message()), wxOK);
				return;
			}
			const auto &project = world->document.data();
			const auto sameFile = [&](const std::filesystem::path &file) {
				std::error_code error;
				return target == std::filesystem::weakly_canonical(file, error) || std::filesystem::equivalent(target, file, error);
			};
			if (sameFile(project.map) || sameFile(project.items) || sameFile(project.file)) {
				return;
			}
			for (const auto &layer : world->document.data().layers) {
				if (sameFile(layer.file)) {
					g_gui.PopupDialog("Save copy", "Choose a new layer file. Use Ctrl+S to save the project.", wxOK);
					return;
				}
			}
			wxTempFile file(dialog.GetPath());
			const auto bytes = world_layers::serializeLayer(world->document.data().layers[entry->second.first]);
			if (!file.IsOpened() || !file.Write(bytes.data(), bytes.size()) || !file.Commit()) {
				g_gui.PopupDialog("Save copy", "Cannot write layer copy", wxOK);
			}
		}
	};
}

void ShowWorldLayerPanel(bool show) {
	auto manager = g_gui.GetAuiManager();
	auto &existing = manager->GetPane("world_layers");
	if (existing.IsOk()) {
		existing.Show(show);
	} else if (show) {
		manager->AddPane(new WorldLayerPanel(g_gui.root), wxAuiPaneInfo().Name("world_layers").Caption("World Layers").Right().BestSize(g_gui.root->FromDIP(wxSize(400, 760))).MinSize(g_gui.root->FromDIP(wxSize(340, 420))).CloseButton(true));
	}
	manager->Update();
}

bool IsWorldLayerPanelShown() {
	const auto manager = g_gui.GetAuiManager();
	if (!manager) {
		return false;
	}
	const auto &pane = manager->GetPane("world_layers");
	return pane.IsOk() && pane.IsShown();
}
