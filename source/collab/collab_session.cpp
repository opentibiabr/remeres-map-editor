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

#include "collab_session.h"

#include "collab_snapshot.h"
#include "collab_tile_codec.h"

#include "../action.h"
#include "../editor.h"
#include "../gui.h"
#include "../iomap.h"
#include "../map.h"
#include "../tile.h"
#include "../map_comments.h"
#include "../net_connection.h"

#include <zlib.h>

#include <sodium.h>

#include <ctime>

namespace collab {

	namespace {
		using Clock = std::chrono::steady_clock;

		constexpr auto kCursorMinInterval = std::chrono::milliseconds(1000 / kMaxCursorsPerSecond);
		constexpr auto kLocalCursorInterval = std::chrono::milliseconds(40); // below the host's limit
		constexpr int kMaxFloor = 15;

		asio::io_context &io() {
			return NetworkConnection::getInstance().get_service();
		}

		// Network callbacks run on the network thread, the session lives on the GUI thread.
		template <typename F>
		void hop(F fn) {
			if (wxTheApp) {
				wxTheApp->CallAfter(std::move(fn));
			}
		}

		int64_t nowSeconds() {
			return static_cast<int64_t>(std::time(nullptr));
		}

		void writeUser(ByteWriter &w, const User &user) {
			w.u32(user.id);
			w.str(user.name);
			w.u32(user.color);
			w.u8(static_cast<uint8_t>(user.role));
		}

		User readUser(ByteReader &r) {
			User user;
			user.id = r.u32();
			user.name = sanitizeText(r.str(kMaxName * 4), kMaxName);
			user.color = r.u32() & 0xFFFFFF;
			const uint8_t role = r.u8();
			if (user.name.empty() || role > static_cast<uint8_t>(Role::Viewer)) {
				throw ProtocolError("invalid user");
			}
			user.role = static_cast<Role>(role);
			return user;
		}

		struct CursorPayload {
			Position pos;
			uint8_t brushSize;
			bool mouseDown;
		};

		CursorPayload readCursor(ByteReader &r) {
			const int x = r.u16();
			const int y = r.u16();
			const int z = r.u8();
			const uint8_t brush = r.u8();
			const uint8_t flags = r.u8();
			if (z > kMaxFloor) {
				throw ProtocolError("invalid cursor");
			}
			return { Position(x, y, z), std::min<uint8_t>(brush, 15), (flags & 1) != 0 };
		}

		void writeCursor(ByteWriter &w, const Position &pos, uint8_t brushSize, bool mouseDown) {
			w.u16(static_cast<uint16_t>(pos.x));
			w.u16(static_cast<uint16_t>(pos.y));
			w.u8(static_cast<uint8_t>(pos.z));
			w.u8(brushSize);
			w.u8(mouseDown ? 1 : 0);
		}
	}

	std::string sanitizeText(const std::string &text, size_t maxChars) {
		if (text.empty()) {
			return std::string();
		}
		wxString w = wxString::FromUTF8(text.data(), text.size());
		if (w.empty()) {
			return std::string(); // not valid UTF-8
		}
		for (size_t i = 0; i < w.length(); ++i) {
			if (w[i].GetValue() < 0x20 || w[i].GetValue() == 0x7F) {
				w[i] = ' ';
			}
		}
		w.Trim(true).Trim(false);
		if (w.length() > maxChars) {
			w = w.Left(maxChars);
		}
		return w.utf8_string();
	}

	namespace {
		// Fires the snapshot pump while a participant is downloading the map.
		class PumpTimer : public wxTimer {
		public:
			void Notify() override {
				Session::get().onPumpTick();
			}
		};

		class MetaTimer : public wxTimer {
		public:
			void Notify() override {
				Session::get().onMetaTick();
			}
		};

		struct ScopeExit {
			std::function<void()> fn;
			~ScopeExit() {
				fn();
			}
		};

		constexpr size_t kStreamHighWater = 4 * 1024 * 1024; // queued bytes before pausing a download
		constexpr int kChunksPerTick = 8;
	}

	Session::Session() = default;
	Session::~Session() = default;

	Session &Session::get() {
		static Session session;
		return session;
	}

	// ---- helpers -------------------------------------------------------------------------

	Connection::Callbacks Session::makeCallbacks(uint64_t generation) {
		Connection::Callbacks callbacks;
		callbacks.onReady = [generation](const Connection::Ptr &conn) {
			hop([conn, generation] { Session::get().handleReady(conn, generation); });
		};
		callbacks.onFrame = [generation](const Connection::Ptr &conn, std::vector<uint8_t> frame) {
			hop([conn, generation, frame = std::move(frame)] { Session::get().handleFrame(conn, frame, generation); });
		};
		callbacks.onClosed = [generation](const Connection::Ptr &conn, const std::string &reason) {
			hop([conn, generation, reason] { Session::get().handleClosed(conn, reason, generation); });
		};
		return callbacks;
	}

	void Session::changed() {
		if (currentState == State::Hosting) {
			statusText = fmt::format("Hosting on port {} - encrypted - {} user(s)", listenPort, userList.size());
		} else if (currentState == State::Joined) {
			const User* me = self();
			statusText = fmt::format("Connected to host - role {}", me ? roleName(me->role) : "?");
		}
		if (onChanged) {
			onChanged();
		}
	}

	const User* Session::self() const {
		auto it = userList.find(selfId);
		return it == userList.end() ? nullptr : &it->second;
	}

	bool Session::mayChangeRole(const User &actor, const User &target, Role newRole) {
		if (target.role == Role::Host || actor.id == target.id || newRole == Role::Host) {
			return false;
		}
		if (actor.role == Role::Host) {
			return true;
		}
		return actor.role == Role::Admin && target.role != Role::Admin && (newRole == Role::Editor || newRole == Role::Viewer);
	}

	bool Session::mayKick(const User &actor, const User &target) {
		if (target.role == Role::Host || actor.id == target.id) {
			return false;
		}
		return actor.role == Role::Host || (actor.role == Role::Admin && target.role != Role::Admin);
	}

	bool Session::canManage(const User &target) const {
		const User* me = self();
		return me && active() && mayKick(*me, target);
	}

