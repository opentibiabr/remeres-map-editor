#include "world/world_document.h"

#include <exception>
#include <fstream>
#ifdef _WIN32
	#ifndef NOMINMAX
		#define NOMINMAX
	#endif
	#include <windows.h>
#endif

namespace {
	bool replaceFile(const std::filesystem::path &temporary, const std::filesystem::path &target, std::string &error) {
#ifdef _WIN32
		if (MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
			return true;
		}
		error = "Cannot replace layer file (Windows error " + std::to_string(GetLastError()) + ")";
		return false;
#else
		std::error_code code;
		std::filesystem::rename(temporary, target, code);
		error = code.message();
		return !code;
#endif
	}

	bool writeLayer(const std::filesystem::path &file, const std::string &content, std::string &error) {
		// Reserve a sibling directory atomically, so two editors cannot share a temp file.
		std::filesystem::path directory;
		std::error_code code;
		for (unsigned attempt = 0; attempt < 100; ++attempt) {
			directory = file;
			directory += ".save-" + std::to_string(attempt);
			if (std::filesystem::create_directory(directory, code)) {
				break;
			}
			if (code && code != std::errc::file_exists) {
				error = code.message();
				return false;
			}
			directory.clear();
		}
		if (directory.empty()) {
			error = "Cannot reserve layer save directory";
			return false;
		}
		const auto temporary = directory / "layer.json";
		std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
		output.write(content.data(), static_cast<std::streamsize>(content.size()));
		output.flush();
		const bool written = output.good();
		output.close();
		const bool result = written && !output.fail() && replaceFile(temporary, file, error);
		if (!result && error.empty()) {
			error = "Cannot write complete layer file";
		}
		std::filesystem::remove(temporary, code);
		std::filesystem::remove(directory, code);
		return result;
	}
}

bool WorldLayerDocument::open(const std::filesystem::path &file, std::string &error) {
	try {
		world_layers::Project loaded;
		world_layers::Diagnostics diagnostics;
		if (!world_layers::loadProject(file, loaded, diagnostics)) {
			for (const auto &entry : diagnostics) {
				error += entry.describe() + "\n";
			}
			return false;
		}
		std::vector<std::string> originals, canonical;
		std::string original;
		if (!world_layers::readFile(file, original, error)) {
			return false;
		}
		for (const auto &layer : loaded.layers) {
			std::string bytes;
			if (!world_layers::readFile(layer.file, bytes, error)) {
				return false;
			}
			originals.push_back(std::move(bytes));
			canonical.push_back(world_layers::serializeLayer(layer));
		}
		project = std::move(loaded);
		projectSource = std::move(original);
		source = std::move(originals);
		saved = std::move(canonical);
		savedObjects.clear();
		for (const auto &layer : project.layers) {
			savedObjects.push_back(layer.objects);
		}
		history.clear();
		cursor = 0;
		++generation;
		return true;
	} catch (const std::exception &exception) {
		error = exception.what();
		return false;
	}
}

bool WorldLayerDocument::edit(const std::string &id, const world_layers::Object &value) {
	const auto current = project.find(id);
	if (!current || *current == value || current->id != value.id || current->itemId != value.itemId || current->replaces != value.replaces) {
		return false;
	}
	if (!world_layers::isValidPosition(value.position)) {
		return false;
	}
	if (value.teleport) {
		const auto &p = value.teleport->destinationOffset;
		if (p.x < -65535 || p.x > 65535 || p.y < -65535 || p.y > 65535 || p.z < -15 || p.z > 15) {
			return false;
		}
	}
	history.resize(cursor);
	history.push_back({ id, *current, value });
	*current = value;
	++cursor;
	++generation;
	return true;
}

bool WorldLayerDocument::undo() {
	if (!canUndo()) {
		return false;
	}
	const auto &change = history[--cursor];
	*project.find(change.id) = change.before;
	++generation;
	return true;
}

bool WorldLayerDocument::redo() {
	if (!canRedo()) {
		return false;
	}
	const auto &change = history[cursor++];
	*project.find(change.id) = change.after;
	++generation;
	return true;
}

bool WorldLayerDocument::dirty() const {
	for (size_t i = 0; i < project.layers.size(); ++i) {
		if (project.layers[i].objects != savedObjects[i]) {
			return true;
		}
	}
	return false;
}

bool WorldLayerDocument::save(std::string &error) {
	try {
		std::string bytes;
		if (!world_layers::readFile(project.file, bytes, error)) {
			return false;
		}
		if (bytes != projectSource) {
			error = "Project changed outside RME. Reopen it before saving.";
			return false;
		}
		std::vector<std::string> pending;
		for (size_t i = 0; i < project.layers.size(); ++i) {
			if (!world_layers::readFile(project.layers[i].file, bytes, error)) {
				return false;
			}
			if (bytes != source[i]) {
				error = "Layer changed outside RME: " + project.layers[i].file.generic_string() + ". Reopen it before saving.";
				return false;
			}
			pending.push_back(world_layers::serializeLayer(project.layers[i]));
		}
		for (size_t i = 0; i < pending.size(); ++i) {
			if (pending[i] == saved[i]) {
				continue;
			}
			if (!writeLayer(project.layers[i].file, pending[i], error)) {
				return false;
			}
			// Each successful layer is atomic and immediately acknowledged. Failed layers stay dirty.
			source[i] = pending[i];
			saved[i] = pending[i];
			savedObjects[i] = project.layers[i].objects;
		}
		return true;
	} catch (const std::exception &exception) {
		error = exception.what();
		return false;
	}
}
