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

#include "collab_crypto.h"
#include "collab_protocol.h"
#include "collab_session.h"
#include "collab_connection.h"
#include "collab_journal.h"
#include "collab_meta.h"
#include "collab_snapshot.h"
#include "collab_tile_codec.h"

#include "../iomap.h"
#include "../net_connection.h"
#include "../map.h"
#include "../monster.h"
#include "../npc.h"
#include "../spawn_monster.h"
#include "../spawn_npc.h"
#include "../tile.h"

#include <zlib.h>

#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <random>

namespace collab {

	namespace {
		int failures = 0;

		void check(bool ok, const char* what) {
			if (!ok) {
				std::fprintf(stderr, "collab selfcheck FAILED: %s\n", what);
				++failures;
			}
		}

		template <typename F>
		bool throwsProtocolError(F fn) {
			try {
				fn();
			} catch (const ProtocolError &) {
				return true;
			}
			return false;
		}

		void checkReaderWriter() {
			ByteWriter w;
			w.u8(7);
			w.u16(0xBEEF);
			w.u32(0xDEADBEEF);
			w.str("hello");

			ByteReader r(w.buffer);
			check(r.u8() == 7 && r.u16() == 0xBEEF && r.u32() == 0xDEADBEEF && r.str(16) == "hello" && r.remaining() == 0, "reader/writer round trip");

			// Every truncation of a valid buffer must throw, never read out of bounds.
			for (size_t cut = 0; cut < w.buffer.size(); ++cut) {
				std::vector<uint8_t> truncated(w.buffer.begin(), w.buffer.begin() + cut);
				check(throwsProtocolError([&] {
						  ByteReader t(truncated);
						  t.u8();
						  t.u16();
						  t.u32();
						  t.str(16);
					  }),
					  "truncated buffer rejected");
			}

			check(throwsProtocolError([&] {
					  ByteReader t(w.buffer);
					  t.u8();
					  t.u16();
					  t.u32();
					  t.str(2); // longer than the cap
				  }),
				  "string cap enforced");

			// Garbage: either parses or throws ProtocolError, nothing else.
			std::mt19937 rng(1234);
			for (int i = 0; i < 2000; ++i) {
				std::vector<uint8_t> garbage(rng() % 64);
				for (auto &b : garbage) {
					b = static_cast<uint8_t>(rng());
				}
				try {
					ByteReader g(garbage);
					while (g.remaining() > 0) {
						switch (g.u8() % 4) {
							case 0:
								g.u16();
								break;
							case 1:
								g.u32();
								break;
							case 2:
								g.str(32);
								break;
							default:
								g.skip(g.u8());
								break;
						}
					}
				} catch (const ProtocolError &) {
				}
			}
		}

		void checkCrypto() {
			check(crypto::init(), "sodium init");

			const crypto::Salt salt = crypto::randomSalt();
			auto a = crypto::deriveKeys("correct horse", salt);
			auto b = crypto::deriveKeys("correct horse", salt);
			auto c = crypto::deriveKeys("battery staple", salt);
			check(a && b && c, "key derivation");
			if (!a || !b || !c) {
				return;
			}
			check(a->clientToServer == b->clientToServer && a->serverToClient == b->serverToClient, "same password, same keys");
			check(a->clientToServer != c->clientToServer, "other password, other keys");
			check(a->clientToServer != a->serverToClient, "directions use different keys");

			crypto::Pusher push(a->clientToServer);
			crypto::Puller pull(b->clientToServer, push.header());
			const std::vector<uint8_t> m1 = makeFrame(Msg::Chat, { 1, 2, 3 });
			const std::vector<uint8_t> m2 = makeFrame(Msg::Cursor, { 9 });
			const auto c1 = push.encrypt(m1);
			const auto c2 = push.encrypt(m2);
			check(c1.size() == m1.size() + crypto::kOverhead, "ciphertext overhead");

			auto p1 = pull.decrypt(c1.data(), c1.size());
			auto p2 = pull.decrypt(c2.data(), c2.size());
			check(p1 && *p1 == m1 && p2 && *p2 == m2, "stream round trip");

			// Wrong key, tampering and reordering must all fail authentication.
			crypto::Puller wrongKey(c->clientToServer, push.header());
			check(!wrongKey.decrypt(c1.data(), c1.size()), "wrong key rejected");

			crypto::Pusher push2(a->clientToServer);
			crypto::Puller pull2(a->clientToServer, push2.header());
			auto d1 = push2.encrypt(m1);
			d1[d1.size() / 2] ^= 0x01;
			check(!pull2.decrypt(d1.data(), d1.size()), "tampering rejected");

			crypto::Pusher push3(a->clientToServer);
			crypto::Puller pull3(a->clientToServer, push3.header());
			push3.encrypt(m1);
			auto e2 = push3.encrypt(m2);
			check(!pull3.decrypt(e2.data(), e2.size()), "reordering rejected");

			check(!pull.decrypt(c1.data(), 3), "short ciphertext rejected");
		}

