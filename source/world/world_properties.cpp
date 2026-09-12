#include "main.h"

#include "world/world_properties.h"
#include "world/world_document.h"
#include "world/world_validation.hpp"
#include "properties_window.h"
#include "map.h"
#include "item.h"
#include <wx/choicdlg.h>
#include <wx/numdlg.h>
#include <wx/scrolwin.h>
#include <charconv>

namespace {
	using namespace world_layers;
	using WorldPosition = world_layers::Position;
	wxString display(const Value &value) {
		if (const auto text = std::get_if<std::string>(&value.data)) {
			return wxstr(*text);
		}
		return wxstr(serializeValue(value));
	}
	Value initial(const Parameter &type) {
		if (type.defaultValue) {
			return *type.defaultValue;
		}
		if (type.type == "boolean") {
			return Value { false };
		}
		if (type.type == "integer" || type.type == "itemId") {
			return Value { int64_t(type.minimum.value_or(type.type == "itemId" ? 1 : 0)) };
		}
		if (type.type == "number") {
			return Value { type.minimum.value_or(0) };
		}
		if (type.type == "enum" && !type.choices.empty()) {
			return type.choices.front();
		}
		if (type.type == "record") {
			return Value { Value::Record {} };
		}
		if (type.type == "list") {
			return Value { Value::List {} };
		}
		if (type.type == "position" || type.type == "offset") {
			return Value { Value::Record { { "x", Value { int64_t(0) } }, { "y", Value { int64_t(0) } }, { "z", Value { int64_t(type.type == "position" ? 7 : 0) } } } };
		}
		if (type.type == "objectRef") {
			return Value { Value::Record { { "object", Value { std::string() } } } };
		}
		return Value { std::string() };
	}
	Parameter scalarType(const Value &value) {
		Parameter result;
		result.type = std::holds_alternative<bool>(value.data) ? "boolean" : std::holds_alternative<int64_t>(value.data) ? "integer"
			: std::holds_alternative<double>(value.data)                                                                 ? "number"
																														 : "string";
		return result;
	}
	Value positionValue(const WorldPosition &position) {
		return Value { Value::Record { { "x", Value { int64_t(position.x) } }, { "y", Value { int64_t(position.y) } }, { "z", Value { int64_t(position.z) } } } };
	}
	WorldPosition valuePosition(const Value &value) {
		const auto record = std::get_if<Value::Record>(&value.data);
		WorldPosition result;
		if (!record) {
			return result;
		}
		int32_t* fields[] = { &result.x, &result.y, &result.z };
		const char* names[] = { "x", "y", "z" };
		for (size_t i = 0; i < 3; ++i) {
			if (const auto found = record->find(names[i]); found != record->end()) {
				if (const auto number = std::get_if<int64_t>(&found->second.data)) {
					*fields[i] = static_cast<int32_t>(*number);
				}
			}
		}
		return result;
	}
	Value referenceValue(const Reference &reference) {
		Value::Record result { { "object", Value { reference.object } } };
		if (reference.offset != WorldPosition {}) {
			result["offset"] = positionValue(reference.offset);
		}
		return Value { std::move(result) };
	}
	Reference valueReference(const Value &value) {
		const auto &record = std::get<Value::Record>(value.data);
		Reference result { std::get<std::string>(record.at("object").data), {} };
		if (const auto found = record.find("offset"); found != record.end()) {
			result.offset = valuePosition(found->second);
		}
		return result;
	}
	wxComboBox* referenceChoice(wxWindow* parent, const Project &project, const std::string &value, bool containers = false) {
		auto choice = new wxComboBox(parent, wxID_ANY, wxstr(value));
		choice->Append("");
		for (const auto &layer : project.layers) {
			for (const auto &object : layer.objects) {
				if (containers && (object.kind != ObjectKind::Item || !g_items.getItemType(object.itemId).isContainer())) {
					continue;
				}
				choice->Append(wxstr(objectId(layer, object)));
			}
		}
		choice->SetToolTip("Choose an object identity or type to search. References follow its position.");
		return choice;
	}
	std::array<wxSpinCtrl*, 3> positionFields(wxWindow* parent, wxSizer* sizer, const WorldPosition &position, bool offset = false) {
		std::array<wxSpinCtrl*, 3> controls;
		const int values[] = { position.x, position.y, position.z };
		const char* labels[] = { "X", "Y", "Floor" };
		for (size_t i = 0; i < 3; ++i) {
			sizer->Add(new wxStaticText(parent, wxID_ANY, labels[i]), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
			const int maximum = i == 2 ? 15 : 65535;
			controls[i] = new wxSpinCtrl(parent, wxID_ANY, "", wxDefaultPosition, wxSize(i == 2 ? 65 : 95, -1), wxSP_ARROW_KEYS, offset ? -maximum : 0, maximum, values[i]);
			sizer->Add(controls[i], 0, wxRIGHT, 10);
		}
		return controls;
	}
	WorldPosition readPosition(const std::array<wxSpinCtrl*, 3> &fields) {
		return { fields[0]->GetValue(), fields[1]->GetValue(), fields[2]->GetValue() };
	}
	void problem(wxWindow* parent, const std::string &message) {
		wxMessageBox(wxstr(message), "World validation", wxOK | wxICON_INFORMATION, parent);
	}
	bool editValue(wxWindow* parent, const wxString &title, const Parameter &type, Value &value, const Project &project, bool fullInteger = false) {
		wxDialog dialog(parent, wxID_ANY, title, wxDefaultPosition, wxSize(540, 370), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
		auto layout = new wxBoxSizer(wxVERTICAL);
		if (!type.help.empty()) {
			layout->Add(new wxStaticText(&dialog, wxID_ANY, wxstr(type.help)), 0, wxEXPAND | wxALL, 8);
		}
		Value draft = value;
		std::function<bool(std::string &)> read;
		if (type.type == "record" || type.type == "list") {
			const bool record = type.type == "record";
			if (record && !std::holds_alternative<Value::Record>(draft.data)) {
				draft = Value { Value::Record {} };
			}
			if (!record && !std::holds_alternative<Value::List>(draft.data)) {
				draft = Value { Value::List {} };
			}
			auto list = new wxListBox(&dialog, wxID_ANY);
			layout->Add(list, 1, wxEXPAND | wxALL, 8);
			std::vector<std::string> names;
			const auto refresh = [&] {
				const int selected = list->GetSelection();
				list->Clear();
				names.clear();
				if (record) {
					const auto &values = std::get<Value::Record>(draft.data);
					for (const auto &[name, field] : type.fields) {
						names.push_back(name);
						const auto found = values.find(name);
						const wxString text = found != values.end() ? display(found->second) : field.defaultValue ? "Default: " + display(*field.defaultValue)
							: field.required                                                                      ? wxString("Required")
																												  : wxString("Not set");
						list->Append(wxstr(field.label.empty() ? name : field.label) + " = " + text);
					}
					for (const auto &[name, field] : values) {
						if (!type.fields.contains(name)) {
							names.push_back(name);
							list->Append(wxstr(name) + " (unknown field; remove or update descriptor)");
						}
					}
				} else {
					const auto &values = std::get<Value::List>(draft.data);
					for (size_t i = 0; i < values.size(); ++i) {
						list->Append(wxString::Format("%zu. ", i + 1) + display(values[i]));
					}
				}
				if (selected != wxNOT_FOUND && unsigned(selected) < list->GetCount()) {
					list->SetSelection(selected);
				}
			};
			const auto edit = [&] {
				const int index = list->GetSelection();
				if (index == wxNOT_FOUND) {
					return;
				}
				if (record) {
					const auto found = type.fields.find(names[index]);
					if (found == type.fields.end()) {
						problem(&dialog, "This field is absent from the current descriptor. Its value is preserved until you remove it.");
						return;
					}
					auto &values = std::get<Value::Record>(draft.data);
					auto local = values.contains(found->first) ? values.at(found->first) : initial(found->second);
					if (editValue(&dialog, wxstr(found->second.label.empty() ? found->first : found->second.label), found->second, local, project, fullInteger)) {
						values[found->first] = std::move(local);
					}
				} else if (type.element.size() == 1) {
					auto &entry = std::get<Value::List>(draft.data)[index];
					editValue(&dialog, "List item", type.element.front(), entry, project, fullInteger);
				}
				refresh();
			};
			auto row = new wxBoxSizer(wxHORIZONTAL);
			auto button = [&](const wxString &label, auto callback) { auto control = new wxButton(&dialog, wxID_ANY, label); control->Bind(wxEVT_BUTTON, callback); row->Add(control, 0, wxRIGHT, 6); };
			button("Edit...", [&](wxCommandEvent &) { edit(); });
			if (!record) {
				button("Add...", [&](wxCommandEvent &) {
					if (type.element.size() != 1) {
						return;
					}
					auto entry = initial(type.element.front());
					if (editValue(&dialog, "New list item", type.element.front(), entry, project, fullInteger)) {
						std::get<Value::List>(draft.data).push_back(std::move(entry));
					}
					refresh();
				});
			}
			button(record ? "Inherit / unset" : "Remove", [&](wxCommandEvent &) {
				const auto index = list->GetSelection();
				if (index == wxNOT_FOUND) {
					return;
				}
				if (record) {
					std::get<Value::Record>(draft.data).erase(names[index]);
				} else {
					auto &values = std::get<Value::List>(draft.data);
					values.erase(values.begin() + index);
				}
				refresh();
			});
			layout->Add(row, 0, wxALL, 8);
			list->Bind(wxEVT_LISTBOX_DCLICK, [&](wxCommandEvent &) { edit(); });
			refresh();
			layout->Add(dialog.CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0, wxALIGN_RIGHT | wxALL, 8);
			dialog.SetSizer(layout);
			dialog.Bind(
				wxEVT_BUTTON, [&](wxCommandEvent &) { std::string error; if (validateParameter(type, draft, error)){ dialog.EndModal(wxID_OK);
} else{ problem(&dialog, error);
} }, wxID_OK
			);
			if (dialog.ShowModal() != wxID_OK) {
				return false;
			}
			value = std::move(draft);
			return true;
		} else if (type.type == "boolean") {
			auto control = new wxCheckBox(&dialog, wxID_ANY, "Enabled");
			control->SetValue(std::holds_alternative<bool>(draft.data) && std::get<bool>(draft.data));
			layout->Add(control, 0, wxALL, 12);
			read = [&, control](std::string &) { draft = Value { control->GetValue() }; return true; };
		} else if (type.type == "enum") {
			auto control = new wxChoice(&dialog, wxID_ANY);
			for (size_t i = 0; i < type.choices.size(); ++i) {
				control->Append(display(type.choices[i]));
				if (draft == type.choices[i]) {
					control->SetSelection(static_cast<int>(i));
				}
			}
			layout->Add(control, 0, wxEXPAND | wxALL, 8);
			read = [&, control](std::string &error) { const auto index = control->GetSelection(); if (index == wxNOT_FOUND) { error = "Choose a value"; return false; } draft = type.choices[index]; return true; };
		} else if (type.type == "position" || type.type == "offset") {
			auto row = new wxBoxSizer(wxHORIZONTAL);
			const auto fields = positionFields(&dialog, row, valuePosition(draft), type.type == "offset");
			layout->Add(row, 0, wxALL, 8);
			read = [&, fields](std::string &) { draft = positionValue(readPosition(fields)); return true; };
		} else if (type.type == "objectRef") {
			Reference reference;
			if (std::holds_alternative<Value::Record>(draft.data)) {
				const auto &record = std::get<Value::Record>(draft.data);
				if (record.contains("object") && std::holds_alternative<std::string>(record.at("object").data)) {
					reference = valueReference(draft);
				}
			}
			auto control = referenceChoice(&dialog, project, reference.object);
			layout->Add(control, 0, wxEXPAND | wxALL, 8);
			auto row = new wxBoxSizer(wxHORIZONTAL);
			const auto fields = positionFields(&dialog, row, reference.offset, true);
			layout->Add(new wxStaticText(&dialog, wxID_ANY, "Spatial offset"), 0, wxLEFT | wxRIGHT, 8);
			layout->Add(row, 0, wxALL, 8);
			read = [&, control, fields](std::string &) { draft = referenceValue({ nstr(control->GetValue()), readPosition(fields) }); return true; };
		} else {
			auto control = new wxTextCtrl(&dialog, wxID_ANY, display(draft), wxDefaultPosition, wxDefaultSize, type.type == "string" ? wxTE_MULTILINE : 0);
			layout->Add(control, 1, wxEXPAND | wxALL, 8);
			read = [&, control](std::string &error) {
				const auto text = nstr(control->GetValue());
				if (type.type == "string") {
					draft = Value { text };
					return true;
				}
				if (type.type == "integer" || type.type == "itemId") {
					int64_t number = 0;
					const auto parsed = std::from_chars(text.data(), text.data() + text.size(), number);
					if (parsed.ec != std::errc {} || parsed.ptr != text.data() + text.size()) {
						error = "Enter an integer without fractions";
						return false;
					}
					draft = Value { number };
				} else {
					double number = 0;
					const auto parsed = std::from_chars(text.data(), text.data() + text.size(), number);
					if (parsed.ec != std::errc {} || parsed.ptr != text.data() + text.size() || !std::isfinite(number)) {
						error = "Enter a finite number";
						return false;
					}
					draft = Value { number };
				}
				return true;
			};
		}
		layout->Add(dialog.CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0, wxALIGN_RIGHT | wxALL, 8);
		dialog.SetSizer(layout);
		dialog.Bind(
			wxEVT_BUTTON, [&](wxCommandEvent &) {
			std::string error;
			if (read(error) && ((fullInteger && type.type == "integer") || validateParameter(type, draft, error))){ dialog.EndModal(wxID_OK);
}
			else{ problem(&dialog, error);
} }, wxID_OK
		);
		if (dialog.ShowModal() != wxID_OK) {
			return false;
		}
		value = std::move(draft);
		return true;
	}
	bool editBehavior(wxWindow* parent, BehaviorBinding &binding, const Project &project) {
		const auto descriptor = project.behavior(binding.id);
		if (!descriptor) {
			problem(parent, "The behavior descriptor is missing");
			return false;
		}
		wxDialog dialog(parent, wxID_ANY, wxstr(descriptor->name.empty() ? descriptor->id : descriptor->name), wxDefaultPosition, wxSize(460, 280), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
		auto draft = binding;
		auto layout = new wxBoxSizer(wxVERTICAL);
		layout->Add(new wxStaticText(&dialog, wxID_ANY, wxstr(descriptor->id + " / contract " + std::to_string(descriptor->contractVersion))), 0, wxALL, 8);
		auto events = new wxCheckListBox(&dialog, wxID_ANY);
		for (const auto &event : descriptor->events) {
			const auto index = events->Append(wxstr(event));
			events->Check(index, std::find(draft.events.begin(), draft.events.end(), event) != draft.events.end());
		}
		layout->Add(events, 1, wxEXPAND | wxALL, 8);
		auto parameters = new wxButton(&dialog, wxID_ANY, "Parameters...");
		parameters->Bind(wxEVT_BUTTON, [&](wxCommandEvent &) {
			Parameter schema;
			schema.type = "record";
			schema.fields = descriptor->parameters;
			Value value { draft.parameters };
			if (editValue(&dialog, "Behavior parameters", schema, value, project)) {
				draft.parameters = std::get<Value::Record>(std::move(value.data));
			}
		});
		layout->Add(parameters, 0, wxEXPAND | wxALL, 8);
		auto relations = new wxButton(&dialog, wxID_ANY, "Related objects...");
		relations->Bind(wxEVT_BUTTON, [&](wxCommandEvent &) {
			Parameter schema;
			schema.type = "record";
			Value::Record values;
			for (const auto &[name, relation] : descriptor->relations) {
				Parameter type;
				type.type = "objectRef";
				if (relation.maximum != 1) {
					type.type = "list";
					type.element.push_back(Parameter {});
					type.element.front().type = "objectRef";
					type.minimum = relation.minimum;
					type.maximum = relation.maximum;
				}
				type.label = relation.label;
				type.help = relation.help;
				type.required = relation.minimum > 0;
				schema.fields[name] = type;
				const auto refs = draft.relations.find(name);
				if (refs == draft.relations.end()) {
					continue;
				}
				if (relation.maximum == 1 && !refs->second.empty()) {
					values[name] = referenceValue(refs->second.front());
				} else {
					Value::List list;
					for (const auto &ref : refs->second) {
						list.push_back(referenceValue(ref));
					}
					values[name] = Value { std::move(list) };
				}
			}
			Value value { values };
			if (editValue(&dialog, "Behavior relations", schema, value, project)) {
				draft.relations.clear();
				for (const auto &[name, entry] : std::get<Value::Record>(value.data)) {
					if (const auto list = std::get_if<Value::List>(&entry.data)) {
						for (const auto &ref : *list) {
							draft.relations[name].push_back(valueReference(ref));
						}
					} else {
						draft.relations[name].push_back(valueReference(entry));
					}
				}
			}
		});
		layout->Add(relations, 0, wxEXPAND | wxALL, 8);
		layout->Add(dialog.CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0, wxALIGN_RIGHT | wxALL, 8);
		dialog.SetSizer(layout);
		dialog.Bind(
			wxEVT_BUTTON, [&](wxCommandEvent &) {
			draft.events.clear();
			for (unsigned i = 0; i < events->GetCount(); ++i){ if (events->IsChecked(i)){ draft.events.push_back(descriptor->events[i]);
}}
			if (draft.events.empty()) { problem(&dialog, "Select at least one event"); return; }
			Parameter schema; schema.type = "record"; schema.fields = descriptor->parameters;
			std::string error;
			if (!validateParameter(schema, Value { draft.parameters }, error)) { problem(&dialog, error); return; }
			for (const auto &[name, type] : descriptor->relations) {
				const auto count = draft.relations.contains(name) ? draft.relations.at(name).size() : 0;
				if (count < type.minimum || count > type.maximum) { problem(&dialog, "Relation requires a different number of targets: " + name); return; }
			}
			dialog.EndModal(wxID_OK); }, wxID_OK
		);
		if (dialog.ShowModal() != wxID_OK) {
			return false;
		}
		binding = std::move(draft);
		return true;
	}
}

struct WorldProperties::State {
	wxNotebook* notebook;
	world_layers::Object &object;
	world_layers::Project &project;
	world_layers::MapItem base;
	const Map* map;
	wxTextCtrl* name = nullptr;
	wxSpinCtrl *itemId = nullptr, *count = nullptr, *subtype = nullptr, *order = nullptr;
	wxCheckBox *hasSubtype = nullptr, *hasTeleport = nullptr;
	wxChoice* lifecycle = nullptr;
	wxComboBox *container = nullptr, *destination = nullptr;
	std::array<wxSpinCtrl*, 3> position {}, offset {};
	wxListBox *attributes = nullptr, *behaviors = nullptr, *relations = nullptr, *contents = nullptr;
	std::vector<std::string> attributeKeys, relationKeys, childIds;

	State(wxNotebook* notebook, Object &object, Project &project, const MapItem &base, const Map* map) :
		notebook(notebook), object(object), project(project), base(base), map(map) { }
	wxScrolledWindow* page(const wxString &title, wxBoxSizer*&layout) {
		auto panel = new wxScrolledWindow(notebook, wxID_ANY);
		panel->SetScrollRate(8, 8);
		layout = new wxBoxSizer(wxVERTICAL);
		panel->SetSizer(layout);
		notebook->AddPage(panel, title);
		return panel;
	}
	template <typename Callback>
	void button(wxWindow* parent, wxSizer* row, const wxString &text, Callback callback) {
		auto button = new wxButton(parent, wxID_ANY, text);
		button->Bind(wxEVT_BUTTON, callback);
		row->Add(button, 0, wxRIGHT, 5);
	}
	void general() {
		wxBoxSizer* layout;
		auto panel = page("World", layout);
		const auto file = project.objects.find(object.id);
		layout->Add(new wxStaticText(panel, wxID_ANY, wxstr("Identity: " + object.id)), 0, wxALL, 8);
		if (file != project.objects.end()) {
			layout->Add(new wxStaticText(panel, wxID_ANY, wxstr("Saved in: " + project.layers[file->second.first].file.generic_string())), 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);
		}
		layout->Add(new wxStaticText(panel, wxID_ANY, "Display name"), 0, wxLEFT | wxRIGHT, 8);
		name = new wxTextCtrl(panel, wxID_ANY, wxstr(object.name));
		layout->Add(name, 0, wxEXPAND | wxALL, 8);
		layout->Add(new wxStaticText(panel, wxID_ANY, object.kind == ObjectKind::Anchor ? "Reference point" : object.mode == SourceMode::Map ? "Existing base item. Moving it also edits the OTBM."
		                                 : object.mode == SourceMode::Replace                                                                ? "External replacement. The original selector stays anchored."
		                                                                                                                                     : "External item. Changes are saved only in JSON."),
		            0, wxALL, 8);
		auto coordinates = new wxBoxSizer(wxHORIZONTAL);
		position = positionFields(panel, coordinates, object.position);
		layout->Add(coordinates, 0, wxALL, 8);
		if (!object.container.empty()) {
			for (auto control : position) {
				control->Disable();
			}
		}
		if (object.kind == ObjectKind::Anchor) {
			return;
		}
		auto row = new wxBoxSizer(wxHORIZONTAL);
		row->Add(new wxStaticText(panel, wxID_ANY, "Item ID"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
		itemId = new wxSpinCtrl(panel, wxID_ANY, "", wxDefaultPosition, wxSize(95, -1), wxSP_ARROW_KEYS, 1, 65535, object.itemId);
		row->Add(itemId, 0, wxRIGHT, 8);
		row->Add(new wxStaticText(panel, wxID_ANY, "Count"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
		count = new wxSpinCtrl(panel, wxID_ANY, "", wxDefaultPosition, wxSize(90, -1), wxSP_ARROW_KEYS, 1, 65535, object.count);
		row->Add(count, 0);
		itemId->Enable(object.mode != SourceMode::Map);
		count->Enable(object.mode != SourceMode::Map);
		layout->Add(row, 0, wxALL, 8);
		row = new wxBoxSizer(wxHORIZONTAL);
		hasSubtype = new wxCheckBox(panel, wxID_ANY, "Override subtype");
		hasSubtype->SetValue(object.subtype.has_value());
		row->Add(hasSubtype, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
		subtype = new wxSpinCtrl(panel, wxID_ANY, "", wxDefaultPosition, wxSize(95, -1), wxSP_ARROW_KEYS, 0, 65535, object.subtype.value_or(0));
		row->Add(subtype, 0);
		hasSubtype->Enable(object.mode != SourceMode::Map);
		subtype->Enable(object.mode != SourceMode::Map);
		layout->Add(row, 0, wxALL, 8);
		layout->Add(new wxStaticText(panel, wxID_ANY, "Lifecycle"), 0, wxLEFT | wxRIGHT, 8);
		lifecycle = new wxChoice(panel, wxID_ANY);
		if (object.mode == SourceMode::Map) {
			lifecycle->Append("Native movement");
			lifecycle->Append("Fixture (fixed)");
			lifecycle->SetSelection(object.lifecycle == Lifecycle::Fixture ? 1 : 0);
		} else {
			lifecycle->Append("Fixture (fixed)");
			lifecycle->Append("Refill on startup (collectible)");
			lifecycle->SetSelection(object.lifecycle == Lifecycle::RefillOnStartup ? 1 : 0);
		}
		layout->Add(lifecycle, 0, wxEXPAND | wxALL, 8);
		if (object.mode != SourceMode::Map) {
			layout->Add(new wxStaticText(panel, wxID_ANY, "Container (empty means map position)"), 0, wxLEFT | wxRIGHT, 8);
			container = referenceChoice(panel, project, object.container, true);
			layout->Add(container, 0, wxEXPAND | wxALL, 8);
			row = new wxBoxSizer(wxHORIZONTAL);
			row->Add(new wxStaticText(panel, wxID_ANY, "Insertion order (0 = first)"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
			order = new wxSpinCtrl(panel, wxID_ANY, "", wxDefaultPosition, wxSize(95, -1), wxSP_ARROW_KEYS, 0, 65535, object.order);
			row->Add(order, 0);
			layout->Add(row, 0, wxALL, 8);
		}
		if (object.selector) {
			layout->Add(new wxStaticText(panel, wxID_ANY, wxstr("Original: item " + std::to_string(object.selector->itemId) + (object.selector->container.empty() ? " at " + std::to_string(object.selector->position.x) + ", " + std::to_string(object.selector->position.y) + ", " + std::to_string(object.selector->position.z) : " inside " + object.selector->container))), 0, wxALL, 8);
		}
	}
	Value::Record values(bool inherited) const {
		auto result = inherited ? base.attributes : object.attributes;
		if (inherited || object.aidOverride || object.aid) {
			result["aid"] = Value { int64_t(inherited ? base.aid : object.aid) };
		}
		if (inherited || object.uidOverride || object.uid) {
			result["uid"] = Value { int64_t(inherited ? base.uid : object.uid) };
		}
		return result;
	}
	void refreshAttributes() {
		attributes->Clear();
		attributeKeys.clear();
		const auto inherited = values(true), overridden = values(false);
		for (const auto* key : { "aid", "uid", "text", "description", "name", "article", "plural", "writer", "date" }) {
			attributeKeys.emplace_back(key);
			const auto from = inherited.find(key), to = overridden.find(key);
			attributes->Append(wxString::FromUTF8(key) + (to != overridden.end() ? " = " + display(to->second) + " (override)" : from != inherited.end() ? " = " + display(from->second) + " (inherited)"
			                                                                                                                                             : wxString(" (inherited / absent)")));
		}
		std::set<std::string> keys;
		for (const auto* collection : { &inherited, &overridden }) {
			if (const auto found = collection->find("custom"); found != collection->end()) {
				for (const auto &[key, value] : std::get<Value::Record>(found->second.data)) {
					keys.insert(key);
				}
			}
		}
		for (const auto &key : keys) {
			attributeKeys.push_back("custom." + key);
			attributes->Append(wxstr("custom." + key));
		}
	}
	void attributePage() {
		wxBoxSizer* layout;
		auto panel = page("Attributes", layout);
		layout->Add(new wxStaticText(panel, wxID_ANY, "AID may repeat. Nonzero UID must be unique. Inherit removes an override."), 0, wxALL, 8);
		attributes = new wxListBox(panel, wxID_ANY);
		layout->Add(attributes, 1, wxEXPAND | wxALL, 8);
		const auto edit = [this, panel] {
			const auto index = attributes->GetSelection();
			if (index == wxNOT_FOUND) {
				return;
			}
			const auto key = attributeKeys[index];
			const bool custom = key.starts_with("custom.");
			const auto own = values(false), inherited = values(true);
			Value value { std::string() };
			if (custom) {
				for (const auto* record : { &inherited, &own }) {
					if (record->contains("custom")) {
						const auto &entries = std::get<Value::Record>(record->at("custom").data);
						if (entries.contains(key.substr(7))) {
							value = entries.at(key.substr(7));
						}
					}
				}
			} else if (own.contains(key)) {
				value = own.at(key);
			} else if (inherited.contains(key)) {
				value = inherited.at(key);
			}
			Parameter type = scalarType(value);
			if (key == "aid" || key == "uid" || key == "date") {
				type.type = "integer";
				type.minimum = 0;
				type.maximum = key == "date" ? 4294967295.0 : 65535.0;
				if (!std::holds_alternative<int64_t>(value.data)) {
					value = Value { int64_t(0) };
				}
			}
			if (!editValue(panel, wxstr(key), type, value, project, custom)) {
				return;
			}
			if (key == "aid") {
				object.aid = static_cast<uint16_t>(std::get<int64_t>(value.data));
				object.aidOverride = true;
			} else if (key == "uid") {
				object.uid = static_cast<uint16_t>(std::get<int64_t>(value.data));
				object.uidOverride = true;
			} else if (custom) {
				if (!object.attributes.contains("custom")) {
					object.attributes["custom"] = Value { Value::Record {} };
				}
				std::get<Value::Record>(object.attributes["custom"].data)[key.substr(7)] = std::move(value);
			} else {
				object.attributes[key] = std::move(value);
			}
			refreshAttributes();
		};
		auto row = new wxBoxSizer(wxHORIZONTAL);
		button(panel, row, "Edit override...", [edit](wxCommandEvent &) { edit(); });
		button(panel, row, "Inherit", [this](wxCommandEvent &) {
			const auto index = attributes->GetSelection();
			if (index == wxNOT_FOUND) {
				return;
			}
			const auto &key = attributeKeys[index];
			if (key == "aid") {
				object.aid = 0;
				object.aidOverride = false;
			} else if (key == "uid") {
				object.uid = 0;
				object.uidOverride = false;
			} else if (key.starts_with("custom.")) {
				if (object.attributes.contains("custom")) {
					auto &custom = std::get<Value::Record>(object.attributes["custom"].data);
					custom.erase(key.substr(7));
					if (custom.empty()) {
						object.attributes.erase("custom");
					}
				}
			} else {
				object.attributes.erase(key);
			}
			refreshAttributes();
		});
		button(panel, row, "Add custom...", [this, panel](wxCommandEvent &) {
			const auto name = wxGetTextFromUser("Attribute key", "Custom attribute", "", panel);
			if (name.empty()) {
				return;
			}
			wxArrayString choices;
			for (const auto* type : { "string", "integer", "number", "boolean" }) {
				choices.Add(type);
			}
			wxSingleChoiceDialog choose(panel, "Value type", "Custom attribute", choices);
			if (choose.ShowModal() != wxID_OK) {
				return;
			}
			Parameter type;
			type.type = nstr(choose.GetStringSelection());
			auto value = initial(type);
			if (!editValue(panel, name, type, value, project, true)) {
				return;
			}
			if (!object.attributes.contains("custom")) {
				object.attributes["custom"] = Value { Value::Record {} };
			}
			std::get<Value::Record>(object.attributes["custom"].data)[nstr(name)] = std::move(value);
			refreshAttributes();
		});
		attributes->Bind(wxEVT_LISTBOX_DCLICK, [edit](wxCommandEvent &) { edit(); });
		layout->Add(row, 0, wxALL, 8);
		refreshAttributes();
	}
	void refreshBehaviors() {
		behaviors->Clear();
		for (const auto &binding : object.behaviors) {
			behaviors->Append(wxstr(binding.id + " / " + std::to_string(binding.contractVersion)));
		}
	}
	void behaviorPage() {
		wxBoxSizer* layout;
		auto panel = page("Behaviors", layout);
		behaviors = new wxListBox(panel, wxID_ANY);
		layout->Add(behaviors, 1, wxEXPAND | wxALL, 8);
		auto row = new wxBoxSizer(wxHORIZONTAL);
		button(panel, row, "Add...", [this, panel](wxCommandEvent &) {
			wxArrayString labels;
			std::vector<const BehaviorDescriptor*> choices;
			for (const auto &descriptor : project.behaviors) {
				if ((descriptor.targetKind == "item") == (object.kind == ObjectKind::Item)) {
					choices.push_back(&descriptor);
					labels.Add(wxstr(descriptor.name.empty() ? descriptor.id : descriptor.name));
				}
			}
			if (choices.empty()) {
				problem(panel, "Include a compatible behavior descriptor in the catalog first");
				return;
			}
			wxSingleChoiceDialog choose(panel, "Behavior", "Add behavior", labels);
			if (choose.ShowModal() != wxID_OK) {
				return;
			}
			const auto descriptor = choices[choose.GetSelection()];
			BehaviorBinding binding;
			binding.id = descriptor->id;
			binding.contractVersion = descriptor->contractVersion;
			binding.events = descriptor->events;
			if (editBehavior(panel, binding, project)) {
				object.behaviors.push_back(std::move(binding));
				refreshBehaviors();
			}
		});
		const auto edit = [this, panel] { const auto index = behaviors->GetSelection(); if (index != wxNOT_FOUND && editBehavior(panel, object.behaviors[index], project)){ refreshBehaviors();
} };
		button(panel, row, "Edit...", [edit](wxCommandEvent &) { edit(); });
		button(panel, row, "Remove", [this](wxCommandEvent &) { const auto index = behaviors->GetSelection(); if (index != wxNOT_FOUND) { object.behaviors.erase(object.behaviors.begin() + index); refreshBehaviors(); } });
		behaviors->Bind(wxEVT_LISTBOX_DCLICK, [edit](wxCommandEvent &) { edit(); });
		layout->Add(row, 0, wxALL, 8);
		refreshBehaviors();
	}
	void refreshRelations() {
		relations->Clear();
		relationKeys.clear();
		for (const auto &[name, refs] : object.relations) {
			relationKeys.push_back(name);
			relations->Append(wxstr(name + " (" + std::to_string(refs.size()) + ")"));
		}
	}
	void relationPage() {
		wxBoxSizer* layout;
		auto panel = page("Relations", layout);
		if (object.kind == ObjectKind::Item) {
			hasTeleport = new wxCheckBox(panel, wxID_ANY, "Native teleport destination");
			hasTeleport->SetValue(object.teleport.has_value());
			layout->Add(hasTeleport, 0, wxALL, 8);
			destination = referenceChoice(panel, project, object.teleport ? object.teleport->destination : "");
			layout->Add(destination, 0, wxEXPAND | wxALL, 8);
			auto row = new wxBoxSizer(wxHORIZONTAL);
			offset = positionFields(panel, row, object.teleport ? object.teleport->destinationOffset : WorldPosition {}, true);
			layout->Add(row, 0, wxALL, 8);
		}
		relations = new wxListBox(panel, wxID_ANY);
		layout->Add(relations, 1, wxEXPAND | wxALL, 8);
		const auto edit = [this, panel](const std::string &name) {
			Parameter type;
			type.type = "list";
			type.element.push_back(Parameter {});
			type.element.front().type = "objectRef";
			Value::List refs;
			if (object.relations.contains(name)) {
				for (const auto &ref : object.relations.at(name)) {
					refs.push_back(referenceValue(ref));
				}
			}
			Value value { std::move(refs) };
			if (!editValue(panel, wxstr(name), type, value, project)) {
				return;
			}
			auto &dest = object.relations[name];
			dest.clear();
			for (const auto &ref : std::get<Value::List>(value.data)) {
				dest.push_back(valueReference(ref));
			}
			refreshRelations();
		};
		auto row = new wxBoxSizer(wxHORIZONTAL);
		button(panel, row, "Add...", [edit, panel](wxCommandEvent &) { const auto name = wxGetTextFromUser("Relation name", "Add relation", "", panel); if (!name.empty()){ edit(nstr(name));
} });
		button(panel, row, "Edit...", [this, edit](wxCommandEvent &) { const auto index = relations->GetSelection(); if (index != wxNOT_FOUND){ edit(relationKeys[index]);
} });
		button(panel, row, "Remove", [this](wxCommandEvent &) { const auto index = relations->GetSelection(); if (index != wxNOT_FOUND) { object.relations.erase(relationKeys[index]); refreshRelations(); } });
		layout->Add(row, 0, wxALL, 8);
		refreshRelations();
	}
	void refreshContents() {
		contents->Clear();
		childIds.clear();
		for (const auto &layer : project.layers) {
			for (const auto &child : layer.objects) {
				if (child.container == object.id || (child.selector && child.selector->container == object.id)) {
					childIds.push_back(objectId(layer, child));
					contents->Append(wxstr(childIds.back() + " (item " + std::to_string(child.itemId) + ")"));
				}
			}
		}
	}
	bool editChild(const std::string &id, const MapItem &original = {}) {
		auto child = *project.find(id);
		auto draft = project;
		std::unique_ptr<Item> preview(Item::Create(child.itemId));
		PropertiesWindow dialog(notebook, map, nullptr, preview.get(), wxDefaultPosition, &child, &draft, id, &draft, &original);
		if (dialog.ShowModal() != 1) {
			return false;
		}
		*draft.find(id) = std::move(child);
		project = std::move(draft);
		refreshContents();
		return true;
	}
	void contentsPage() {
		wxBoxSizer* layout;
		auto panel = page("Contents", layout);
		contents = new wxListBox(panel, wxID_ANY);
		layout->Add(contents, 1, wxEXPAND | wxALL, 8);
		auto row = new wxBoxSizer(wxHORIZONTAL);
		const auto create = [this, panel](bool existing) {
			MapItem original;
			uint16_t id = 0;
			if (existing) {
				wxArrayString labels;
				for (size_t i = 0; i < base.children.size(); ++i) {
					labels.Add(wxString::Format("%zu. Item %u", i + 1, base.children[i].itemId));
				}
				if (labels.empty()) {
					problem(panel, "This original container has no base children");
					return;
				}
				wxSingleChoiceDialog choose(panel, "Select the actual base child", "Configure container content", labels);
				if (choose.ShowModal() != wxID_OK) {
					return;
				}
				original = base.children[choose.GetSelection()];
				id = original.itemId;
			} else {
				const auto number = wxGetNumberFromUser("External item", "Item ID", "Add content", 2828, 1, 65535, panel);
				if (number < 1) {
					return;
				}
				id = static_cast<uint16_t>(number);
			}
			const auto identity = nstr(wxGetTextFromUser("Stable object identity", "Container content", wxstr(object.id + ".item_" + std::to_string(id)), panel));
			if (identity.empty()) {
				return;
			}
			if (project.find(identity)) {
				problem(panel, "That identity already exists");
				return;
			}
			Object child;
			child.id = identity;
			child.itemId = id;
			child.container = object.id;
			child.position = object.position;
			if (existing) {
				child.mode = SourceMode::Map;
				child.lifecycle = Lifecycle::Native;
				child.selector = Selector {};
				child.selector->container = object.id;
				child.selector->itemId = id;
				std::string error;
				if (!captureSelector(*child.selector, base.children, original.key, error)) {
					problem(panel, error);
					return;
				}
			} else {
				child.lifecycle = Lifecycle::RefillOnStartup;
			}
			const auto parent = project.objects.find(object.id);
			if (parent == project.objects.end()) {
				return;
			}
			auto before = project;
			project.layers[parent->second.first].objects.push_back(child);
			Diagnostics diagnostics;
			if (!project.rebuildIndex(diagnostics)) {
				problem(panel, diagnostics.front().describe());
				return;
			}
			if (!editChild(identity, original)) {
				project = std::move(before);
			}
			refreshContents();
		};
		button(panel, row, "Configure base...", [create](wxCommandEvent &) { create(true); });
		button(panel, row, "Add external...", [create](wxCommandEvent &) { create(false); });
		layout->Add(row, 0, wxALL, 8);
		row = new wxBoxSizer(wxHORIZONTAL);
		button(panel, row, "Properties...", [this](wxCommandEvent &) {
			const auto index = contents->GetSelection();
			if (index == wxNOT_FOUND) {
				return;
			}
			MapItem original;
			const auto child = project.find(childIds[index]);
			std::string error;
			if (child && child->selector) {
				resolveSelector(*child->selector, base.children, original, error);
			}
			editChild(childIds[index], original);
		});
		button(panel, row, "Remove declaration", [this, panel](wxCommandEvent &) { const auto index = contents->GetSelection(); if (index == wxNOT_FOUND){ return;
} std::string error; if (!WorldLayerDocument::removeObject(project, childIds[index], error)){ problem(panel, error);
} refreshContents(); });
		layout->Add(row, 0, wxALL, 8);
		refreshContents();
	}
	bool read(std::string &error) {
		object.name = nstr(name->GetValue());
		object.position = readPosition(position);
		if (object.kind == ObjectKind::Anchor) {
			return true;
		}
		if (object.mode != SourceMode::Map) {
			object.itemId = static_cast<uint16_t>(itemId->GetValue());
			object.count = static_cast<uint16_t>(count->GetValue());
			object.subtype = hasSubtype->GetValue() ? std::optional<uint16_t>(static_cast<uint16_t>(subtype->GetValue())) : std::nullopt;
			object.container = nstr(container->GetValue());
			object.order = static_cast<uint32_t>(order->GetValue());
			object.lifecycle = lifecycle->GetSelection() == 1 ? Lifecycle::RefillOnStartup : Lifecycle::Fixture;
		} else {
			object.lifecycle = lifecycle->GetSelection() == 1 ? Lifecycle::Fixture : Lifecycle::Native;
		}
		if (hasTeleport->GetValue()) {
			object.teleport = world_layers::Teleport { nstr(destination->GetValue()), readPosition(offset) };
		} else {
			object.teleport.reset();
		}
		Layer probe;
		probe.schemaVersion = 2;
		probe.id = "validation";
		probe.objects = { object };
		Layer parsed;
		Diagnostics diagnostics;
		if (!parseLayer(serializeLayer(probe), {}, parsed, diagnostics)) {
			error = diagnostics.front().describe();
			return false;
		}
		return true;
	}
};

WorldProperties::WorldProperties(wxNotebook* notebook, world_layers::Object &object, world_layers::Project &project, const world_layers::MapItem &base, const Map* map) :
	state(std::make_unique<State>(notebook, object, project, base, map)) {
	state->general();
	if (object.kind == world_layers::ObjectKind::Item) {
		state->attributePage();
	}
	state->behaviorPage();
	state->relationPage();
	if (object.kind == world_layers::ObjectKind::Item && g_items.getItemType(object.itemId).isContainer()) {
		state->contentsPage();
	}
}
WorldProperties::~WorldProperties() = default;
bool WorldProperties::read(std::string &error) {
	return state->read(error);
}