	uint32_t Session::pickColor() const {
		for (size_t i = 0; i < MapComments::kPaletteSize; ++i) {
			const uint32_t color = MapComments::paletteColor(i);
			bool used = false;
			for (const auto &entry : userList) {
				used = used || entry.second.color == color;
			}
			if (!used) {
				return color;
			}
		}
		return MapComments::paletteColor(nextUserId);
	}

	std::string Session::uniqueName(const std::string &wanted) const {
		auto taken = [this](const std::string &name) {
			for (const auto &entry : userList) {
				if (entry.second.name == name) {
					return true;
				}
			}
			return false;
		};
		if (!taken(wanted)) {
			return wanted;
		}
		for (int n = 2;; ++n) {
			const std::string suffix = fmt::format(" ({})", n);
			wxString base = wxString::FromUTF8(wanted.c_str());
			base = base.Left(kMaxName - suffix.size());
			std::string candidate = base.utf8_string() + suffix;
			if (!taken(candidate)) {
				return candidate;
			}
		}
	}

	Session::Peer* Session::peerForUser(uint32_t userId) {
		for (auto &entry : peers) {
			if (entry.second.hello && entry.second.userId == userId) {
				return &entry.second;
			}
		}
		return nullptr;
	}

	void Session::broadcast(Msg type, const std::vector<uint8_t> &payload, const Connection* except) {
		for (auto &entry : peers) {
			Peer &peer = entry.second;
			if (peer.hello && !peer.dropping && peer.conn.get() != except) {
				sendTo(peer, type, payload);
			}
		}
	}

	void Session::sendTo(Peer &peer, Msg type, const std::vector<uint8_t> &payload) {
		if (!peer.streaming) {
			peer.conn->send(type, payload);
		} else if (type != Msg::Cursor) {
			peer.deferred.emplace_back(type, payload); // cursors are stale by the time the map is out
		}
	}

	// ---- map download (host side) --------------------------------------------------------

	void Session::startStreaming(Peer &peer, std::shared_ptr<const std::string> snapshot) {
		const size_t total = snapshot->size();
		peer.snapshot = std::move(snapshot);
		peer.snapshotSent = 0;
		peer.streaming = true;

		ByteWriter begin;
		begin.u32(static_cast<uint32_t>(total));
		begin.u32(static_cast<uint32_t>((total + kSnapshotChunk - 1) / kSnapshotChunk));
		peer.conn->send(Msg::SnapshotBegin, begin.buffer);

		if (!pumpTimer) {
			pumpTimer = std::make_unique<PumpTimer>();
		}
		if (!pumpTimer->IsRunning()) {
			pumpTimer->Start(30);
		}
		pumpSnapshots();
	}

	void Session::onPumpTick() {
		pumpSnapshots();
	}

	void Session::pumpSnapshots() {
		bool any = false;
		for (auto &entry : peers) {
			Peer &peer = entry.second;
			if (!peer.streaming) {
				continue;
			}
			if (peer.dropping) {
				peer.streaming = false;
				peer.snapshot.reset();
				continue;
			}

			const std::string &data = *peer.snapshot;
			if (peer.conn->queuedBytes() < kStreamHighWater) {
				for (int i = 0; i < kChunksPerTick && peer.snapshotSent < data.size(); ++i) {
					const size_t n = std::min(kSnapshotChunk, data.size() - peer.snapshotSent);
					const auto* begin = reinterpret_cast<const uint8_t*>(data.data()) + peer.snapshotSent;
					peer.conn->send(Msg::SnapshotChunk, std::vector<uint8_t>(begin, begin + n));
					peer.snapshotSent += n;
				}
			}

			if (peer.snapshotSent < data.size()) {
				any = true;
				continue;
			}

			ByteWriter end;
			end.u32(static_cast<uint32_t>(crc32(0, reinterpret_cast<const Bytef*>(data.data()), static_cast<uInt>(data.size()))));
			peer.conn->send(Msg::SnapshotEnd, end.buffer);
			peer.streaming = false;
			peer.snapshot.reset();
			for (auto &frame : peer.deferred) {
				peer.conn->send(frame.first, frame.second);
			}
			peer.deferred.clear();

			if (peer.resyncAfter && hostEditor) {
				peer.resyncAfter = false;
				std::string fresh;
				std::string error;
				if (buildSnapshot(*hostEditor, fresh, error)) {
					resyncPeer(peer, std::make_shared<const std::string>(std::move(fresh)));
					any = true;
				}
			}
		}
		if (!any && pumpTimer) {
			pumpTimer->Stop();
		}
	}

	// ---- chat ----------------------------------------------------------------------------

	void Session::storeChat(const ChatLine &line) {
		chatLog.push_back(line);
		if (chatLog.size() > 1000) {
			chatLog.erase(chatLog.begin(), chatLog.begin() + 200);
		}
		if (onChat) {
			onChat(line);
		}
	}

	void Session::postChat(uint32_t userId, const std::string &text, bool system) {
		ChatLine line;
		line.userId = userId;
		line.text = text;
		line.system = system;
		line.time = nowSeconds();
		auto it = userList.find(userId);
		if (!system && it != userList.end()) {
			line.name = it->second.name;
			line.color = it->second.color;
		}

		ByteWriter w;
		w.u32(line.userId);
		w.str(line.name);
		w.u32(line.color);
		w.u32(static_cast<uint32_t>(line.time));
		w.u8(system ? 1 : 0);
		w.str(line.text);
		broadcast(Msg::ChatMsg, w.buffer);
		storeChat(line);
	}

	void Session::addSystemChat(const std::string &text) {
		if (currentState == State::Hosting) {
			postChat(0, text, true);
		}
	}

	void Session::sendChat(const std::string &raw) {
		const std::string text = sanitizeText(raw, kMaxChat);
		if (text.empty()) {
			return;
		}
		if (currentState == State::Hosting) {
			postChat(selfId, text, false);
		} else if (currentState == State::Joined && link) {
			ByteWriter w;
			w.str(text);
			link->send(Msg::Chat, w.buffer);
		}
	}

	// ---- hosting -------------------------------------------------------------------------