		// A snapshot as the host would send it, built by hand.
		std::string makeSnapshotBytes(const std::string &otbm) {
			ByteWriter w;
			w.u16(1);
			w.blob("Test map");
			w.blob(otbm);
			w.blob("<spawns/>");
			w.blob("");
			w.blob("");
			w.blob("");
			w.blob("<comments/>");

			uLongf bound = compressBound(static_cast<uLong>(w.buffer.size()));
			std::string out(4 + bound, '\0');
			const uint32_t size = static_cast<uint32_t>(w.buffer.size());
			out[0] = static_cast<char>(size >> 24);
			out[1] = static_cast<char>(size >> 16);
			out[2] = static_cast<char>(size >> 8);
			out[3] = static_cast<char>(size);
			compress2(reinterpret_cast<Bytef*>(out.data() + 4), &bound, w.buffer.data(), static_cast<uLong>(w.buffer.size()), 6);
			out.resize(4 + bound);
			return out;
		}

		void checkSnapshot() {
			std::string error;
			Snapshot parsed;
			const std::string good = makeSnapshotBytes("OTBM-DATA");
			check(parseSnapshot(good, parsed, error) && parsed.data.otbm == "OTBM-DATA" && parsed.data.monsters == "<spawns/>" && parsed.mapName == "Test map", "snapshot round trip");

			// Truncated or corrupted data must fail cleanly, never crash.
			for (size_t cut = 0; cut < good.size(); cut += std::max<size_t>(1, good.size() / 40)) {
				Snapshot s;
				std::string e;
				check(!parseSnapshot(good.substr(0, cut), s, e), "truncated snapshot rejected");
			}
			std::mt19937 rng(99);
			for (int i = 0; i < 300; ++i) {
				std::string garbage(1 + rng() % 200, '\0');
				for (auto &c : garbage) {
					c = static_cast<char>(rng());
				}
				Snapshot s;
				std::string e;
				parseSnapshot(garbage, s, e); // result does not matter, only that it returns
			}

			// A header promising far more data than there is must not be believed.
			std::string lying = good;
			lying[0] = 0x7F;
			Snapshot s;
			check(!parseSnapshot(lying, s, error), "lying snapshot size rejected");
		}

		// A tile with everything but items (those need the item database, which a headless run
		// does not load): house, flags, zones, spawns, monsters and an npc.
		void checkTileCodec() {
			Map map;
			VirtualIOMap io((MapVersion()));
			const Position pos(100, 100, 7);

			Tile* tile = map.allocator(map.createTileL(pos));
			tile->house_id = 12;
			tile->setMapFlags(TILESTATE_PROTECTIONZONE | TILESTATE_NOPVP);
			tile->addZone(3);
			tile->addZone(9);
			tile->spawnMonster = newd SpawnMonster(4);
			auto* monster = newd Monster("Rat");
			monster->setSpawnMonsterTime(60);
			monster->setDirection(SOUTH);
			monster->setWeight(2);
			tile->addMonster(monster);
			tile->spawnNpc = newd SpawnNpc(2);
			auto* npc = newd Npc("Eryn");
			npc->setSpawnNpcTime(30);
			npc->setDirection(WEST);
			tile->npc = npc;

			const std::string bytes = encodeTile(tile, io);
			check(!bytes.empty(), "a populated tile encodes to something");

			Tile* copy = decodeTile(map, pos, bytes, io);
			check(encodeTile(copy, io) == bytes, "tile codec round trip");
			check(copy->house_id == 12 && copy->zones.size() == 2 && copy->monsters.size() == 1 && copy->npc && copy->spawnMonster && copy->spawnMonster->getSize() == 4, "tile codec keeps every field");

			Tile* blank = decodeTile(map, pos, std::string(), io);
			check(encodeTile(blank, io).empty() && encodeTile(nullptr, io).empty(), "blank tile and missing tile encode the same");

			// Every truncation must be refused, never read past the buffer, never leak.
			for (size_t cut = 0; cut < bytes.size(); ++cut) {
				check(throwsProtocolError([&] { delete decodeTile(map, pos, bytes.substr(0, cut), io); }) || cut == 0, "truncated tile rejected");
			}
			std::mt19937 rng(7);
			for (int i = 0; i < 500; ++i) {
				std::string garbage(rng() % 96, '\0');
				for (auto &c : garbage) {
					c = static_cast<char>(rng());
				}
				try {
					delete decodeTile(map, pos, garbage, io);
				} catch (const ProtocolError &) {
				}
			}

			delete tile;
			delete copy;
			delete blank;
		}

