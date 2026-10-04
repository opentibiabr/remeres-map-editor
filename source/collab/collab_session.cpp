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

#include "../map_comments.h"
#include "../net_connection.h"

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
			const Peer &peer = entry.second;
			if (peer.hello && !peer.dropping && peer.conn.get() != except) {
				peer.conn->send(type, payload);
			}
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

	bool Session::startHosting(const std::string &name, uint16_t port, const std::string &password, Role defaultRoleForJoiners, std::string &error) {
		if (active()) {
			error = "A session is already active";
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

		if (protocol != kProtocolVersion || version != __RME_VERSION__) {
			rejectPeer(peer, fmt::format("Version mismatch: the host runs RME {} (protocol {}), you run RME {} (protocol {})", __RME_VERSION__, kProtocolVersion, sanitizeText(version, 32), protocol));
			return;
		}
		if (userList.size() >= kMaxUsers) {
			rejectPeer(peer, "Session is full");
			return;
		}

		std::string cleanName = sanitizeText(name, kMaxName);
		if (cleanName.empty()) {
			cleanName = "User";
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
		peer.conn->send(Msg::Welcome, welcome.buffer);

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
		currentState = State::Joined;
		link->markAuthenticated();
		changed();
	}

	void Session::clientFrame(const std::vector<uint8_t> &frame) {
		ByteReader reader(frame);
		const Msg type = static_cast<Msg>(reader.u8());

		if (currentState == State::Connecting) {
			if (type == Msg::Welcome) {
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

	// ---- teardown ------------------------------------------------------------------------

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
		peers.clear();
		userList.clear();
		remoteCursors.clear();
		selfId = 0;
		currentState = State::Idle;
		++generation; // late callbacks of the old session are ignored
		lastSentPos = Position();
		statusText = message;
		changed();
		if (onCursorsChanged) {
			onCursorsChanged();
		}
	}

} // namespace collab