	bool Session::startHosting(Editor* editor, const std::string &name, uint16_t port, const std::string &password, Role defaultRoleForJoiners, bool shareMap, bool saveOnParticipants, std::string &error) {
		if (active()) {
			error = "A session is already active";
			return false;
		}
		if (!editor || editor->IsCollabClient()) {
			error = "Open the map you want to share first";
			return false;
		}
		if (password.size() < kMinPassword) {
			error = fmt::format("The password must have at least {} characters", kMinPassword);
			return false;
		}

		std::string cleanName = sanitizeText(name, kMaxName);
		if (cleanName.empty()) {
			cleanName = "Host";
		}

		const crypto::Salt salt = crypto::randomSalt();
		std::shared_ptr<const crypto::SessionKeys> keys;
		{
			wxBusyCursor busy;
			auto derived = crypto::deriveKeys(password, salt);
			if (!derived) {
				error = "Could not derive the session key (out of memory?)";
				return false;
			}
			keys = std::make_shared<const crypto::SessionKeys>(*derived);
		}

		NetworkConnection &network = NetworkConnection::getInstance();
		if (!network.start()) {
			error = "Could not start the network service";
			return false;
		}

		auto listener = std::make_shared<asio::ip::tcp::acceptor>(io());
		try {
			const asio::ip::tcp::endpoint endpoint(asio::ip::tcp::v4(), port);
			listener->open(endpoint.protocol());
			listener->set_option(asio::ip::tcp::acceptor::reuse_address(true));
			listener->bind(endpoint);
			listener->listen();
		} catch (const std::exception &e) {
			error = fmt::format("Could not listen on port {} ({})", port, e.what());
			return false;
		}

		++generation;
		hostEditor = editor;
		shareMapFlag = shareMap;
		saveOnParticipantsFlag = shareMap && saveOnParticipants;
		sessionMapName = editor->getMap().getName();
		acceptor = listener;
		listenPort = port;
		defaultRole = defaultRoleForJoiners == Role::Viewer ? Role::Viewer : Role::Editor;
		chatLog.clear();
		userList.clear();
		remoteCursors.clear();
		nextUserId = 1;
		selfId = 0;
		selfName = cleanName;

		User host;
		host.id = 0;
		host.name = cleanName;
		host.color = MapComments::paletteColor(0);
		host.role = Role::Host;
		userList[0] = host;

		currentState = State::Hosting;
		lastSynced = captureMeta(editor->getMap());
		pending = Pending();
		pendingHouseTiles.clear();
		if (!metaTimer) {
			metaTimer = std::make_unique<MetaTimer>();
		}
		metaTimer->Start(500);
		acceptLoop(listener, generation, salt, keys);
		changed();
		return true;
	}

	void Session::acceptLoop(std::shared_ptr<asio::ip::tcp::acceptor> acceptor, uint64_t generation, crypto::Salt salt, std::shared_ptr<const crypto::SessionKeys> keys) {
		auto socket = std::make_shared<asio::ip::tcp::socket>(io());
		acceptor->async_accept(*socket, [acceptor, generation, salt, keys, socket](const std::error_code &error) {
			if (error == asio::error::operation_aborted) {
				return;
			}
			if (!error) {
				auto conn = Connection::makeServer(io(), std::move(*socket), salt, keys, makeCallbacks(generation));
				hop([conn, generation] { Session::get().handleAccepted(conn, generation); });
				conn->start();
			}
			if (acceptor->is_open()) {
				acceptLoop(acceptor, generation, salt, keys);
			}
		});
	}

	void Session::handleAccepted(const Connection::Ptr &conn, uint64_t gen) {
		if (gen != generation || currentState != State::Hosting || peers.size() >= kMaxUsers * 2) {
			conn->close("Not accepting connections");
			return;
		}
		Peer peer;
		peer.conn = conn;
		peers[conn.get()] = peer;
	}

	void Session::rejectPeer(Peer &peer, const std::string &reason) {
		ByteWriter w;
		w.str(reason);
		peer.dropping = true;
		peer.conn->send(Msg::Reject, w.buffer);
		peer.conn->close(reason);
	}

	void Session::dropPeer(const Connection::Ptr &conn, const std::string &reason) {
		auto it = peers.find(conn.get());
		if (it == peers.end()) {
			return;
		}
		if (it->second.hello && !it->second.dropping) {
			ByteWriter w;
			w.str(reason);
			conn->send(Msg::Bye, w.buffer);
		}
		it->second.dropping = true;
		conn->close(reason);
	}

	void Session::hostHello(Peer &peer, ByteReader &reader) {
		const std::string name = reader.str(kMaxName * 4);
		const std::string version = reader.str(64);
		const uint16_t protocol = reader.u16();
		const uint32_t otbm = reader.u32();

		if (protocol != kProtocolVersion || version != __RME_VERSION__) {
			rejectPeer(peer, fmt::format("Version mismatch: the host runs RME {} (protocol {}), you run RME {} (protocol {})", __RME_VERSION__, kProtocolVersion, sanitizeText(version, 32), protocol));
			return;
		}
		if (userList.size() >= kMaxUsers) {
			rejectPeer(peer, "Session is full");
			return;
		}

		if (!hostEditor) {
			rejectPeer(peer, "The host has no map open");
			return;
		}
		const uint32_t hostOtbm = static_cast<uint32_t>(hostEditor->getMap().getVersion().otbm);
		if (otbm != hostOtbm) {
			rejectPeer(peer, fmt::format("Map version mismatch: the host map is OTBM {} but you have OTBM {} loaded. Load the matching client version before joining.", hostOtbm, otbm));
			return;
		}

		std::string cleanName = sanitizeText(name, kMaxName);
		if (cleanName.empty()) {
			cleanName = "User";
		}

		// The password was proven by decrypting this frame; building a big map can take a while.
		peer.conn->markAuthenticated();
		std::string snapshot;
		std::string snapshotError;
		if (!buildSnapshot(*hostEditor, snapshot, snapshotError)) {
			rejectPeer(peer, "The host could not prepare the map: " + snapshotError);
			return;
		}

		User user;
		user.id = nextUserId++;
		user.name = uniqueName(cleanName);
		user.color = pickColor();
		user.role = defaultRole;
		userList[user.id] = user;
		peer.userId = user.id;
		peer.hello = true;
		peer.conn->markAuthenticated();

		ByteWriter welcome;
		welcome.u32(user.id);
		welcome.u32(user.color);
		welcome.u8(static_cast<uint8_t>(user.role));
		welcome.u16(static_cast<uint16_t>(userList.size()));
		for (const auto &entry : userList) {
			writeUser(welcome, entry.second);
		}
		welcome.u8(shareMapFlag ? 1 : 0);
		welcome.u8(saveOnParticipantsFlag ? 1 : 0);
		welcome.str(sessionMapName.substr(0, 255));
		peer.conn->send(Msg::Welcome, welcome.buffer);
		startStreaming(peer, std::make_shared<const std::string>(std::move(snapshot)));

		ByteWriter joined;
		writeUser(joined, user);
		broadcast(Msg::UserJoined, joined.buffer, peer.conn.get());

		addSystemChat(user.name + " joined");
		changed();
	}