		void checkJournal() {
			Journal journal;
			std::string error;
			check(journal.open(":memory:", error), "journal opens");

			const Position a(10, 10, 7);
			const Position b(11, 10, 7);
			const JournalEntry first = journal.append("Ana", 0xE91E63, 9, "Draw", { { a, "old", "mid" } });
			const JournalEntry second = journal.append("Ana", 0xE91E63, 9, "Draw", { { a, "mid", "new" }, { b, "", "x" } });
			check(first.id != 0 && first.id == second.id, "strokes of one user coalesce into one entry");
			check(second.tileCount == 2, "coalesced entry counts both tiles");

			const std::vector<JournalTile> tiles = journal.tiles(second.id);
			bool keepsFirstBefore = false;
			for (const JournalTile &tile : tiles) {
				keepsFirstBefore = keepsFirstBefore || (tile.pos == a && tile.before == "old" && tile.after == "new");
			}
			check(keepsFirstBefore, "first before and last after are kept");

			const JournalEntry other = journal.append("Bia", 0x2196F3, 9, "Draw", { { b, "x", "y" } });
			check(other.id != first.id, "another user gets another entry");
			check(journal.append("Ana", 0, 9, "Draw", { { a, "same", "same" } }).id == 0, "a tile that did not change is not recorded");

			check(journal.list(0, 10, "").size() == 2 && journal.list(0, 10, "Bia").size() == 1, "list and user filter");
			check(journal.list(0, 10, "")[0].id == other.id, "newest first");

			journal.setState(first.id, EntryState::Reverted);
			// A reverted entry must not keep growing.
			const JournalEntry after = journal.append("Ana", 0xE91E63, 9, "Draw", { { a, "new", "newer" } });
			check(after.id != first.id, "a reverted entry stops coalescing");
			check(journal.appendInfo("Ana", 0, "Houses (1)").state == static_cast<int>(EntryState::Info), "info entries");
		}

		// A real handshake over loopback: the right password gets in (and the host learns which of
		// its passwords was used), a wrong one is turned away.
		struct Handshake {
			std::mutex mutex;
			std::condition_variable changed;
			bool serverFrame = false;
			size_t keyIndex = 0;
			bool clientReply = false;
			bool serverClosed = false;
			Connection::Ptr server;
			Connection::Ptr client;
		};

		std::shared_ptr<Handshake> attempt(asio::io_context &io, asio::ip::tcp::acceptor &acceptor, const crypto::Salt &salt, const std::vector<std::shared_ptr<const crypto::SessionKeys>> &keysets, const std::string &password) {
			auto state = std::make_shared<Handshake>();

			auto socket = std::make_shared<asio::ip::tcp::socket>(io);
			acceptor.async_accept(*socket, [&io, socket, state, salt, keysets](const std::error_code &error) {
				if (error) {
					return;
				}
				Connection::Callbacks callbacks;
				callbacks.onFrame = [state](const Connection::Ptr &conn, std::vector<uint8_t>) {
					{
						std::lock_guard<std::mutex> lock(state->mutex);
						state->serverFrame = true;
						state->keyIndex = conn->keyIndex();
					}
					conn->send(Msg::Bye); // the answer
					state->changed.notify_all();
				};
				callbacks.onClosed = [state](const Connection::Ptr &, const std::string &) {
					{
						std::lock_guard<std::mutex> lock(state->mutex);
						state->serverClosed = true;
					}
					state->changed.notify_all();
				};
				auto conn = Connection::makeServer(io, std::move(*socket), salt, keysets, callbacks);
				{
					std::lock_guard<std::mutex> lock(state->mutex);
					state->server = conn;
				}
				conn->start();
			});

			Connection::Callbacks callbacks;
			callbacks.onReady = [](const Connection::Ptr &conn) { conn->send(Msg::Chat, { 1, 2 }); };
			callbacks.onFrame = [state](const Connection::Ptr &, std::vector<uint8_t>) {
				{
					std::lock_guard<std::mutex> lock(state->mutex);
					state->clientReply = true;
				}
				state->changed.notify_all();
			};
			state->client = Connection::makeClient(io, "127.0.0.1", acceptor.local_endpoint().port(), password, callbacks);
			state->client->start();

			std::unique_lock<std::mutex> lock(state->mutex);
			state->changed.wait_for(lock, std::chrono::seconds(30), [&] { return (state->serverFrame && state->clientReply) || state->serverClosed; });
			return state;
		}

