#include "main.h"

#include "world/world_editor.h"
#include "application.h"
#include "complexitem.h"
#include "editor.h"
#include "gui.h"
#include "map_tab.h"
#include "map_display.h"
#include <wx/file.h>

namespace {
	Position native(const world_layers::Position &p) {
		return Position(p.x, p.y, p.z);
	}
	world_layers::Position portable(const Position &p) {
		return { p.x, p.y, p.z };
	}

	class EditorMapView final : public world_layers::MapView {
	public:
		EditorMapView(Map &map, const std::vector<world_layers::UniqueOccurrence> &ids) : map(map), ids(ids) { }
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

WorldLayerEditor::WorldLayerEditor(Editor &owner, WorldLayerDocument data) : document(std::move(data)), editor(owner) {
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
	class WorldLayerPanel final : public wxPanel {
	public:
		explicit WorldLayerPanel(wxWindow* parent) : wxPanel(parent) {
			SetExtraStyle(GetExtraStyle() | wxWS_EX_PROCESS_IDLE);
			auto layout = new wxBoxSizer(wxVERTICAL);
			visible = new wxCheckBox(this, wxID_ANY, "Show external objects and replacements");
			layout->Add(visible, 0, wxEXPAND | wxALL, 6);
			objects = new wxListBox(this, wxID_ANY);
			layout->Add(objects, 1, wxEXPAND | wxALL, 6);
			details = new wxStaticText(this, wxID_ANY, "Open a .world.json project");
			layout->Add(details, 0, wxEXPAND | wxALL, 6);
			auto grid = new wxFlexGridSizer(2, 4, 6);
			grid->AddGrowableCol(1);
			const auto number = [&](const wxString &label, int minimum, int maximum) {
				grid->Add(new wxStaticText(this, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL);
				auto input = new wxSpinCtrl(this, wxID_ANY);
				input->SetRange(minimum, maximum);
				grid->Add(input, 1, wxEXPAND);
				return input;
			};
			xyz[0] = number("X", 0, 65535);
			xyz[1] = number("Y", 0, 65535);
			xyz[2] = number("Floor", 0, 15);
			aid = number("AID (may repeat)", 0, 65535);
			uid = number("UID (unique)", 0, 65535);
			grid->Add(new wxStaticText(this, wxID_ANY, "Destination object"), 0, wxALIGN_CENTER_VERTICAL);
			destination = new wxComboBox(this, wxID_ANY);
			grid->Add(destination, 1, wxEXPAND);
			offset[0] = number("Arrival offset X", -65535, 65535);
			offset[1] = number("Arrival offset Y", -65535, 65535);
			offset[2] = number("Arrival offset floor", -15, 15);
			layout->Add(grid, 0, wxEXPAND | wxALL, 6);
			auto buttons = new wxBoxSizer(wxHORIZONTAL);
			const auto button = [&](const wxString &label, auto handler) {
				auto control = new wxButton(this, wxID_ANY, label);
				control->Bind(wxEVT_BUTTON, handler);
				buttons->Add(control, 1, wxALL, 2);
			};
			button("Apply", [this](wxCommandEvent &) { apply(); });
			button("Go to object", [this](wxCommandEvent &) { navigate(false); });
			button("Go to arrival", [this](wxCommandEvent &) { navigate(true); });
			layout->Add(buttons, 0, wxEXPAND | wxALL, 4);
			auto copy = new wxButton(this, wxID_ANY, "Save selected layer copy...");
			copy->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { saveCopy(); });
			layout->Add(copy, 0, wxEXPAND | wxALL, 6);
			errors = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxSize(-1, 100), wxTE_MULTILINE | wxTE_READONLY);
			layout->Add(errors, 1, wxEXPAND | wxALL, 6);
			SetSizer(layout);
			objects->Bind(wxEVT_LISTBOX, [this](wxCommandEvent &) {
				if (auto world = current()) {
					world->finishDrag(false);
					world->document.selected = nstr(objects->GetStringSelection());
					update(true);
					g_gui.RefreshView();
				}
			});
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
		wxCheckBox* visible;
		wxListBox* objects;
		wxStaticText* details;
		wxSpinCtrl* xyz[3];
		wxSpinCtrl* offset[3];
		wxSpinCtrl* aid;
		wxSpinCtrl* uid;
		wxComboBox* destination;
		wxTextCtrl* errors;
		std::string identity, selection;
		uint64_t revision = 0;

		void update(bool force) {
			auto world = current();
			if (!world) {
				Enable(false);
				identity.clear();
				return;
			}
			Enable(true);
			const auto &document = world->document;
			const auto path = document.data().file.generic_string();
			if (!force && identity == path && revision == document.revision() && selection == document.selected) {
				return;
			}
			identity = path;
			revision = document.revision();
			selection = document.selected;
			visible->SetValue(document.visible);
			objects->Clear();
			destination->Clear();
			destination->Append("");
			for (const auto &layer : document.data().layers) {
				for (const auto &object : layer.objects) {
					const auto id = wxstr(layer.id + "." + object.id);
					objects->Append(id);
					destination->Append(id);
				}
			}
			objects->SetStringSelection(wxstr(selection));
			const auto object = document.data().find(selection);
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
			std::string text = world->diagnostics.empty() ? "Valid against the loaded map. Ctrl+S saves layers. OTBM is read-only in this project." : "Validation failed. Review before starting Canary:\n";
			for (const auto &error : world->diagnostics) {
				text += error.describe() + "\n";
			}
			errors->ChangeValue(wxstr(text));
			Layout();
		}

		void apply() {
			auto world = current();
			if (!world) {
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

void ShowWorldLayerPanel() {
	auto manager = g_gui.GetAuiManager();
	auto &existing = manager->GetPane("world_layers");
	if (existing.IsOk()) {
		existing.Show();
	} else {
		manager->AddPane(new WorldLayerPanel(g_gui.root), wxAuiPaneInfo().Name("world_layers").Caption("World Layers").Right().BestSize(380, 760).MinSize(310, 420).CloseButton(false));
	}
	manager->Update();
}