	void Session::hostFrame(Peer &peer, const std::vector<uint8_t> &frame) {
		ByteReader reader(frame);
		const Msg type = static_cast<Msg>(reader.u8());

		if (!peer.hello) {
			if (type != Msg::Hello) {
				throw ProtocolError("expected hello");
			}
			hostHello(peer, reader);
			return;
		}

		const auto actorIt = userList.find(peer.userId);
		if (actorIt == userList.end()) {
			throw ProtocolError("unknown user");
		}
		const User actor = actorIt->second;
		const Clock::time_point now = Clock::now();

		switch (type) {
			case Msg::Cursor: {
				const CursorPayload cursor = readCursor(reader);
				if (now - peer.lastCursor < kCursorMinInterval) {
					return; // over the rate limit, drop
				}
				peer.lastCursor = now;
				remoteCursors[actor.id] = { cursor.pos, cursor.brushSize, cursor.mouseDown, now };

				ByteWriter w;
				w.u32(actor.id);
				writeCursor(w, cursor.pos, cursor.brushSize, cursor.mouseDown);
				broadcast(Msg::Cursor, w.buffer, peer.conn.get());
				if (onCursorsChanged) {
					onCursorsChanged();
				}
				return;
			}
			case Msg::Chat: {
				const std::string text = sanitizeText(reader.str(kMaxChat * 4), kMaxChat);
				if (now - peer.chatWindow > std::chrono::seconds(1)) {
					peer.chatWindow = now;
					peer.chatCount = 0;
				}
				if (text.empty() || ++peer.chatCount > kMaxChatPerSecond) {
					return;
				}
				postChat(actor.id, text, false);
				return;
			}
			case Msg::SetRole: {
				const uint32_t target = reader.u32();
				const uint8_t role = reader.u8();
				if (role > static_cast<uint8_t>(Role::Viewer)) {
					throw ProtocolError("invalid role");
				}
				applyRole(actor, target, static_cast<Role>(role));
				return;
			}
			case Msg::Kick:
				applyKick(actor, reader.u32());
				return;
			case Msg::TileBatch:
				hostTileBatch(actor, reader);
				return;
			case Msg::MetaOps:
				hostMetaOps(actor, peer.conn.get(), reader);
				return;
			case Msg::Bye:
				dropPeer(peer.conn, "Left");
				return;
			default:
				throw ProtocolError("unexpected message");
		}
	}

	void Session::applyRole(const User &actor, uint32_t targetId, Role role) {
		auto it = userList.find(targetId);
		if (it == userList.end() || !mayChangeRole(actor, it->second, role)) {
			return;
		}
		it->second.role = role;

		ByteWriter w;
		writeUser(w, it->second);
		broadcast(Msg::UserUpdated, w.buffer);
		addSystemChat(fmt::format("{} is now {}", it->second.name, roleName(role)));
		changed();
	}

	void Session::applyKick(const User &actor, uint32_t targetId) {
		auto it = userList.find(targetId);
		Peer* peer = peerForUser(targetId);
		if (it == userList.end() || !peer || !mayKick(actor, it->second)) {
			return;
		}
		const std::string name = it->second.name;
		dropPeer(peer->conn, "Kicked by " + actor.name);
		addSystemChat(name + " was kicked");
	}

	void Session::setRole(uint32_t userId, Role role) {
		const User* me = self();
		if (!me) {
			return;
		}
		if (currentState == State::Hosting) {
			applyRole(*me, userId, role);
		} else if (currentState == State::Joined && link) {
			ByteWriter w;
			w.u32(userId);
			w.u8(static_cast<uint8_t>(role));
			link->send(Msg::SetRole, w.buffer);
		}
	}

	void Session::kick(uint32_t userId) {
		const User* me = self();
		if (!me) {
			return;
		}
		if (currentState == State::Hosting) {
			applyKick(*me, userId);
		} else if (currentState == State::Joined && link) {
			ByteWriter w;
			w.u32(userId);
			link->send(Msg::Kick, w.buffer);
		}
	}

	// ---- joining -------------------------------------------------------------------------

	void Session::join(const std::string &name, const std::string &host, uint16_t port, const std::string &password) {
		if (active()) {
			return;
		}
		wxString trimmedHost = wxString::FromUTF8(host.c_str());
		trimmedHost.Trim(true).Trim(false);
		if (trimmedHost.empty()) {
			statusText = "Enter the host address";
			changed();
			return;
		}

		NetworkConnection &network = NetworkConnection::getInstance();
		if (!network.start()) {
			statusText = "Could not start the network service";
			changed();
			return;
		}

		selfName = sanitizeText(name, kMaxName);
		if (selfName.empty()) {
			selfName = "User";
		}

		++generation;
		chatLog.clear();
		userList.clear();
		remoteCursors.clear();
		selfId = 0;
		welcomed = false;
		download = Download();
		currentState = State::Connecting;
		statusText = fmt::format("Connecting to {}:{}...", trimmedHost.utf8_string(), port);
		link = Connection::makeClient(io(), trimmedHost.utf8_string(), port, password, makeCallbacks(generation));
		link->start();
		changed();
	}