		void checkHandshake() {
			if (!NetworkConnection::getInstance().start()) {
				check(false, "network service starts");
				return;
			}
			asio::io_context &io = NetworkConnection::getInstance().get_service();
			asio::ip::tcp::acceptor acceptor(io, asio::ip::tcp::endpoint(asio::ip::address_v4::loopback(), 0));

			const crypto::Salt salt = crypto::randomSalt();
			std::vector<std::shared_ptr<const crypto::SessionKeys>> keysets;
			for (const char* password : { "editor password", "viewer password" }) {
				auto keys = crypto::deriveKeys(password, salt);
				check(keys.has_value(), "handshake keys");
				if (!keys) {
					return;
				}
				keysets.push_back(std::make_shared<const crypto::SessionKeys>(*keys));
			}

			auto editor = attempt(io, acceptor, salt, keysets, "editor password");
			check(editor->serverFrame && editor->clientReply && editor->keyIndex == 0, "the first password gets in as key set 0");
			auto viewer = attempt(io, acceptor, salt, keysets, "viewer password");
			check(viewer->serverFrame && viewer->clientReply && viewer->keyIndex == 1, "the second password gets in as key set 1");
			auto wrong = attempt(io, acceptor, salt, keysets, "not the password");
			check(!wrong->serverFrame && wrong->serverClosed, "a wrong password is turned away");

			for (auto &probe : { editor, viewer, wrong }) {
				if (probe->client) {
					probe->client->close("done");
				}
				if (probe->server) {
					probe->server->close("done");
				}
			}
		}

		void checkComments() {
			MapComments::setAuthorOverride("Ana");
			MapComments comments;
			comments.setIdPrefix(3);
			const Position pos(10, 10, 7);
			const uint32_t rootId = comments.add(pos, "first", 0, "Bia", 2).id;
			comments.add(pos, "reply", rootId);
			check((rootId >> 20) == 3, "comment ids carry the user prefix");
			check(comments.all().size() == 2 && comments.at(pos) && comments.at(pos)->id == rootId, "a tile's comment is the first of the thread");

			MapComments copy;
			check(copy.loadXml(comments.toXml()) && copy.all().size() == 2, "comments round trip through xml");
			const MapComment* back = copy.get(rootId);
			check(back && back->kind == 2 && back->assignee == "Bia" && back->author == "Ana", "kind, assignee and author survive");

			// ... and through the metadata sync
			Map map;
			map.comments = comments;
			const MetaState state = captureMeta(map);
			const auto entry = state.find({ static_cast<uint8_t>(MetaKind::Comment), std::to_string(rootId) });
			check(entry != state.end() && decodeComment(rootId, entry->second).assignee == "Bia", "comments are part of the metadata state");
			check(throwsProtocolError([&] { decodeComment(rootId, "garbage"); }), "malformed comment rejected");

			check(copy.remove(rootId) && copy.all().empty(), "removing a comment removes its replies");
			MapComments::setAuthorOverride(std::string());
		}

		void checkMeta() {
			MetaState before;
			MetaState after;
			before[{ static_cast<uint8_t>(MetaKind::House), "1" }] = "a";
			before[{ static_cast<uint8_t>(MetaKind::Zone), "Z" }] = "z";
			after[{ static_cast<uint8_t>(MetaKind::House), "1" }] = "b";
			after[{ static_cast<uint8_t>(MetaKind::Town), "2" }] = "t";
			const auto ops = diffMeta(before, after);
			check(ops.size() == 3, "meta diff: update, add and remove");

			ByteWriter w;
			writeMetaOps(w, ops);
			ByteReader r(w.buffer);
			check(readMetaOps(r).size() == 3 && r.remaining() == 0, "meta ops round trip");
			for (size_t cut = 0; cut < w.buffer.size(); ++cut) {
				std::vector<uint8_t> truncated(w.buffer.begin(), w.buffer.begin() + cut);
				check(throwsProtocolError([&] {
						  ByteReader t(truncated);
						  readMetaOps(t);
					  }),
					  "truncated meta ops rejected");
			}
		}

		void checkSanitize() {
			check(sanitizeText("  Ana  ", 32) == "Ana", "trim");
			check(sanitizeText("a\nb\tc", 32) == "a b c", "control characters");
			check(sanitizeText("abcdef", 3) == "abc", "length cap");
			check(sanitizeText(std::string("\xff\xfe", 2), 32).empty(), "invalid UTF-8 rejected");
			check(sanitizeText("", 32).empty(), "empty stays empty");
		}
	}

	bool selfCheck() {
		failures = 0;
		checkReaderWriter();
		checkCrypto();
		checkSanitize();
		checkSnapshot();
		checkTileCodec();
		checkMeta();
		checkComments();
		checkJournal();
		checkHandshake();
		if (failures == 0) {
			std::fprintf(stdout, "collab selfcheck OK\n");
		}
		return failures == 0;
	}

} // namespace collab
