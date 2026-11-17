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

#include "main.h"

#include "collab_snapshot.h"

#include "collab_protocol.h"
#include "collab_session.h"

#include "../editor.h"
#include "../gui.h"
#include "../map.h"

#include <sodium.h>
#include <zlib.h>

namespace collab {

	namespace {
		constexpr uint16_t kSnapshotFormat = 1;

		void wipeString(std::string &s) {
			if (!s.empty()) {
				sodium_memzero(s.data(), s.size());
			}
			s.clear();
		}
	}

	void Snapshot::wipe() {
		wipeString(mapName);
		wipeString(data.otbm);
		wipeString(data.monsters);
		wipeString(data.npcs);
		wipeString(data.houses);
		wipeString(data.zones);
		wipeString(comments);
	}

	bool serializeSnapshot(Editor &editor, std::string &raw, std::string &error) {
		Map &map = editor.getMap();

		Snapshot snapshot;
		snapshot.mapName = map.getName();
		{
			ScopedLoadingBar loadingBar("Preparing the map for a new participant...");
			IOMapOTBM io(map.getVersion());
			if (!io.saveMemory(map, snapshot.data)) {
				error = "Could not serialize the map";
				return false;
			}
		}
		snapshot.comments = map.comments.toXml();

		ByteWriter w;
		w.u16(kSnapshotFormat);
		w.blob(snapshot.mapName);
		w.blob(snapshot.data.otbm);
		w.blob(snapshot.data.monsters);
		w.blob(snapshot.data.npcs);
		w.blob(snapshot.data.houses);
		w.blob(snapshot.data.zones);
		w.blob(snapshot.comments);
		if (w.buffer.size() > kMaxSnapshot) {
			error = "The map is too big to share";
			return false;
		}
		raw.assign(reinterpret_cast<const char*>(w.buffer.data()), w.buffer.size());
		return true;
	}

	bool compressSnapshot(const std::string &raw, std::string &compressed, std::string &error) {
		// The bigger the map, the more a fast level matters for how long the participant waits.
		const int level = raw.size() > (64u << 20) ? 1 : (raw.size() > (16u << 20) ? 3 : 6);
		uLongf bound = compressBound(static_cast<uLong>(raw.size()));
		compressed.assign(4 + bound, '\0');
		const uint32_t rawSize = static_cast<uint32_t>(raw.size());
		compressed[0] = static_cast<char>(rawSize >> 24);
		compressed[1] = static_cast<char>(rawSize >> 16);
		compressed[2] = static_cast<char>(rawSize >> 8);
		compressed[3] = static_cast<char>(rawSize);
		if (compress2(reinterpret_cast<Bytef*>(compressed.data() + 4), &bound, reinterpret_cast<const Bytef*>(raw.data()), static_cast<uLong>(raw.size()), level) != Z_OK) {
			error = "Could not compress the map";
			return false;
		}
		compressed.resize(4 + bound);
		return true;
	}

	bool buildSnapshot(Editor &editor, std::string &compressed, std::string &error) {
		std::string raw;
		return serializeSnapshot(editor, raw, error) && compressSnapshot(raw, compressed, error);
	}

	bool parseSnapshot(const std::string &compressed, Snapshot &out, std::string &error) {
		if (compressed.size() < 5) {
			error = "Truncated map data";
			return false;
		}

		const auto* bytes = reinterpret_cast<const uint8_t*>(compressed.data());
		const uint32_t rawSize = (static_cast<uint32_t>(bytes[0]) << 24) | (static_cast<uint32_t>(bytes[1]) << 16) | (static_cast<uint32_t>(bytes[2]) << 8) | bytes[3];
		// deflate cannot expand more than ~1032:1, so a bigger claim is a lie (and an allocation bomb)
		const uint64_t plausible = static_cast<uint64_t>(compressed.size() - 4) * 1032 + 64;
		if (rawSize == 0 || rawSize > kMaxSnapshot || rawSize > plausible) {
			error = "Invalid map size";
			return false;
		}

		std::vector<uint8_t> raw;
		try {
			raw.resize(rawSize);
		} catch (const std::bad_alloc &) {
			error = "Not enough memory for the shared map";
			return false;
		}
		uLongf destLen = rawSize;
		if (uncompress(raw.data(), &destLen, bytes + 4, static_cast<uLong>(compressed.size() - 4)) != Z_OK || destLen != rawSize) {
			sodium_memzero(raw.data(), raw.size());
			error = "Corrupted map data";
			return false;
		}

		bool ok = true;
		try {
			ByteReader r(raw);
			if (r.u16() != kSnapshotFormat) {
				throw ProtocolError("unsupported snapshot format");
			}
			out.mapName = sanitizeText(r.blob(1024), 128);
			out.data.otbm = r.blob(rawSize);
			out.data.monsters = r.blob(rawSize);
			out.data.npcs = r.blob(rawSize);
			out.data.houses = r.blob(rawSize);
			out.data.zones = r.blob(rawSize);
			out.comments = r.blob(rawSize);
			if (out.data.otbm.empty()) {
				throw ProtocolError("snapshot without a map");
			}
		} catch (const ProtocolError &e) {
			error = e.what();
			ok = false;
		}
		sodium_memzero(raw.data(), raw.size());
		if (!ok) {
			out.wipe();
		}
		if (out.mapName.empty()) {
			out.mapName = "Shared map";
		}
		return ok;
	}

	bool loadSnapshotInto(Map &map, const Snapshot &snapshot, std::string &error) {
		IOMapOTBM io((MapVersion()));
		if (!io.loadMemory(map, snapshot.data)) {
			error = nstr(io.getError());
			return false;
		}
		map.comments.loadXml(snapshot.comments);
		return true;
	}

} // namespace collab
