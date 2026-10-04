//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////
// Remere's Map Editor is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Remere's Map Editor is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <http://www.gnu.org/licenses/>.
//////////////////////////////////////////////////////////////////////

#ifndef RME_COLLAB_SNAPSHOT_H
#define RME_COLLAB_SNAPSHOT_H

#include "../iomap_otbm.h"

class Editor;
class Map;

namespace collab {

	// Everything a participant needs to rebuild the host's map.
	struct Snapshot {
		std::string mapName;
		IOMapOTBM::MemoryMap data;
		std::string comments; // <map>-comments.xml content

		// Overwrites the buffers: a protected copy must not leave the map lying in freed memory.
		void wipe();
	};

	// Host, GUI thread (shows a loading bar). Output: u32 uncompressed size + zlib stream.
	bool buildSnapshot(Editor &editor, std::string &compressed, std::string &error);
	// Participant. Rejects anything malformed or bigger than kMaxSnapshot.
	bool parseSnapshot(const std::string &compressed, Snapshot &out, std::string &error);
	// Fills an empty map from a snapshot.
	bool loadSnapshotInto(Map &map, const Snapshot &snapshot, std::string &error);

} // namespace collab

#endif