	void Session::clientWelcome(ByteReader &reader) {
		selfId = reader.u32();
		reader.u32(); // our color, also present in the user list
		reader.u8(); // our role, also present in the user list
		const uint16_t count = reader.u16();
		if (count == 0 || count > kMaxUsers) {
			throw ProtocolError("invalid user list");
		}
		userList.clear();
		for (uint16_t i = 0; i < count; ++i) {
			User user = readUser(reader);
			userList[user.id] = user;
		}
		if (userList.find(selfId) == userList.end()) {
			throw ProtocolError("user list without ourselves");
		}
		shareMapFlag = reader.u8() != 0;
		saveOnParticipantsFlag = reader.u8() != 0;
		sessionMapName = sanitizeText(reader.str(255 * 4), 255);
		welcomed = true;
		link->markAuthenticated();
		statusText = "Receiving the map...";
		changed();
	}

	int Session::downloadPercent() const {
		if (!download.begun || download.total == 0) {
			return 0;
		}
		return static_cast<int>(std::min<uint64_t>(100, static_cast<uint64_t>(download.data.size()) * 100 / download.total));
	}

	void Session::clientSnapshotFrame(Msg type, ByteReader &reader) {
		switch (type) {
			case Msg::SnapshotBegin: {
				if (download.begun) {
					throw ProtocolError("duplicate snapshot");
				}
				download.total = reader.u32();
				download.chunks = reader.u32();
				if (download.total == 0 || download.total > kMaxSnapshot || download.chunks != (download.total + kSnapshotChunk - 1) / kSnapshotChunk) {
					throw ProtocolError("invalid snapshot header");
				}
				download.begun = true;
				download.data.reserve(std::min<size_t>(download.total, 256u * 1024 * 1024));
				return;
			}
			case Msg::SnapshotChunk: {
				if (!download.begun || reader.remaining() == 0 || reader.remaining() > kSnapshotChunk || download.data.size() + reader.remaining() > download.total) {
					throw ProtocolError("invalid snapshot chunk");
				}
				download.data.append(reinterpret_cast<const char*>(reader.current()), reader.remaining());
				++download.received;
				statusText = fmt::format("Receiving the map... {}%", downloadPercent());
				changed();
				return;
			}
			case Msg::SnapshotEnd: {
				const uint32_t expected = reader.u32();
				const auto actual = static_cast<uint32_t>(crc32(0, reinterpret_cast<const Bytef*>(download.data.data()), static_cast<uInt>(download.data.size())));
				if (!download.begun || download.data.size() != download.total || actual != expected) {
					throw ProtocolError("corrupted snapshot");
				}
				finishJoin();
				return;
			}
			default:
				throw ProtocolError("expected the map");
		}
	}

	void Session::finishJoin() {
		Snapshot snapshot;
		std::string error;
		const bool parsed = parseSnapshot(download.data, snapshot, error);
		if (!download.data.empty()) {
			sodium_memzero(download.data.data(), download.data.size());
		}
		download = Download();
		if (!parsed) {
			resetToIdle("Could not read the shared map: " + error);
			return;
		}

		// Whole-map loading blocks the GUI; this is the same cost as opening a file.
		Editor* previous = clientEditor;
		Editor* editor = previous ? g_gui.ReplaceCollabEditor(previous, snapshot, !shareMapFlag) : g_gui.OpenCollabEditor(snapshot, !shareMapFlag);
		snapshot.wipe();
		resyncing = false;
		if (!editor) {
			resetToIdle("Could not open the shared map");
			return;
		}
		clientEditor = editor; // before closing the old tab, so that closing it does not end the session
		if (previous) {
			g_gui.CloseEditorTabs(previous);
		}

		currentState = State::Joined;
		lastSynced = captureMeta(editor->getMap());
		pending = Pending();
		lastSentSeq.clear();
		pendingHouseTiles.clear();
		if (!metaTimer) {
			metaTimer = std::make_unique<MetaTimer>();
		}
		metaTimer->Start(500);
		changed();
	}

	void Session::clientFrame(const std::vector<uint8_t> &frame) {
		ByteReader reader(frame);
		const Msg type = static_cast<Msg>(reader.u8());

		if (currentState == State::Connecting) {
			if (welcomed) {
				clientSnapshotFrame(type, reader);
			} else if (type == Msg::Welcome) {
				clientWelcome(reader);
			} else if (type == Msg::Reject) {
				resetToIdle("Rejected: " + sanitizeText(reader.str(512), 300));
			} else {
				throw ProtocolError("expected welcome");
			}
			return;
		}

		switch (type) {
			case Msg::UserJoined:
			case Msg::UserUpdated: {
				User user = readUser(reader);
				userList[user.id] = user;
				changed();
				return;
			}
			case Msg::UserLeft: {
				const uint32_t id = reader.u32();
				userList.erase(id);
				remoteCursors.erase(id);
				changed();
				if (onCursorsChanged) {
					onCursorsChanged();
				}
				return;
			}
			case Msg::Cursor: {
				const uint32_t id = reader.u32();
				const CursorPayload cursor = readCursor(reader);
				if (id != selfId && userList.count(id)) {
					remoteCursors[id] = { cursor.pos, cursor.brushSize, cursor.mouseDown, Clock::now() };
					if (onCursorsChanged) {
						onCursorsChanged();
					}
				}
				return;
			}
			case Msg::ChatMsg: {
				ChatLine line;
				line.userId = reader.u32();
				line.name = sanitizeText(reader.str(kMaxName * 4), kMaxName);
				line.color = reader.u32() & 0xFFFFFF;
				line.time = reader.u32();
				line.system = reader.u8() != 0;
				line.text = sanitizeText(reader.str(kMaxChat * 4), kMaxChat);
				if (!line.text.empty()) {
					storeChat(line);
				}
				return;
			}
			case Msg::Bye:
				resetToIdle("Disconnected: " + sanitizeText(reader.str(512), 300));
				return;
			case Msg::TileUpdate:
				clientTileUpdate(reader);
				return;
			case Msg::MetaOps:
				clientMetaOps(reader);
				return;
			case Msg::FullResync:
				// The host ran a whole-map operation: a new snapshot follows. Local edits in
				// flight are moot, the new map replaces everything.
				download = Download();
				resyncing = true;
				pending = Pending();
				lastSentSeq.clear();
				return;
			case Msg::SnapshotBegin:
			case Msg::SnapshotChunk:
			case Msg::SnapshotEnd:
				if (!resyncing) {
					throw ProtocolError("unexpected snapshot");
				}
				clientSnapshotFrame(type, reader);
				return;
			default:
				throw ProtocolError("unexpected message");
		}
	}

