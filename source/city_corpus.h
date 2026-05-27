//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////
// Remere's Map Editor is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//////////////////////////////////////////////////////////////////////

#ifndef RME_CITY_CORPUS_H
#define RME_CITY_CORPUS_H

class Editor;
class wxWindow;

namespace CityCorpus {
bool ExportSelection(Editor &editor, wxWindow* parent);
bool ExportAllTowns(Editor &editor, wxWindow* parent);
}

#endif
