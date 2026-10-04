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

#ifndef RME_COLLAB_PROTOCOL_H
#define RME_COLLAB_PROTOCOL_H

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace collab {

	constexpr uint16_t kProtocolVersion = 1;

	// Limits. Everything read from the network is checked against these.
	constexpr size_t kMaxFrame = 1024 * 1024; // plaintext, type byte included
	constexpr size_t kMaxUsers = 32;
	constexpr size_t kMaxName = 32; // characters
	constexpr size_t kMaxChat = 500; // characters
	constexpr size_t kMinPassword = 6;
	constexpr size_t kSnapshotChunk = 256 * 1024;
	constexpr size_t kMaxTilesPerFrame = 4000;
	constexpr size_t kMaxTileFrameBytes = 900 * 1024; // keeps a frame under kMaxFrame
	constexpr size_t kMaxSnapshot = 0x7FFFFFFF; // compressed and uncompressed
	constexpr size_t kMaxOutbox = 64 * 1024 * 1024; // bytes queued for one peer
	constexpr int kMaxCursorsPerSecond = 30;
	constexpr int kMaxChatPerSecond = 5;

	enum class Msg : uint8_t {
		Hello = 1, // C->S name, rmeVersion, protocol
		Welcome, // S->C userId, color, role, users
		Reject, // S->C reason
		UserJoined, // S->C user
		UserLeft, // S->C userId
		UserUpdated, // S->C user
		Cursor, // C->S: x y z brush flags, S->C: userId x y z brush flags
		Chat, // C->S text
		ChatMsg, // S->C userId, text, time, system
		SetRole, // C->S userId, role
		Kick, // C->S userId
		Bye, // both reason
		SnapshotBegin, // S->C totalBytes, chunkCount
		SnapshotChunk, // S->C raw bytes
		SnapshotEnd, // S->C crc32
		TileBatch, // C->S seq, actionType, count, tile records
		TileUpdate, // S->C originUserId, originSeq, count, tile records
		MetaOps, // both: houses, towns, waypoints, zones, map properties
		FullResync, // S->C: a whole-map operation happened, a new snapshot follows
		HistoryQuery, // C(admin)->S beforeId, limit, user filter
		HistoryPage, // S->C(admin) replace flag, entries
		HistoryAppend, // S->C(admin) one new or updated entry
		HistoryRevert, // C(admin)->S entryId, force
		HistoryReapply, // C(admin)->S entryId, force
		HistoryResult, // S->C entryId, ok, applied, conflicts, message
		SaveRequest, // C(admin)->S
		SaveNotice, // S->C map name: participants with a shared copy save it locally
		View, // C->S x, y, floor of the screen center; S->C with the user id first
		Summon, // C(admin)->S x, y, floor; S->C summoner id first: everybody goes there
		ClaimAdd, // C->S x1, y1, x2, y2, floor
		ClaimRemove, // C->S claim id
		Claims, // S->C op (0 add, 1 remove), claim
		HistoryPreview, // C(admin)->S entryId, reapply, force: what would a revert change
		HistoryPreviewResult, // S->C tiles it would change, conflicts, message
	};

	enum class Role : uint8_t {
		Host,
		Admin,
		Editor,
		Viewer,
	};

	inline const char* roleName(Role role) {
		switch (role) {
			case Role::Host:
				return "Host";
			case Role::Admin:
				return "Admin";
			case Role::Editor:
				return "Editor";
			case Role::Viewer:
				return "Viewer";
		}
		return "?";
	}

	struct ProtocolError : std::runtime_error {
		using std::runtime_error::runtime_error;
	};

	// Bounds-checked big-endian reader. Every read past the end throws ProtocolError.
	class ByteReader {
	public:
		ByteReader(const uint8_t* data, size_t size) :
			data(data), size(size) { }
		explicit ByteReader(const std::vector<uint8_t> &buffer, size_t offset = 0) :
			data(buffer.data()), size(buffer.size()) {
			skip(offset);
		}

		uint8_t u8() {
			need(1);
			return data[pos++];
		}
		uint16_t u16() {
			need(2);
			uint16_t v = static_cast<uint16_t>((data[pos] << 8) | data[pos + 1]);
			pos += 2;
			return v;
		}
		uint64_t u64() {
			const uint64_t high = u32();
			return (high << 32) | u32();
		}
		uint32_t u32() {
			need(4);
			uint32_t v = (static_cast<uint32_t>(data[pos]) << 24) | (static_cast<uint32_t>(data[pos + 1]) << 16) | (static_cast<uint32_t>(data[pos + 2]) << 8) | data[pos + 3];
			pos += 4;
			return v;
		}
		// u16 length prefix, rejected when longer than maxLen bytes.
		std::string str(size_t maxLen) {
			size_t len = u16();
			if (len > maxLen) {
				throw ProtocolError("string too long");
			}
			need(len);
			std::string s(reinterpret_cast<const char*>(data + pos), len);
			pos += len;
			return s;
		}
		// u32 length prefix, rejected when longer than maxLen bytes.
		std::string blob(size_t maxLen) {
			size_t len = u32();
			if (len > maxLen) {
				throw ProtocolError("blob too long");
			}
			need(len);
			std::string s(reinterpret_cast<const char*>(data + pos), len);
			pos += len;
			return s;
		}
		void skip(size_t n) {
			need(n);
			pos += n;
		}
		size_t remaining() const noexcept {
			return size - pos;
		}
		const uint8_t* current() const noexcept {
			return data + pos;
		}

	private:
		void need(size_t n) const {
			if (n > size - pos) {
				throw ProtocolError("truncated message");
			}
		}

		const uint8_t* data;
		size_t size;
		size_t pos = 0;
	};

	class ByteWriter {
	public:
		void u8(uint8_t v) {
			buffer.push_back(v);
		}
		void u16(uint16_t v) {
			buffer.push_back(static_cast<uint8_t>(v >> 8));
			buffer.push_back(static_cast<uint8_t>(v));
		}
		void u32(uint32_t v) {
			u16(static_cast<uint16_t>(v >> 16));
			u16(static_cast<uint16_t>(v));
		}
		void u64(uint64_t v) {
			u32(static_cast<uint32_t>(v >> 32));
			u32(static_cast<uint32_t>(v));
		}
		void str(const std::string &s) {
			if (s.size() > 0xFFFF) {
				throw ProtocolError("string too long");
			}
			u16(static_cast<uint16_t>(s.size()));
			buffer.insert(buffer.end(), s.begin(), s.end());
		}
		void blob(const std::string &s) {
			u32(static_cast<uint32_t>(s.size()));
			buffer.insert(buffer.end(), s.begin(), s.end());
		}
		std::vector<uint8_t> buffer;
	};

	// Frame plaintext = type byte + payload. Payloads are fixed binary layouts written with
	// ByteWriter (no general-purpose parser on untrusted input).
	inline std::vector<uint8_t> makeFrame(Msg type, const std::vector<uint8_t> &payload = {}) {
		std::vector<uint8_t> frame;
		frame.reserve(payload.size() + 1);
		frame.push_back(static_cast<uint8_t>(type));
		frame.insert(frame.end(), payload.begin(), payload.end());
		return frame;
	}

// Runnable check of the protocol primitives and crypto, see collab_selfcheck.cpp.
	// Exposed as the hidden --collab-selfcheck command line switch.
	bool selfCheck();

} // namespace collab

#endif