	// ---- network events (GUI thread) -----------------------------------------------------

	void Session::handleReady(const Connection::Ptr &conn, uint64_t gen) {
		if (gen != generation || conn != link || currentState != State::Connecting) {
			return;
		}
		ByteWriter w;
		w.str(selfName);
		w.str(__RME_VERSION__);
		w.u16(kProtocolVersion);
		w.u32(static_cast<uint32_t>(g_gui.getLoadedMapVersion().otbm));
		conn->send(Msg::Hello, w.buffer);
	}

	void Session::handleFrame(const Connection::Ptr &conn, const std::vector<uint8_t> &frame, uint64_t gen) {
		if (gen != generation) {
			return;
		}
		try {
			if (currentState == State::Hosting) {
				auto it = peers.find(conn.get());
				if (it != peers.end() && !it->second.dropping) {
					hostFrame(it->second, frame);
				}
			} else if (conn == link) {
				clientFrame(frame);
			}
		} catch (const std::exception &e) {
			if (currentState == State::Hosting) {
				dropPeer(conn, std::string("Protocol error: ") + e.what());
			} else {
				resetToIdle(std::string("Protocol error: ") + e.what());
			}
		}
	}

	void Session::handleClosed(const Connection::Ptr &conn, const std::string &reason, uint64_t gen) {
		if (gen != generation) {
			return;
		}

		if (currentState == State::Hosting) {
			auto it = peers.find(conn.get());
			if (it == peers.end()) {
				return;
			}
			const Peer peer = it->second;
			peers.erase(it);
			if (!peer.hello) {
				return;
			}
			auto user = userList.find(peer.userId);
			if (user == userList.end()) {
				return;
			}
			const std::string name = user->second.name;
			userList.erase(user);
			remoteCursors.erase(peer.userId);

			ByteWriter w;
			w.u32(peer.userId);
			broadcast(Msg::UserLeft, w.buffer);
			addSystemChat(name + " left");
			changed();
			if (onCursorsChanged) {
				onCursorsChanged();
			}
			return;
		}

		if (conn != link) {
			return;
		}
		link.reset();
		if (currentState == State::Connecting) {
			const bool silent = reason == "Connection closed" || reason == "Connection lost" || reason == "Wrong password or corrupted stream";
			resetToIdle(silent ? "Wrong password or not an RME collaboration server." : reason);
		} else {
			resetToIdle("Disconnected: " + reason);
		}
	}

	// ---- presence ------------------------------------------------------------------------

	void Session::onLocalCursor(const Position &pos, uint8_t brushSize, bool mouseDown) {
		if (currentState != State::Hosting && currentState != State::Joined) {
			return;
		}
		if (pos.x < 0 || pos.y < 0 || pos.x > 0xFFFF || pos.y > 0xFFFF || pos.z < 0 || pos.z > kMaxFloor) {
			return;
		}
		brushSize = std::min<uint8_t>(brushSize, 15);
		if (pos == lastSentPos && brushSize == lastSentBrush && mouseDown == lastSentDown) {
			return;
		}
		const Clock::time_point now = Clock::now();
		// ponytail: dropped samples are not re-sent, so a remote cursor can stay a tile or two
		// behind when the mouse stops. Add a trailing timer if it bothers anyone.
		if (mouseDown == lastSentDown && now - lastSentAt < kLocalCursorInterval) {
			return;
		}
		lastSentPos = pos;
		lastSentBrush = brushSize;
		lastSentDown = mouseDown;
		lastSentAt = now;

		ByteWriter w;
		if (currentState == State::Hosting) {
			w.u32(selfId);
			writeCursor(w, pos, brushSize, mouseDown);
			broadcast(Msg::Cursor, w.buffer);
		} else if (link) {
			writeCursor(w, pos, brushSize, mouseDown);
			link->send(Msg::Cursor, w.buffer);
		}
	}

	// ---- live tile replication -----------------------------------------------------------

	bool Session::canEdit(const Editor* editor) const {
		if (applying) {
			return true;
		}
		if (editor == clientEditor && editor) {
			const User* me = self();
			return currentState == State::Joined && me && me->role != Role::Viewer;
		}
		return true;
	}

	void Session::onTileCommitted(Editor &editor, int actionType, const Position &pos, const Tile* before) {
		if ((currentState != State::Hosting && currentState != State::Joined) || &editor != boundEditor()) {
			return;
		}
		if (actionType == ACTION_SELECT || actionType == ACTION_UNSELECT) {
			return;
		}
		if (applying && !isHost()) {
			return; // it came from the host
		}

		if (pending.before.find(pos) == pending.before.end()) {
			std::string blob;
			if (isHost()) {
				VirtualIOMap io(editor.getMap().getVersion());
				blob = encodeTile(before, io); // kept for the history, see the journal
			}
			pending.before.emplace(pos, std::move(blob));
		}
		pending.type = actionType;
		if (!applying) {
			scheduleFlush();
		}
	}

	void Session::scheduleFlush() {
		if (flushScheduled) {
			return;
		}
		flushScheduled = true;
		// Many tile swaps of one brush stroke or paste become one batch.
		hop([] {
			Session &session = Session::get();
			session.flushScheduled = false;
			session.flushPending(session.selfId, 0);
		});
	}

	void Session::beginApply(uint32_t origin, uint32_t seq) {
		flushPending(selfId, 0); // earlier local edits keep their own origin
		applying = true;
		applyOrigin = origin;
		applySeq = seq;
	}

	void Session::endApply() {
		flushPending(applyOrigin, applySeq);
		applying = false;
	}

	void Session::flushPending(uint32_t origin, uint32_t originSeq) {
		if (pending.before.empty()) {
			return;
		}
		Pending batch = std::move(pending);
		pending = Pending();

		Editor* editor = boundEditor();
		if (!editor || (currentState != State::Hosting && currentState != State::Joined)) {
			return;
		}

		Map &map = editor->getMap();
		VirtualIOMap io(map.getVersion());
		const bool host = isHost();

		ByteWriter records;
		std::vector<Position> framePositions;
		uint32_t count = 0;
		auto emit = [&] {
			if (count == 0) {
				return;
			}
			ByteWriter frame;
			if (host) {
				frame.u32(origin);
				frame.u32(originSeq);
			} else {
				const uint32_t seq = ++clientSeq;
				frame.u32(seq);
				frame.u8(static_cast<uint8_t>(batch.type));
				for (const Position &framePosition : framePositions) {
					lastSentSeq[framePosition] = seq;
				}
			}
			frame.u32(count);
			frame.buffer.insert(frame.buffer.end(), records.buffer.begin(), records.buffer.end());

			if (host) {
				broadcast(Msg::TileUpdate, frame.buffer);
			} else if (link) {
				link->send(Msg::TileBatch, frame.buffer);
			}
			records = ByteWriter();
			framePositions.clear();
			count = 0;
		};

		for (const auto &entry : batch.before) {
			const Position &pos = entry.first;
			records.u16(static_cast<uint16_t>(pos.x));
			records.u16(static_cast<uint16_t>(pos.y));
			records.u8(static_cast<uint8_t>(pos.z));
			records.blob(encodeTile(map.getTile(pos), io));
			framePositions.push_back(pos);
			++count;
			if (count >= kMaxTilesPerFrame || records.buffer.size() >= kMaxTileFrameBytes) {
				emit();
			}
		}
		emit();
	}

	std::vector<Tile*> Session::readTiles(ByteReader &reader, uint32_t count, Map &map) {
		if (count == 0 || count > kMaxTilesPerFrame) {
			throw ProtocolError("invalid tile count");
		}
		VirtualIOMap io(map.getVersion());
		std::vector<Tile*> tiles;
		try {
			for (uint32_t i = 0; i < count; ++i) {
				const int x = reader.u16();
				const int y = reader.u16();
				const int z = reader.u8();
				const Position pos(x, y, z);
				if (!pos.isValid() || x >= map.getWidth() || y >= map.getHeight()) {
					throw ProtocolError("tile outside the map");
				}
				tiles.push_back(decodeTile(map, pos, reader.blob(1024 * 1024), io));
			}
		} catch (...) {
			for (Tile* tile : tiles) {
				delete tile;
			}
			throw;
		}
		return tiles;
	}

	void Session::applyTiles(std::vector<Tile*> &tiles, uint32_t origin, uint32_t seq) {
		Editor* editor = boundEditor();
		if (!editor || tiles.empty()) {
			for (Tile* tile : tiles) {
				delete tile;
			}
			tiles.clear();
			return;
		}

		std::vector<Position> positions;
		Action* action = editor->createAction(ACTION_REMOTE);
		for (Tile* tile : tiles) {
			positions.push_back(tile->getPosition());
			action->addChange(newd Change(tile));
		}
		tiles.clear();

		beginApply(origin, seq);
		{
			ScopeExit done { [this] { endApply(); } };
			editor->addAction(action); // ACTION_REMOTE: committed, never kept in the undo history
		}

		// Tiles may name a house whose entry has not arrived yet (houses sync a moment later).
		Map &map = editor->getMap();
		for (const Position &pos : positions) {
			const Tile* tile = map.getTile(pos);
			if (tile && tile->getHouseID() != 0 && !map.houses.getHouse(tile->getHouseID())) {
				pendingHouseTiles[tile->getHouseID()].push_back(pos);
			}
		}
		g_gui.RefreshView();
	}

	void Session::hostTileBatch(const User &actor, ByteReader &reader) {
		if (actor.role == Role::Viewer || !hostEditor) {
			return; // the viewer's own editor already refuses to edit
		}
		const uint32_t seq = reader.u32();
		const uint8_t type = reader.u8();
		if (type > ACTION_MCP) {
			throw ProtocolError("invalid action type");
		}
		const uint32_t count = reader.u32();
		std::vector<Tile*> tiles = readTiles(reader, count, hostEditor->getMap());
		applyTiles(tiles, actor.id, seq);
	}

	void Session::clientTileUpdate(ByteReader &reader) {
		if (!clientEditor) {
			return;
		}
		const uint32_t origin = reader.u32();
		const uint32_t originSeq = reader.u32();
		const uint32_t count = reader.u32();
		std::vector<Tile*> tiles = readTiles(reader, count, clientEditor->getMap());

		// Our own newer edit of the same tile is still on its way to the host: applying this older
		// state would flash it back for a moment. Our own echo is applied, it fixes the cases where
		// another user's update crossed it.
		std::vector<Tile*> keep;
		for (Tile* tile : tiles) {
			const Position &pos = tile->getPosition();
			auto it = lastSentSeq.find(pos);
			if (origin == selfId && it != lastSentSeq.end()) {
				if (it->second > originSeq) {
					delete tile;
					continue;
				}
				lastSentSeq.erase(it);
			}
			keep.push_back(tile);
		}
		applyTiles(keep, origin, originSeq);
	}

	// ---- houses, towns, waypoints, zones, map properties ----------------------------------

	void Session::onMetaTick() {
		Editor* editor = boundEditor();
		if (!editor || (currentState != State::Hosting && currentState != State::Joined)) {
			return;
		}
		MetaState current = captureMeta(editor->getMap());
		const std::vector<MetaOp> ops = diffMeta(lastSynced, current);
		if (ops.empty()) {
			return;
		}
		lastSynced = std::move(current);
		sendMetaOps(ops, nullptr);
	}

	void Session::sendMetaOps(const std::vector<MetaOp> &ops, const Connection* except) {
		constexpr size_t kOpsPerFrame = 500;
		for (size_t start = 0; start < ops.size(); start += kOpsPerFrame) {
			const std::vector<MetaOp> chunk(ops.begin() + start, ops.begin() + std::min(ops.size(), start + kOpsPerFrame));
			ByteWriter w;
			writeMetaOps(w, chunk);
			if (isHost()) {
				broadcast(Msg::MetaOps, w.buffer, except);
			} else if (link) {
				link->send(Msg::MetaOps, w.buffer);
			}
		}
	}

	void Session::applyIncomingMeta(const std::vector<MetaOp> &ops) {
		Editor* editor = boundEditor();
		if (!editor) {
			return;
		}
		Map &map = editor->getMap();
		for (const MetaOp &op : ops) {
			applyMetaOp(map, op);
			// What the map holds now is what both sides agree on: do not send it back.
			const auto key = std::make_pair(static_cast<uint8_t>(op.kind), op.key);
			if (auto current = captureMetaEntry(map, op.kind, op.key)) {
				lastSynced[key] = *current;
			} else {
				lastSynced.erase(key);
			}
		}
		attachPendingHouseTiles(map, pendingHouseTiles);
		map.doChange();
		g_gui.RefreshPalettes();
		g_gui.RefreshView();
	}

	void Session::hostMetaOps(const User &actor, const Connection* from, ByteReader &reader) {
		if (actor.role == Role::Viewer || !hostEditor) {
			return;
		}
		const std::vector<MetaOp> ops = readMetaOps(reader);
		applyIncomingMeta(ops);
		sendMetaOps(ops, from);
	}

	void Session::clientMetaOps(ByteReader &reader) {
		applyIncomingMeta(readMetaOps(reader));
	}

	// ---- whole-map operations ------------------------------------------------------------

	void Session::onWholeMapOperation(Editor* editor) {
		if (currentState != State::Hosting || editor != hostEditor || resyncScheduled) {
			return;
		}
		resyncScheduled = true;
		hop([] { Session::get().runResync(); }); // after the operation that called us returns
	}

	void Session::resyncPeer(Peer &peer, std::shared_ptr<const std::string> snapshot) {
		if (peer.streaming) {
			peer.resyncAfter = true; // it gets the new copy when the current download ends
			return;
		}
		peer.conn->send(Msg::FullResync);
		startStreaming(peer, std::move(snapshot));
	}

	void Session::runResync() {
		resyncScheduled = false;
		if (currentState != State::Hosting || !hostEditor) {
			return;
		}
		// The snapshot already contains everything that is waiting to be sent.
		pending = Pending();
		lastSynced = captureMeta(hostEditor->getMap());

		std::shared_ptr<const std::string> snapshot;
		for (auto &entry : peers) {
			Peer &peer = entry.second;
			if (!peer.hello || peer.dropping) {
				continue;
			}
			if (!snapshot) {
				std::string bytes;
				std::string error;
				if (!buildSnapshot(*hostEditor, bytes, error)) {
					addSystemChat("Could not resend the map to participants: " + error);
					return;
				}
				snapshot = std::make_shared<const std::string>(std::move(bytes));
			}
			resyncPeer(peer, snapshot);
		}
	}

	// ---- teardown ------------------------------------------------------------------------

	int Session::confirmStopPrompt() {
		const size_t others = userList.size() > 0 ? userList.size() - 1 : 0;
		if (others == 0) {
			return wxID_YES;
		}
		return g_gui.PopupDialog("Stop the session", wxString::Format("Closing this map stops the collaboration session for %zu participant(s). Continue?", others), wxYES | wxNO);
	}

	bool Session::confirmCloseEditor(Editor* editor) {
		if (currentState != State::Hosting || editor != hostEditor) {
			return true;
		}
		if (confirmStopPrompt() != wxID_YES) {
			return false;
		}
		leave();
		return true;
	}

	bool Session::confirmShutdown() {
		if (currentState != State::Hosting) {
			return true;
		}
		return confirmStopPrompt() == wxID_YES; // leave() runs with the other shutdown steps
	}

	void Session::onEditorClosing(Editor* editor) {
		if (editor == hostEditor) {
			hostEditor = nullptr;
			leave();
		}
		if (editor == clientEditor) {
			clientEditor = nullptr;
			leave();
		}
	}

	void Session::leave() {
		if (!active()) {
			return;
		}
		if (currentState == State::Hosting) {
			for (auto &entry : peers) {
				Peer &peer = entry.second;
				if (peer.hello && !peer.dropping) {
					ByteWriter w;
					w.str("The host closed the session");
					peer.conn->send(Msg::Bye, w.buffer);
				}
				peer.conn->close("Session closed");
			}
			resetToIdle("Session stopped");
		} else {
			if (link && currentState == State::Joined) {
				ByteWriter w;
				w.str("Left");
				link->send(Msg::Bye, w.buffer);
			}
			resetToIdle(currentState == State::Connecting ? "Cancelled" : "Left the session");
		}
	}

	void Session::resetToIdle(const std::string &message) {
		if (acceptor) {
			// Closing from here would race with the accept running on the network thread.
			asio::post(io(), [listener = acceptor] {
				std::error_code ec;
				listener->close(ec);
			});
			acceptor.reset();
		}
		if (link) {
			link->close("Left");
			link.reset();
		}
		if (pumpTimer) {
			pumpTimer->Stop();
		}
		if (metaTimer) {
			metaTimer->Stop();
		}
		pending = Pending();
		lastSentSeq.clear();
		lastSynced.clear();
		pendingHouseTiles.clear();
		clientSeq = 0;
		applying = false;
		resyncing = false;
		peers.clear();
		userList.clear();
		remoteCursors.clear();
		selfId = 0;
		hostEditor = nullptr;
		welcomed = false;
		download = Download();

		std::string finalMessage = message;
		if (clientEditor) {
			Editor* editor = clientEditor;
			clientEditor = nullptr;
			if (editor->IsProtectedCopy()) {
				// Nothing was written to disk, closing the tab discards the only copy.
				g_gui.CloseEditorTabs(editor);
				finalMessage += " - the host did not share this map, so it was discarded";
			} else {
				editor->SetCollabClient(false, false); // keeps working as a local, unsaved map
				finalMessage += " - the map stays open as a local copy";
			}
		}
		currentState = State::Idle;
		++generation; // late callbacks of the old session are ignored
		lastSentPos = Position();
		statusText = finalMessage;
		changed();
		if (onCursorsChanged) {
			onCursorsChanged();
		}
	}

} // namespace collab
