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
#include "collab_journal.h"
#include "collab_tile_codec.h"

#include "../action.h"
#include "../editor.h"
#include "../gui.h"
#include "../brush.h"
#include "../iomap.h"
#include "../map_display.h"
#include "../map_tab.h"
#include "../map.h"
#include "../tile.h"
#include "../map_comments.h"
#include "../net_connection.h"
#include "../settings.h"

#include <zlib.h>

#include <wx/filedlg.h>
#include <wx/stdpaths.h>

#include <sodium.h>

#include <ctime>
#include <thread>
#include <fstream>

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

		// Defined with the history code below; startHosting needs it first.
		std::string journalPath(Editor &editor);

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
			if (user.name.empty() || role > static_cast<uint8_t>(Role::Commenter)) {
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

		Position readView(ByteReader &r) {
			const int x = r.u16();
			const int y = r.u16();
			const int z = r.u8();
			if (z > kMaxFloor) {
				throw ProtocolError("invalid position");
			}
			return Position(x, y, z);
		}

		void writeView(ByteWriter &w, const Position &pos) {
			w.u16(static_cast<uint16_t>(pos.x));
			w.u16(static_cast<uint16_t>(pos.y));
			w.u8(static_cast<uint8_t>(pos.z));
		}

		void writePresence(ByteWriter &w, const Presence &presence) {
			w.u8(presence.hasSelection ? 1 : 0);
			if (presence.hasSelection) {
				w.u16(static_cast<uint16_t>(presence.selFrom.x));
				w.u16(static_cast<uint16_t>(presence.selFrom.y));
				w.u16(static_cast<uint16_t>(presence.selTo.x));
				w.u16(static_cast<uint16_t>(presence.selTo.y));
				w.u8(static_cast<uint8_t>(presence.selFrom.z));
			}
			w.str(presence.tool);
			w.u8(presence.typing ? 1 : 0);
		}

		Presence readPresence(ByteReader &r) {
			Presence presence;
			presence.hasSelection = r.u8() != 0;
			if (presence.hasSelection) {
				const int x1 = r.u16();
				const int y1 = r.u16();
				const int x2 = r.u16();
				const int y2 = r.u16();
				const int z = r.u8();
				if (z > kMaxFloor) {
					throw ProtocolError("invalid selection");
				}
				presence.selFrom = Position(std::min(x1, x2), std::min(y1, y2), z);
				presence.selTo = Position(std::max(x1, x2), std::max(y1, y2), z);
			}
			presence.tool = sanitizeText(r.str(256), 40);
			presence.typing = r.u8() != 0;
			presence.updated = std::chrono::steady_clock::now();
			return presence;
		}

		bool samePresence(const Presence &a, const Presence &b) {
			return a.hasSelection == b.hasSelection && (!a.hasSelection || (a.selFrom == b.selFrom && a.selTo == b.selTo)) && a.tool == b.tool && a.typing == b.typing;
		}

		void writeClaimAdd(ByteWriter &w, const Claim &claim) {
			w.u8(0);
			w.u32(claim.id);
			w.u32(claim.ownerId);
			w.u16(static_cast<uint16_t>(claim.from.x));
			w.u16(static_cast<uint16_t>(claim.from.y));
			w.u16(static_cast<uint16_t>(claim.to.x));
			w.u16(static_cast<uint16_t>(claim.to.y));
			w.u8(static_cast<uint8_t>(claim.from.z));
		}

		void writePositions(ByteWriter &w, const std::vector<Position> &positions) {
			const size_t count = std::min<size_t>(positions.size(), 5000);
			w.u16(static_cast<uint16_t>(count));
			for (size_t i = 0; i < count; ++i) {
				writeView(w, positions[i]);
			}
		}

		std::vector<Position> readPositions(ByteReader &r) {
			const uint16_t count = r.u16();
			if (count > 5000) {
				throw ProtocolError("too many positions");
			}
			std::vector<Position> positions;
			positions.reserve(count);
			for (uint16_t i = 0; i < count; ++i) {
				positions.push_back(readView(r));
			}
			return positions;
		}

		// This machine's IPv4 addresses that a participant on the same network can reach.
		std::vector<std::string> localAddresses() {
			std::vector<std::string> found;
			auto add = [&found](const asio::ip::address &address) {
				if (address.is_v4() && !address.is_loopback()) {
					const std::string text = address.to_string();
					if (std::find(found.begin(), found.end(), text) == found.end()) {
						found.push_back(text);
					}
				}
			};

			std::error_code ec;
			{
				// Connecting a UDP socket sends nothing; it only picks the interface that would
				// be used to reach the internet, which is the main LAN address.
				asio::ip::udp::socket probe(io());
				probe.open(asio::ip::udp::v4(), ec);
				if (!ec) {
					probe.connect(asio::ip::udp::endpoint(asio::ip::make_address_v4("8.8.8.8"), 53), ec);
				}
				if (!ec) {
					const auto local = probe.local_endpoint(ec);
					if (!ec) {
						add(local.address());
					}
				}
			}
			const std::string host = asio::ip::host_name(ec);
			if (!ec) {
				asio::ip::tcp::resolver resolver(io());
				for (const auto &entry : resolver.resolve(host, "0", ec)) {
					add(entry.endpoint().address());
				}
			}
			return found;
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

		// One-shot: sends the newest cursor position that the rate limit held back.
		class CursorTimer : public wxTimer {
		public:
			void Notify() override {
				Session::get().onCursorTick();
			}
		};

		class AutosaveTimer : public wxTimer {
		public:
			void Notify() override {
				Session::get().onAutosaveTick();
			}
		};

		class ReconnectTimer : public wxTimer {
		public:
			void Notify() override {
				Session::get().onReconnectTick();
			}
		};

		int64_t steadyMillis() {
			return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
		}

		class ViewTimer : public wxTimer {
		public:
			void Notify() override {
				Session::get().onViewTick();
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
			if (hostRttMs >= 0) {
				statusText += fmt::format(" - {} ms{}", hostRttMs, hostRttMs > 800 ? " (slow connection)" : "");
			}
			statusText += (pending.before.empty() && lastSentSeq.empty()) ? " - synced" : " - syncing...";
		}
		if (g_gui.root) {
			g_gui.UpdateTitle(); // the participant count is in the tab title
		}
		if (onChanged) {
			onChanged();
		}
	}

	int Session::latencyMs(uint32_t userId) const {
		if (currentState == State::Hosting) {
			for (const auto &entry : peers) {
				if (entry.second.hello && entry.second.userId == userId) {
					return entry.second.rttMs;
				}
			}
			return -1;
		}
		return currentState == State::Joined && userId == 0 ? hostRttMs : -1;
	}

	void Session::sendPings() {
		ByteWriter w;
		w.u32(++pingTick);
		w.u64(static_cast<uint64_t>(steadyMillis()));
		if (currentState == State::Hosting) {
			for (auto &entry : peers) {
				if (entry.second.hello && !entry.second.dropping && !entry.second.streaming) {
					entry.second.conn->send(Msg::Ping, w.buffer); // not through sendTo: it must not wait for a download
				}
			}
		} else if (currentState == State::Joined && link) {
			link->send(Msg::Ping, w.buffer);
		}
	}

	void Session::answerPing(Connection &conn, ByteReader &reader) {
		ByteWriter w;
		w.u32(reader.u32());
		w.u64(reader.u64());
		conn.send(Msg::Pong, w.buffer);
	}

	void Session::notePong(Peer* peer, ByteReader &reader) {
		reader.u32();
		const int64_t sent = static_cast<int64_t>(reader.u64());
		const int rtt = static_cast<int>(std::clamp<int64_t>(steadyMillis() - sent, 0, 60000));
		int &slot = peer ? peer->rttMs : hostRttMs;
		const bool noticeable = slot < 0 || std::abs(rtt - slot) > 30 || (rtt > 800) != (slot > 800);
		slot = rtt;
		if (noticeable) {
			changed();
		}
	}

	std::string Session::titleMark(const Editor* editor) const {
		if (!editor || editor != boundEditor() || (currentState != State::Hosting && currentState != State::Joined)) {
			return std::string();
		}
		return fmt::format("[{}{}] ", editor->IsProtectedCopy() ? "Protected " : "", userList.size());
	}

	void Session::pushToast(const std::string &text) {
		toastQueue.push_back({ text, Clock::now() + std::chrono::seconds(5) });
		while (toastQueue.size() > 4) {
			toastQueue.pop_front();
		}
		if (onOverlayChanged) {
			onOverlayChanged();
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
		return actor.role == Role::Admin && target.role != Role::Admin && (newRole == Role::Editor || newRole == Role::Viewer || newRole == Role::Commenter);
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
		} else if (type != Msg::Cursor && type != Msg::View && type != Msg::Presence) {
			peer.deferred.emplace_back(type, payload); // cursors and views are stale by the time the map is out
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
			if (!peer.snapshot) {
				continue; // still being compressed
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
			sendPresenceTo(peer);

			if (peer.resyncAfter && hostEditor) {
				peer.resyncAfter = false;
				beginPeerResync(peer);
				streamTo({ peer.conn });
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
		if (journal && !system) {
			journal->addChat(line.name, line.text);
		}
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

		// An optional second password lets people in as Viewers: its key set is index 1.
		const std::string viewerPassword = hostOptions.viewerPassword;
		if (!viewerPassword.empty() && (viewerPassword.size() < kMinPassword || viewerPassword == password)) {
			error = fmt::format("The viewer password needs at least {} characters and must differ from the main one", kMinPassword);
			return false;
		}

		const crypto::Salt salt = crypto::randomSalt();
		std::vector<std::shared_ptr<const crypto::SessionKeys>> keysets;
		{
			wxBusyCursor busy;
			for (const std::string* candidate : { &password, &viewerPassword }) {
				if (candidate == &viewerPassword && viewerPassword.empty()) {
					break;
				}
				auto derived = crypto::deriveKeys(*candidate, salt);
				if (!derived) {
					error = "Could not derive the session key (out of memory?)";
					return false;
				}
				keysets.push_back(std::make_shared<const crypto::SessionKeys>(*derived));
			}
		}
		if (!hostOptions.viewerPassword.empty()) {
			sodium_memzero(hostOptions.viewerPassword.data(), hostOptions.viewerPassword.size());
			hostOptions.viewerPassword.clear();
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
		lanList = localAddresses();
		claimList.clear();
		nextClaimId = 1;
		acceptor = listener;
		listenPort = port;
		defaultRole = (defaultRoleForJoiners == Role::Viewer || defaultRoleForJoiners == Role::Commenter) ? defaultRoleForJoiners : Role::Editor;
		chatLog.clear();
		userList.clear();
		remoteCursors.clear();
		nextUserId = 1;
		selfId = 0;
		selfName = cleanName;
		MapComments::setAuthorOverride(cleanName);
		editor->getMap().comments.setIdPrefix(0);

		User host;
		host.id = 0;
		host.name = cleanName;
		host.color = MapComments::paletteColor(0);
		host.role = Role::Host;
		userList[0] = host;

		currentState = State::Hosting;
		historyList.clear();
		journal = std::make_unique<Journal>();
		std::string journalError;
		if (!journal->open(journalPath(*editor), journalError)) {
			journal.reset();
			addSystemChat("History is disabled: " + journalError);
		}
		lastSynced = captureMeta(editor->getMap());
		pending = Pending();
		pendingHouseTiles.clear();
		if (!metaTimer) {
			metaTimer = std::make_unique<MetaTimer>();
		}
		metaTimer->Start(500);
		if (!viewTimer) {
			viewTimer = std::make_unique<ViewTimer>();
		}
		lastSentView = Position();
		presenceSent = false;
		viewTimer->Start(250);
		if (currentState == State::Hosting && hostOptions.autosaveMinutes > 0) {
			if (!autosaveTimer) {
				autosaveTimer = std::make_unique<AutosaveTimer>();
			}
			lastAutosave = Clock::now();
			autosaveTimer->Start(30 * 1000);
		}
		acceptLoop(listener, generation, salt, keysets);
		changed();
		return true;
	}

	void Session::acceptLoop(std::shared_ptr<asio::ip::tcp::acceptor> acceptor, uint64_t generation, crypto::Salt salt, std::vector<std::shared_ptr<const crypto::SessionKeys>> keysets) {
		auto socket = std::make_shared<asio::ip::tcp::socket>(io());
		acceptor->async_accept(*socket, [acceptor, generation, salt, keysets, socket](const std::error_code &error) {
			if (error == asio::error::operation_aborted) {
				return;
			}
			if (!error) {
				auto conn = Connection::makeServer(io(), std::move(*socket), salt, keysets, makeCallbacks(generation));
				hop([conn, generation] { Session::get().handleAccepted(conn, generation); });
				conn->start();
			}
			if (acceptor->is_open()) {
				acceptLoop(acceptor, generation, salt, keysets);
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

		// The password was proven by decrypting this frame.
		peer.conn->markAuthenticated();

		User user;
		user.id = nextUserId++;
		user.name = uniqueName(cleanName);
		user.color = pickColor();
		user.role = peer.conn->keyIndex() == 1 ? Role::Viewer : defaultRole; // the viewer password only reads
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
		// Everything that happens from now on waits for the participant until the map is sent.
		peer.streaming = true;
		peer.snapshot.reset();
		peer.snapshotSent = 0;
		for (const auto &entry : claimList) {
			ByteWriter claim;
			writeClaimAdd(claim, entry.second);
			sendTo(peer, Msg::Claims, claim.buffer); // after the map: the peer is still streaming
		}

		ByteWriter joined;
		writeUser(joined, user);
		broadcast(Msg::UserJoined, joined.buffer, peer.conn.get());

		addSystemChat(user.name + " joined");
		pushToast(user.name + " joined");
		changed();

		// Serialized right here, so the map is exactly the state the held-back changes build on.
		const Connection::Ptr joining = peer.conn;
		buildSnapshotAsync([joining](std::shared_ptr<const std::string> snapshot, const std::string &error) {
			Session &session = Session::get();
			auto it = session.peers.find(joining.get());
			if (it == session.peers.end() || it->second.dropping) {
				return;
			}
			if (!snapshot) {
				session.dropPeer(joining, "The host could not prepare the map: " + error);
				return;
			}
			session.startStreaming(it->second, snapshot);
		});
	}

	void Session::invalidateSnapshot() {
		++dataVersion;
		snapshotCache.reset();
	}

	void Session::buildSnapshotAsync(std::function<void(std::shared_ptr<const std::string>, const std::string &)> done) {
		if (!hostEditor) {
			done(nullptr, "no map is open");
			return;
		}
		if (snapshotCache) { // nothing changed since the last participant got it
			done(snapshotCache, std::string());
			return;
		}

		std::string raw;
		std::string error;
		if (!serializeSnapshot(*hostEditor, raw, error)) {
			done(nullptr, error);
			return;
		}

		const uint64_t version = dataVersion;
		const uint64_t gen = generation;
		std::thread([done, raw = std::move(raw), version, gen]() mutable {
			std::string compressed;
			std::string failure;
			std::shared_ptr<const std::string> result;
			if (compressSnapshot(raw, compressed, failure)) {
				result = std::make_shared<const std::string>(std::move(compressed));
			}
			hop([done, result, failure, version, gen] {
				Session &session = Session::get();
				if (gen != session.generation) {
					return; // the session ended meanwhile
				}
				if (result && version == session.dataVersion) {
					session.snapshotCache = result;
				}
				done(result, failure);
			});
		}).detach();
	}

	void Session::beginPeerResync(Peer &peer) {
		peer.conn->send(Msg::FullResync); // what follows is held back until the new map is out
		peer.streaming = true;
		peer.snapshot.reset();
		peer.snapshotSent = 0;
	}

	void Session::streamTo(std::vector<Connection::Ptr> targets) {
		buildSnapshotAsync([targets](std::shared_ptr<const std::string> snapshot, const std::string &error) {
			Session &session = Session::get();
			for (const Connection::Ptr &conn : targets) {
				auto it = session.peers.find(conn.get());
				if (it == session.peers.end() || it->second.dropping) {
					continue;
				}
				if (!snapshot) {
					session.dropPeer(conn, "The host could not resend the map: " + error);
					continue;
				}
				session.startStreaming(it->second, snapshot);
			}
		});
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
				if (role > static_cast<uint8_t>(Role::Commenter)) {
					throw ProtocolError("invalid role");
				}
				applyRole(actor, target, static_cast<Role>(role));
				return;
			}
			case Msg::Kick:
				applyKick(actor, reader.u32());
				return;
			case Msg::Ping:
				answerPing(*peer.conn, reader);
				return;
			case Msg::Pong:
				notePong(&peer, reader);
				return;
			case Msg::View: {
				const Position view = readView(reader);
				remoteViews[actor.id] = view;
				ByteWriter w;
				w.u32(actor.id);
				writeView(w, view);
				broadcast(Msg::View, w.buffer, peer.conn.get());
				if (followId == actor.id) {
					applyFollow(view);
				}
				return;
			}
			case Msg::Presence: {
				const Presence presence = readPresence(reader);
				remotePresence[actor.id] = presence;
				ByteWriter w;
				w.u32(actor.id);
				writePresence(w, presence);
				broadcast(Msg::Presence, w.buffer, peer.conn.get());
				if (onPresenceChanged) {
					onPresenceChanged();
				}
				return;
			}
			case Msg::Summon: {
				const Position target = readView(reader);
				if (actor.role != Role::Host && actor.role != Role::Admin) {
					return;
				}
				ByteWriter w;
				w.u32(actor.id);
				writeView(w, target);
				broadcast(Msg::Summon, w.buffer, peer.conn.get());
				applySummon(actor.name, target);
				return;
			}
			case Msg::ClaimAdd: {
				const int x1 = reader.u16();
				const int y1 = reader.u16();
				const int x2 = reader.u16();
				const int y2 = reader.u16();
				const int z = reader.u8();
				hostAddClaim(actor, Position(x1, y1, z), Position(x2, y2, z));
				return;
			}
			case Msg::ClaimRemove:
				hostRemoveClaim(actor, reader.u32());
				return;
			case Msg::TileBatch:
				hostTileBatch(peer, actor, reader);
				return;
			case Msg::MetaOps:
				hostMetaOps(peer, actor, reader);
				return;
			case Msg::HistoryQuery:
			case Msg::HistoryRevert:
			case Msg::HistoryReapply:
			case Msg::HistoryPreview:
			case Msg::HistoryMark:
			case Msg::HistoryRestore:
			case Msg::HistoryRevertRecent:
				hostHistoryFrame(peer, actor, type, reader);
				return;
			case Msg::SaveRequest:
				if (actor.role == Role::Admin && hostEditor) {
					if (!hostEditor->getMap().hasFile()) {
						addSystemChat(actor.name + " asked the host to save, but the map has no file yet");
					} else {
						addSystemChat(actor.name + " requested a save");
						hostEditor->saveMap(FileName(), true);
					}
				}
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
		reconnectAttempt = 0;
		reconnectHost = trimmedHost.utf8_string();
		reconnectPort = port;
		reconnectPassword = password; // only to rejoin after a dropped connection; wiped when the session ends
		hostRttMs = -1;
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
		MapComments::setAuthorOverride(self() ? self()->name : std::string());
		editor->getMap().comments.setIdPrefix(selfId);
		if (previous) {
			g_gui.CloseEditorTabs(previous);
		}

		currentState = State::Joined;
		reconnectAttempt = 0;
		lastSynced = captureMeta(editor->getMap());
		pending = Pending();
		lastSentSeq.clear();
		pendingHouseTiles.clear();
		if (!metaTimer) {
			metaTimer = std::make_unique<MetaTimer>();
		}
		metaTimer->Start(500);
		if (!viewTimer) {
			viewTimer = std::make_unique<ViewTimer>();
		}
		lastSentView = Position();
		presenceSent = false;
		viewTimer->Start(250);
		if (currentState == State::Hosting && hostOptions.autosaveMinutes > 0) {
			if (!autosaveTimer) {
				autosaveTimer = std::make_unique<AutosaveTimer>();
			}
			lastAutosave = Clock::now();
			autosaveTimer->Start(30 * 1000);
		}
		changed();
	}

	void Session::clientFrame(const std::vector<uint8_t> &frame) {
		ByteReader reader(frame);
		const Msg type = static_cast<Msg>(reader.u8());

		if (type == Msg::Ping) {
			if (link) {
				answerPing(*link, reader);
			}
			return;
		}
		if (type == Msg::Pong) {
			notePong(nullptr, reader);
			return;
		}

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
				if (type == Msg::UserJoined) {
					pushToast(user.name + " joined");
				}
				changed();
				return;
			}
			case Msg::UserLeft: {
				const uint32_t id = reader.u32();
				auto leaving = userList.find(id);
				if (leaving != userList.end()) {
					pushToast(leaving->second.name + " left");
				}
				userList.erase(id);
				remoteCursors.erase(id);
				remoteViews.erase(id);
				remotePresence.erase(id);
				if (followId == id) {
					followId = 0;
				}
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
			case Msg::HistoryPage:
			case Msg::HistoryAppend:
			case Msg::HistoryResult:
			case Msg::HistoryPreviewResult:
				clientHistoryFrame(type, reader);
				return;
			case Msg::View: {
				const uint32_t id = reader.u32();
				const Position view = readView(reader);
				if (id != selfId && userList.count(id)) {
					remoteViews[id] = view;
					if (followId == id) {
						applyFollow(view);
					}
				}
				return;
			}
			case Msg::Presence: {
				const uint32_t id = reader.u32();
				const Presence presence = readPresence(reader);
				if (id != selfId && userList.count(id)) {
					remotePresence[id] = presence;
					if (onPresenceChanged) {
						onPresenceChanged();
					}
					if (onOverlayChanged) {
						onOverlayChanged();
					}
				}
				return;
			}
			case Msg::Summon: {
				const uint32_t id = reader.u32();
				const Position target = readView(reader);
				auto who = userList.find(id);
				applySummon(who != userList.end() ? who->second.name : std::string("Someone"), target);
				return;
			}
			case Msg::Claims:
				clientClaims(reader);
				return;
			case Msg::SaveNotice:
				if (shareMapFlag) {
					hop([] { Session::get().saveLocalCopy(); }); // not inside the network event
				}
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
			remoteViews.erase(peer.userId);
			remotePresence.erase(peer.userId);
			if (followId == peer.userId) {
				followId = 0;
			}
			removeClaimsOf(peer.userId);

			ByteWriter w;
			w.u32(peer.userId);
			broadcast(Msg::UserLeft, w.buffer);
			addSystemChat(name + " left");
			pushToast(name + " left");
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
		if (currentState == State::Connecting && reconnectAttempt > 0) {
			++reconnectAttempt; // this try failed too
			scheduleReconnect(reason);
			return;
		}
		if (currentState == State::Joined && clientEditor && !reconnectPassword.empty()) {
			beginReconnect(reason); // the map stays open (read-only) while we try to get back in
			return;
		}
		if (currentState == State::Connecting) {
			const bool silent = reason == "Connection closed" || reason == "Connection lost" || reason == "Wrong password or corrupted stream";
			resetToIdle(silent ? "Wrong password or not an RME collaboration server." : reason);
		} else {
			resetToIdle("Disconnected: " + reason);
		}
	}

	// ---- reconnecting --------------------------------------------------------------------

	void Session::beginReconnect(const std::string &reason) {
		reconnectAttempt = 1;
		currentState = State::Connecting;
		welcomed = false;
		resyncing = false;
		download = Download();
		pending = Pending();
		lastSentSeq.clear();
		remoteCursors.clear();
		remoteViews.clear();
		remotePresence.clear();
		hostRttMs = -1;
		followId = 0;
		scheduleReconnect(reason);
	}

	void Session::scheduleReconnect(const std::string &reason) {
		constexpr int kMaxAttempts = 8;
		if (reconnectAttempt > kMaxAttempts) {
			resetToIdle("Could not reconnect (" + reason + ")");
			return;
		}
		if (!reconnectTimer) {
			reconnectTimer = std::make_unique<ReconnectTimer>();
		}
		const int delay = std::min(1000 * reconnectAttempt, 8000);
		statusText = fmt::format("Connection lost ({}) - reconnecting in {} s (attempt {} of {}). The map is read-only meanwhile.", reason, delay / 1000, reconnectAttempt, kMaxAttempts);
		reconnectTimer->StartOnce(delay);
		changed();
	}

	void Session::onReconnectTick() {
		if (currentState != State::Connecting || reconnectAttempt == 0 || link) {
			return;
		}
		statusText = fmt::format("Reconnecting to {}:{} (attempt {})...", reconnectHost, reconnectPort, reconnectAttempt);
		++generation;
		link = Connection::makeClient(io(), reconnectHost, reconnectPort, reconnectPassword, makeCallbacks(generation));
		link->start();
		changed();
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
		if (mouseDown == lastSentDown && now - lastSentAt < kLocalCursorInterval) {
			// Too soon: remember it and send it when the interval is over, so the others see
			// where the mouse stopped.
			deferredCursor = { pos, brushSize, mouseDown, true };
			if (!cursorTimer) {
				cursorTimer = std::make_unique<CursorTimer>();
			}
			if (!cursorTimer->IsRunning()) {
				const auto wait = std::chrono::duration_cast<std::chrono::milliseconds>(kLocalCursorInterval - (now - lastSentAt));
				cursorTimer->StartOnce(std::max<int>(1, static_cast<int>(wait.count())));
			}
			return;
		}
		deferredCursor.valid = false;
		sendCursor(pos, brushSize, mouseDown);
	}

	void Session::onCursorTick() {
		if (!deferredCursor.valid || (currentState != State::Hosting && currentState != State::Joined)) {
			return;
		}
		deferredCursor.valid = false;
		if (deferredCursor.pos == lastSentPos && deferredCursor.brushSize == lastSentBrush && deferredCursor.mouseDown == lastSentDown) {
			return;
		}
		sendCursor(deferredCursor.pos, deferredCursor.brushSize, deferredCursor.mouseDown);
	}

	void Session::sendCursor(const Position &pos, uint8_t brushSize, bool mouseDown) {
		lastSentPos = pos;
		lastSentBrush = brushSize;
		lastSentDown = mouseDown;
		lastSentAt = Clock::now();

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
			return currentState == State::Joined && me && canEditMap(me->role);
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
		if (isHost()) {
			invalidateSnapshot();
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
		if (origin == selfId && !applying) {
			warnAboutClaims(batch.before);
		}

		Editor* editor = boundEditor();
		if (!editor || (currentState != State::Hosting && currentState != State::Joined)) {
			return;
		}

		Map &map = editor->getMap();
		VirtualIOMap io(map.getVersion());
		const bool host = isHost();

		ByteWriter records;
		std::vector<Position> framePositions;
		std::vector<JournalTile> journalRows;
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
			const std::string after = encodeTile(map.getTile(pos), io);
			records.blob(after);
			if (host) {
				journalRows.push_back({ pos, entry.second, after });
			}
			framePositions.push_back(pos);
			++count;
			if (count >= kMaxTilesPerFrame || records.buffer.size() >= kMaxTileFrameBytes) {
				emit();
			}
		}
		emit();
		if (host) {
			recordHistory(origin, batch.type, journalRows);
		}
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

	void Session::hostTileBatch(Peer &peer, const User &actor, ByteReader &reader) {
		if (!canEditMap(actor.role) || !hostEditor) {
			return; // their own editor already refuses to edit
		}
		const uint32_t seq = reader.u32();
		const uint8_t type = reader.u8();
		if (type > ACTION_MCP) {
			throw ProtocolError("invalid action type");
		}
		const uint32_t count = reader.u32();
		Map &map = hostEditor->getMap();
		std::vector<Tile*> tiles = readTiles(reader, count, map);

		if (actor.role == Role::Editor && !hostOptions.editorsSpawns) {
			// The host does not let editors touch spawns and creatures: those tiles are not applied,
			// and the editor gets them back the way they really are.
			std::vector<Tile*> allowed;
			std::vector<Position> refused;
			for (Tile* tile : tiles) {
				if (sameCreatures(tile, map.getTile(tile->getPosition()))) {
					allowed.push_back(tile);
				} else {
					refused.push_back(tile->getPosition());
					delete tile;
				}
			}
			applyTiles(allowed, actor.id, seq);
			sendTilesTo(peer, refused);
			return;
		}
		applyTiles(tiles, actor.id, seq);
	}

	void Session::sendTilesTo(Peer &peer, const std::vector<Position> &positions) {
		if (positions.empty() || !hostEditor) {
			return;
		}
		Map &map = hostEditor->getMap();
		VirtualIOMap io(map.getVersion());
		ByteWriter records;
		uint32_t count = 0;
		auto emit = [&] {
			if (count == 0) {
				return;
			}
			ByteWriter frame;
			frame.u32(0); // origin: the host, never the receiver
			frame.u32(0);
			frame.u32(count);
			frame.buffer.insert(frame.buffer.end(), records.buffer.begin(), records.buffer.end());
			sendTo(peer, Msg::TileUpdate, frame.buffer);
			records = ByteWriter();
			count = 0;
		};
		for (const Position &pos : positions) {
			records.u16(static_cast<uint16_t>(pos.x));
			records.u16(static_cast<uint16_t>(pos.y));
			records.u8(static_cast<uint8_t>(pos.z));
			records.blob(encodeTile(map.getTile(pos), io));
			++count;
			if (count >= kMaxTilesPerFrame || records.buffer.size() >= kMaxTileFrameBytes) {
				emit();
			}
		}
		emit();
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
		invalidateSnapshot();
		sendMetaOps(ops, nullptr);
		if (isHost()) {
			recordMetaInfo(selfId, ops);
		}
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
		invalidateSnapshot();
		bool commentsTouched = false;
		for (const MetaOp &op : ops) {
			std::optional<std::string> previous;
			if (op.kind == MetaKind::Comment) {
				commentsTouched = true;
				previous = captureMetaEntry(map, op.kind, op.key);
			}
			applyMetaOp(map, op);
			if (op.kind == MetaKind::Comment && !op.remove) {
				notifyAboutComment(op, previous);
			}
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
		if (commentsTouched && onCommentsChanged) {
			onCommentsChanged();
		}
	}

	void Session::notifyAboutComment(const MetaOp &op, const std::optional<std::string> &previous) {
		const User* me = self();
		if (!me) {
			return;
		}
		const uint32_t id = static_cast<uint32_t>(std::strtoul(op.key.c_str(), nullptr, 10));
		MapComment now;
		std::optional<MapComment> before;
		try {
			now = decodeComment(id, op.data);
			if (previous) {
				before = decodeComment(id, *previous);
			}
		} catch (const ProtocolError &) {
			return;
		}
		if (now.author == me->name) {
			return;
		}

		auto lower = [](std::string text) {
			std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return text;
		};
		const std::string mention = "@" + lower(me->name);
		const bool mentioned = lower(now.text).find(mention) != std::string::npos && (!before || lower(before->text).find(mention) == std::string::npos);
		const bool assigned = now.assignee == me->name && (!before || before->assignee != me->name);
		if (mentioned || assigned) {
			const std::string text = now.author + (mentioned ? " mentioned you in a comment" : " assigned a comment to you");
			pushToast(text);
			ChatLine line;
			line.system = true;
			line.time = nowSeconds();
			line.text = text;
			storeChat(line);
		}
	}

	bool Session::mayApplyMeta(const User &actor, const MetaOp &op) {
		if (actor.role == Role::Host || actor.role == Role::Admin) {
			return true;
		}
		switch (op.kind) {
			case MetaKind::Comment: {
				if (actor.role == Role::Viewer) {
					return false;
				}
				const uint32_t id = static_cast<uint32_t>(std::strtoul(op.key.c_str(), nullptr, 10));
				if (id == 0) {
					return false;
				}
				const MapComment* existing = hostEditor->getMap().comments.get(id);
				if (op.remove) {
					return !existing || existing->author == actor.name;
				}
				const MapComment incoming = decodeComment(id, op.data); // malformed: the sender is dropped
				if (existing && existing->author != actor.name) {
					// Somebody else's comment: only marking it resolved or open is allowed.
					return incoming.author == existing->author && incoming.parent == existing->parent && incoming.text == existing->text && incoming.assignee == existing->assignee && incoming.kind == existing->kind && incoming.pos == existing->pos;
				}
				return incoming.author == actor.name; // no writing in somebody else's name
			}
			case MetaKind::MapProps:
				return actor.role == Role::Editor && hostOptions.editorsProps;
			default:
				return actor.role == Role::Editor && hostOptions.editorsMeta;
		}
	}

	// What the host holds for the refused entries, so the sender's map goes back to it.
	void Session::correctMeta(Peer &peer, const std::vector<MetaOp> &rejected) {
		Map &map = hostEditor->getMap();
		std::vector<MetaOp> fixes;
		for (const MetaOp &op : rejected) {
			MetaOp fix;
			fix.kind = op.kind;
			fix.key = op.key;
			try {
				if (auto current = captureMetaEntry(map, op.kind, op.key)) {
					fix.data = *current;
				} else {
					fix.remove = true;
				}
			} catch (const ProtocolError &) {
				continue;
			}
			fixes.push_back(std::move(fix));
		}
		constexpr size_t kOpsPerFrame = 500;
		for (size_t start = 0; start < fixes.size(); start += kOpsPerFrame) {
			const std::vector<MetaOp> chunk(fixes.begin() + start, fixes.begin() + std::min(fixes.size(), start + kOpsPerFrame));
			ByteWriter w;
			writeMetaOps(w, chunk);
			sendTo(peer, Msg::MetaOps, w.buffer);
		}
	}

	void Session::hostMetaOps(Peer &peer, const User &actor, ByteReader &reader) {
		if (!hostEditor) {
			return;
		}
		const std::vector<MetaOp> ops = readMetaOps(reader);
		std::vector<MetaOp> allowed;
		std::vector<MetaOp> rejected;
		for (const MetaOp &op : ops) {
			(mayApplyMeta(actor, op) ? allowed : rejected).push_back(op);
		}
		if (!allowed.empty()) {
			applyIncomingMeta(allowed);
			sendMetaOps(allowed, peer.conn.get());
			recordMetaInfo(actor.id, allowed);
		}
		if (!rejected.empty()) {
			correctMeta(peer, rejected);
		}
	}

	void Session::clientMetaOps(ByteReader &reader) {
		applyIncomingMeta(readMetaOps(reader));
	}

	// ---- history -------------------------------------------------------------------------

	namespace {
		void writeEntry(ByteWriter &w, const JournalEntry &e) {
			w.u64(static_cast<uint64_t>(e.id));
			w.str(e.user);
			w.u32(e.color);
			w.u8(static_cast<uint8_t>(e.actionType));
			w.str(e.label);
			w.u32(static_cast<uint32_t>(e.created));
			w.u32(e.tileCount);
			w.u8(static_cast<uint8_t>(e.state));
			w.u16(static_cast<uint16_t>(e.center.x));
			w.u16(static_cast<uint16_t>(e.center.y));
			w.u8(static_cast<uint8_t>(e.center.z));
		}

		JournalEntry readEntry(ByteReader &r) {
			JournalEntry e;
			e.id = static_cast<int64_t>(r.u64());
			e.user = sanitizeText(r.str(kMaxName * 4), kMaxName);
			e.color = r.u32() & 0xFFFFFF;
			e.actionType = r.u8();
			e.label = sanitizeText(r.str(256), 64);
			e.created = r.u32();
			e.tileCount = r.u32();
			e.state = r.u8();
			const int x = r.u16();
			const int y = r.u16();
			const int z = r.u8();
			e.center = Position(x, y, z);
			if (e.id <= 0 || e.state > static_cast<int>(EntryState::Info) || z > kMaxFloor) {
				throw ProtocolError("invalid history entry");
			}
			return e;
		}

		std::string journalPath(Editor &editor) {
			const Map &map = editor.getMap();
			if (map.hasFile()) {
				wxFileName file(wxstr(map.getFilename()));
				return nstr(file.GetPathWithSep()) + nstr(file.GetName()) + ".collab.sqlite";
			}
			// A map that was never saved keeps its history in the user data directory.
			const wxString folder = wxStandardPaths::Get().GetUserDataDir() + wxFileName::GetPathSeparator() + "collab";
			wxFileName::Mkdir(folder, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
			const wxString file = folder + wxFileName::GetPathSeparator() + wxDateTime::Now().Format("%Y%m%d-%H%M%S") + ".sqlite";
			return nstr(file); // nstr does not parenthesize its argument
		}
	}

	bool Session::canSeeHistory() const {
		const User* me = self();
		return me && (currentState == State::Hosting || currentState == State::Joined) && (me->role == Role::Host || me->role == Role::Admin);
	}

	void Session::mergeHistory(const std::vector<JournalEntry> &entries, bool replace) {
		if (replace) {
			historyList.clear();
		}
		for (const JournalEntry &entry : entries) {
			auto existing = std::find_if(historyList.begin(), historyList.end(), [&](const JournalEntry &e) { return e.id == entry.id; });
			if (existing != historyList.end()) {
				*existing = entry;
			} else {
				historyList.push_back(entry);
			}
		}
		std::sort(historyList.begin(), historyList.end(), [](const JournalEntry &a, const JournalEntry &b) { return a.id > b.id; });
		if (onHistoryChanged) {
			onHistoryChanged();
		}
	}

	void Session::noteHistoryUser(const std::string &name) {
		if (!name.empty() && std::find(historyUserNames.begin(), historyUserNames.end(), name) == historyUserNames.end()) {
			historyUserNames.push_back(name);
			std::sort(historyUserNames.begin(), historyUserNames.end());
		}
	}

	void Session::publishEntry(const JournalEntry &entry) {
		if (entry.id == 0) {
			return;
		}
		noteHistoryUser(entry.user);
		if (historyFilter.empty() || entry.user == historyFilter) {
			mergeHistory({ entry }, false);
		}

		ByteWriter w;
		writeEntry(w, entry);
		for (auto &item : peers) {
			Peer &peer = item.second;
			auto user = userList.find(peer.userId);
			if (peer.hello && !peer.dropping && user != userList.end() && user->second.role == Role::Admin) {
				sendTo(peer, Msg::HistoryAppend, w.buffer);
			}
		}
	}

	void Session::recordHistory(uint32_t origin, int actionType, const std::vector<JournalTile> &rows) {
		if (!journal) {
			return;
		}
		auto user = userList.find(origin);
		const std::string name = user != userList.end() ? user->second.name : "Unknown";
		const uint32_t color = user != userList.end() ? user->second.color : 0x9E9E9E;
		const std::string label = revertLabel.empty() ? nstr(ActionQueue::labelFor(static_cast<ActionIdentifier>(actionType))) : revertLabel;
		publishEntry(journal->append(name, color, actionType, label, rows));
	}

	void Session::recordMetaInfo(uint32_t origin, const std::vector<MetaOp> &ops) {
		if (!journal || ops.empty()) {
			return;
		}
		static const char* const names[] = { "Houses", "Towns", "Waypoints", "Zones", "Map properties", "Comments" };
		size_t counts[static_cast<size_t>(MetaKind::Count)] = {};
		for (const MetaOp &op : ops) {
			if (op.kind != MetaKind::Comment) { // chatty and not revertable anyway
				++counts[static_cast<size_t>(op.kind)];
			}
		}
		std::string label;
		for (size_t i = 0; i < static_cast<size_t>(MetaKind::Count); ++i) {
			if (counts[i] > 0) {
				label += (label.empty() ? "" : ", ") + std::string(names[i]) + " (" + std::to_string(counts[i]) + ")";
			}
		}
		if (label.empty()) {
			return;
		}
		auto user = userList.find(origin);
		publishEntry(journal->appendInfo(user != userList.end() ? user->second.name : "Unknown", user != userList.end() ? user->second.color : 0x9E9E9E, label));
	}

	Session::RevertOutcome Session::executeRevert(const User &actor, int64_t entryId, bool reapply, bool force, bool dryRun) {
		RevertOutcome outcome;
		if (!journal || !hostEditor || currentState != State::Hosting) {
			outcome.message = "History is not available";
			return outcome;
		}
		if (actor.role != Role::Host && actor.role != Role::Admin) {
			outcome.message = "Only the host and admins can revert";
			return outcome;
		}
		JournalEntry entry;
		if (!journal->get(entryId, entry) || entry.state == static_cast<int>(EntryState::Info)) {
			outcome.message = "That entry cannot be reverted";
			return outcome;
		}

		Map &map = hostEditor->getMap();
		VirtualIOMap io(map.getVersion());
		std::vector<Tile*> tiles;
		for (const JournalTile &row : journal->tiles(entryId)) {
			const std::string &expected = reapply ? row.before : row.after;
			const std::string &target = reapply ? row.after : row.before;
			const std::string current = encodeTile(map.getTile(row.pos), io);
			if (current == target) {
				continue; // already there
			}
			if (current != expected && !force) {
				++outcome.skipped; // somebody changed it after this entry
				if (outcome.conflicts.size() < 5000) {
					outcome.conflicts.push_back(row.pos);
				}
				continue;
			}
			if (dryRun) {
				if (outcome.changes.size() < 5000) {
					outcome.changes.push_back(row.pos);
				}
				++outcome.applied;
				continue;
			}
			try {
				tiles.push_back(decodeTile(map, row.pos, target, io));
			} catch (const ProtocolError &) {
				++outcome.skipped;
			}
		}

		if (dryRun) {
			outcome.ok = true;
			outcome.message = fmt::format("{} would change {} tiles, {} would be skipped (changed later by others)", reapply ? "Reapply" : "Revert", outcome.applied, outcome.skipped);
			return outcome;
		}

		outcome.applied = static_cast<uint32_t>(tiles.size());
		if (!tiles.empty()) {
			revertLabel = fmt::format("{} #{}", reapply ? "Reapply" : "Revert", entryId);
			Action* action = hostEditor->createAction(ACTION_COLLAB_REVERT);
			for (Tile* tile : tiles) {
				action->addChange(newd Change(tile));
			}
			// Applied as the actor, so the history shows who reverted.
			beginApply(actor.id, 0);
			{
				ScopeExit done { [this] { endApply(); } };
				hostEditor->addAction(action);
			}
			revertLabel.clear();
			g_gui.RefreshView();
		}

		const EntryState state = outcome.skipped == 0 ? (reapply ? EntryState::Applied : EntryState::Reverted) : EntryState::PartiallyReverted;
		journal->setState(entryId, state);
		JournalEntry updated;
		if (journal->get(entryId, updated)) {
			publishEntry(updated);
		}

		outcome.ok = true;
		outcome.message = fmt::format("{} {} tiles, {} skipped (changed later by others)", reapply ? "Reapplied" : "Reverted", outcome.applied, outcome.skipped);
		return outcome;
	}

	void Session::requestHistory(int64_t beforeId, const std::string &userFilter) {
		if (!canSeeHistory()) {
			return;
		}
		historyFilter = sanitizeText(userFilter, kMaxName);
		if (currentState == State::Hosting) {
			if (journal) {
				for (const std::string &name : journal->users()) {
					noteHistoryUser(name);
				}
				mergeHistory(journal->list(beforeId, 200, historyFilter), beforeId == 0);
			}
		} else if (link) {
			ByteWriter w;
			w.u64(static_cast<uint64_t>(beforeId));
			w.u16(200);
			w.str(historyFilter);
			link->send(Msg::HistoryQuery, w.buffer);
		}
	}

	void Session::replyHistoryResult(Peer &peer, int64_t entryId, const RevertOutcome &outcome) {
		ByteWriter w;
		w.u64(static_cast<uint64_t>(entryId));
		w.u8(outcome.ok ? 1 : 0);
		w.u32(outcome.applied);
		w.u32(outcome.skipped);
		w.u16(0);
		w.str(outcome.message);
		sendTo(peer, Msg::HistoryResult, w.buffer);
	}

	// Applies tiles as one revert-like action by the actor (it shows up in the history as theirs).
	void Session::applyAsAction(const User &actor, std::vector<Tile*> &tiles, const std::string &label) {
		if (tiles.empty() || !hostEditor) {
			return;
		}
		revertLabel = label;
		Action* action = hostEditor->createAction(ACTION_COLLAB_REVERT);
		for (Tile* tile : tiles) {
			action->addChange(newd Change(tile));
		}
		tiles.clear();
		beginApply(actor.id, 0);
		{
			ScopeExit done { [this] { endApply(); } };
			hostEditor->addAction(action);
		}
		revertLabel.clear();
		g_gui.RefreshView();
	}

	Session::RevertOutcome Session::executeRestore(const User &actor, int64_t pointId) {
		RevertOutcome outcome;
		if (!journal || !hostEditor || currentState != State::Hosting || (actor.role != Role::Host && actor.role != Role::Admin)) {
			outcome.message = "Only the host and admins can restore";
			return outcome;
		}
		JournalEntry point;
		if (!journal->get(pointId, point) || point.actionType != kRestorePoint) {
			outcome.message = "That entry is not a restore point";
			return outcome;
		}

		// Walking the history forward, the first "before" of a tile is how it was at the point.
		const std::vector<int64_t> ids = journal->idsAfter(pointId);
		std::map<Position, std::string> wanted;
		for (int64_t id : ids) {
			for (const JournalTile &row : journal->tiles(id)) {
				wanted.emplace(row.pos, row.before);
			}
		}

		Map &map = hostEditor->getMap();
		VirtualIOMap io(map.getVersion());
		std::vector<Tile*> tiles;
		for (const auto &entry : wanted) {
			if (encodeTile(map.getTile(entry.first), io) == entry.second) {
				continue;
			}
			try {
				tiles.push_back(decodeTile(map, entry.first, entry.second, io));
			} catch (const ProtocolError &) {
				++outcome.skipped;
			}
		}
		outcome.applied = static_cast<uint32_t>(tiles.size());
		applyAsAction(actor, tiles, "Restore #" + std::to_string(pointId));

		for (int64_t id : ids) {
			journal->setState(id, EntryState::Reverted);
			JournalEntry updated;
			if (journal->get(id, updated)) {
				publishEntry(updated);
			}
		}
		outcome.ok = true;
		outcome.message = fmt::format("Restored {} tiles to \"{}\" ({} entries rolled back)", outcome.applied, point.label, ids.size());
		return outcome;
	}

	Session::RevertOutcome Session::executeRevertRecent(const User &actor, const std::string &user, int minutes, bool force) {
		RevertOutcome total;
		if (!journal || !hostEditor || currentState != State::Hosting || (actor.role != Role::Host && actor.role != Role::Admin)) {
			total.message = "Only the host and admins can revert";
			return total;
		}
		if (user.empty() || minutes <= 0) {
			total.message = "Pick a user and a number of minutes";
			return total;
		}

		// Newest first, so that each entry meets the map the way its own edit left it.
		const std::vector<JournalEntry> entries = journal->listSince(user, nowSeconds() - static_cast<int64_t>(minutes) * 60);
		size_t reverted = 0;
		for (const JournalEntry &entry : entries) {
			const RevertOutcome one = executeRevert(actor, entry.id, false, force);
			if (one.ok) {
				++reverted;
				total.applied += one.applied;
				total.skipped += one.skipped;
			}
		}
		total.ok = true;
		total.message = fmt::format("Reverted {} entries by {} from the last {} minutes: {} tiles, {} skipped", reverted, user, minutes, total.applied, total.skipped);
		return total;
	}

	void Session::markRestorePoint(const std::string &name) {
		const std::string clean = sanitizeText(name, 64);
		const User* me = self();
		if (clean.empty() || !me || !canSeeHistory()) {
			return;
		}
		if (currentState == State::Hosting) {
			if (journal) {
				publishEntry(journal->appendInfo(me->name, me->color, "Restore point: " + clean, kRestorePoint));
			}
		} else if (link) {
			ByteWriter w;
			w.str(clean);
			link->send(Msg::HistoryMark, w.buffer);
		}
	}

	void Session::restoreToPoint(int64_t entryId) {
		clearPreview();
		if (currentState == State::Hosting) {
			const User* me = self();
			if (me) {
				const RevertOutcome outcome = executeRestore(*me, entryId);
				if (onHistoryResult) {
					onHistoryResult(outcome.message);
				}
			}
		} else if (canSeeHistory() && link) {
			ByteWriter w;
			w.u64(static_cast<uint64_t>(entryId));
			link->send(Msg::HistoryRestore, w.buffer);
		}
	}

	void Session::revertRecent(const std::string &user, int minutes, bool force) {
		clearPreview();
		if (currentState == State::Hosting) {
			const User* me = self();
			if (me) {
				const RevertOutcome outcome = executeRevertRecent(*me, user, minutes, force);
				if (onHistoryResult) {
					onHistoryResult(outcome.message);
				}
			}
		} else if (canSeeHistory() && link) {
			ByteWriter w;
			w.str(user);
			w.u32(static_cast<uint32_t>(std::max(minutes, 0)));
			w.u8(force ? 1 : 0);
			link->send(Msg::HistoryRevertRecent, w.buffer);
		}
	}

	bool Session::exportHistoryCsv(const std::string &path) {
		if (!canSeeHistory()) {
			return false;
		}
		// The host has all of it; an admin exports what is loaded in the tab.
		const std::vector<JournalEntry> entries = (currentState == State::Hosting && journal) ? journal->list(0, 1000000, std::string()) : historyList;

		auto field = [](std::string text) {
			std::string quoted = "\"";
			for (char c : text) {
				quoted += c == '"' ? std::string("\"\"") : std::string(1, c);
			}
			return quoted + "\"";
		};
		std::ofstream file(path, std::ios::binary | std::ios::trunc);
		if (!file) {
			return false;
		}
		file << "id,time,user,action,tiles,state,x,y,z\r\n";
		static const char* const states[] = { "applied", "reverted", "partial", "info" };
		for (const JournalEntry &entry : entries) {
			char when[32] = {};
			const std::time_t seconds = static_cast<std::time_t>(entry.created);
			std::tm local {};
#ifdef _WIN32
			localtime_s(&local, &seconds);
#else
			localtime_r(&seconds, &local);
#endif
			std::strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S", &local);
			file << entry.id << ',' << when << ',' << field(entry.user) << ',' << field(entry.label) << ',' << entry.tileCount << ',' << states[std::clamp(entry.state, 0, 3)] << ',' << entry.center.x << ',' << entry.center.y << ',' << entry.center.z << "\r\n";
		}
		return static_cast<bool>(file);
	}

	void Session::previewEntry(int64_t entryId, bool reapply, bool force) {
		if (!canSeeHistory()) {
			return;
		}
		if (currentState == State::Hosting) {
			const User* me = self();
			if (!me) {
				return;
			}
			const RevertOutcome outcome = executeRevert(*me, entryId, reapply, force, true);
			previewChangeList = outcome.changes;
			previewConflictList = outcome.conflicts;
			if (onHistoryResult) {
				onHistoryResult(outcome.message);
			}
			if (onOverlayChanged) {
				onOverlayChanged();
			}
		} else if (link) {
			ByteWriter w;
			w.u64(static_cast<uint64_t>(entryId));
			w.u8(reapply ? 1 : 0);
			w.u8(force ? 1 : 0);
			link->send(Msg::HistoryPreview, w.buffer);
		}
	}

	void Session::clearPreview() {
		if (previewChangeList.empty() && previewConflictList.empty()) {
			return;
		}
		previewChangeList.clear();
		previewConflictList.clear();
		if (onOverlayChanged) {
			onOverlayChanged();
		}
	}

	void Session::runHistoryAction(int64_t entryId, bool reapply, bool force) {
		clearPreview();
		if (currentState == State::Hosting) {
			const User* me = self();
			if (me) {
				const RevertOutcome outcome = executeRevert(*me, entryId, reapply, force);
				if (onHistoryResult) {
					onHistoryResult(outcome.message);
				}
			}
		} else if (canSeeHistory() && link) {
			ByteWriter w;
			w.u64(static_cast<uint64_t>(entryId));
			w.u8(force ? 1 : 0);
			link->send(reapply ? Msg::HistoryReapply : Msg::HistoryRevert, w.buffer);
		}
	}

	void Session::revertEntry(int64_t entryId, bool force) {
		runHistoryAction(entryId, false, force);
	}

	void Session::reapplyEntry(int64_t entryId, bool force) {
		runHistoryAction(entryId, true, force);
	}

	void Session::hostHistoryFrame(Peer &peer, const User &actor, Msg type, ByteReader &reader) {
		if (actor.role != Role::Host && actor.role != Role::Admin) {
			return;
		}

		if (type == Msg::HistoryQuery) {
			const auto beforeId = static_cast<int64_t>(reader.u64());
			const int limit = std::min<int>(reader.u16(), 200);
			const std::string filter = sanitizeText(reader.str(kMaxName * 4), kMaxName);
			const std::vector<JournalEntry> entries = journal ? journal->list(beforeId, limit, filter) : std::vector<JournalEntry>();

			ByteWriter w;
			w.u8(beforeId == 0 ? 1 : 0);
			w.u16(static_cast<uint16_t>(entries.size()));
			for (const JournalEntry &entry : entries) {
				writeEntry(w, entry);
			}
			const std::vector<std::string> names = journal ? journal->users() : std::vector<std::string>();
			w.u16(static_cast<uint16_t>(std::min<size_t>(names.size(), kMaxUsers * 4)));
			for (size_t i = 0; i < names.size() && i < kMaxUsers * 4; ++i) {
				w.str(names[i]);
			}
			sendTo(peer, Msg::HistoryPage, w.buffer);
			return;
		}

		if (type == Msg::HistoryMark) {
			const std::string name = sanitizeText(reader.str(256), 64);
			if (!name.empty() && journal) {
				const JournalEntry mark = journal->appendInfo(actor.name, actor.color, "Restore point: " + name, kRestorePoint);
				publishEntry(mark);
			}
			return;
		}
		if (type == Msg::HistoryRestore) {
			const auto pointId = static_cast<int64_t>(reader.u64());
			replyHistoryResult(peer, pointId, executeRestore(actor, pointId));
			return;
		}
		if (type == Msg::HistoryRevertRecent) {
			const std::string user = sanitizeText(reader.str(kMaxName * 4), kMaxName);
			const int minutes = static_cast<int>(std::min<uint32_t>(reader.u32(), 24 * 60));
			const bool recentForce = reader.u8() != 0;
			replyHistoryResult(peer, 0, executeRevertRecent(actor, user, minutes, recentForce));
			return;
		}
		if (type == Msg::HistoryPreview) {
			const auto previewId = static_cast<int64_t>(reader.u64());
			const bool reapply = reader.u8() != 0;
			const bool previewForce = reader.u8() != 0;
			const RevertOutcome preview = executeRevert(actor, previewId, reapply, previewForce, true);

			ByteWriter w;
			writePositions(w, preview.changes);
			writePositions(w, preview.conflicts);
			w.str(preview.message);
			sendTo(peer, Msg::HistoryPreviewResult, w.buffer);
			return;
		}

		const auto entryId = static_cast<int64_t>(reader.u64());
		const bool force = reader.u8() != 0;
		const RevertOutcome outcome = executeRevert(actor, entryId, type == Msg::HistoryReapply, force);

		ByteWriter w;
		w.u64(static_cast<uint64_t>(entryId));
		w.u8(outcome.ok ? 1 : 0);
		w.u32(outcome.applied);
		w.u32(outcome.skipped);
		w.u16(static_cast<uint16_t>(outcome.conflicts.size()));
		for (const Position &pos : outcome.conflicts) {
			w.u16(static_cast<uint16_t>(pos.x));
			w.u16(static_cast<uint16_t>(pos.y));
			w.u8(static_cast<uint8_t>(pos.z));
		}
		w.str(outcome.message);
		sendTo(peer, Msg::HistoryResult, w.buffer);
	}

	void Session::clientHistoryFrame(Msg type, ByteReader &reader) {
		if (!canSeeHistory()) {
			return;
		}
		if (type == Msg::HistoryAppend) {
			const JournalEntry entry = readEntry(reader);
			noteHistoryUser(entry.user);
			if (historyFilter.empty() || entry.user == historyFilter) {
				mergeHistory({ entry }, false);
			}
			return;
		}
		if (type == Msg::HistoryPage) {
			const bool replace = reader.u8() != 0;
			const uint16_t count = reader.u16();
			if (count > 200) {
				throw ProtocolError("history page too long");
			}
			std::vector<JournalEntry> entries;
			for (uint16_t i = 0; i < count; ++i) {
				entries.push_back(readEntry(reader));
			}
			const uint16_t names = reader.u16();
			if (names > kMaxUsers * 4) {
				throw ProtocolError("too many history users");
			}
			for (uint16_t i = 0; i < names; ++i) {
				noteHistoryUser(sanitizeText(reader.str(kMaxName * 4), kMaxName));
			}
			mergeHistory(entries, replace);
			return;
		}

		if (type == Msg::HistoryPreviewResult) {
			previewChangeList = readPositions(reader);
			previewConflictList = readPositions(reader);
			const std::string summary = sanitizeText(reader.str(1024), 300);
			if (onHistoryResult && !summary.empty()) {
				onHistoryResult(summary);
			}
			if (onOverlayChanged) {
				onOverlayChanged();
			}
			return;
		}

		// HistoryResult
		clearPreview();
		reader.u64(); // entry id
		reader.u8(); // ok
		reader.u32(); // applied
		reader.u32(); // skipped
		const uint16_t conflicts = reader.u16();
		reader.skip(static_cast<size_t>(conflicts) * 5); // positions, the message says how many
		const std::string message = sanitizeText(reader.str(1024), 300);
		if (onHistoryResult && !message.empty()) {
			onHistoryResult(message);
		}
	}

	// ---- views and summons ---------------------------------------------------------------

	Presence Session::currentPresence() const {
		Presence presence;
		Editor* editor = boundEditor();
		if (editor && editor->hasSelection()) {
			const Position from = editor->getSelection().minPosition();
			const Position to = editor->getSelection().maxPosition();
			if (from.z == to.z) { // a selection over several floors is not shown
				presence.hasSelection = true;
				presence.selFrom = from;
				presence.selTo = to;
			}
		}
		if (g_gui.IsSelectionMode()) {
			presence.tool = "Selecting";
		} else if (Brush* brush = g_gui.GetCurrentBrush()) {
			presence.tool = sanitizeText(brush->getName(), 40);
		}
		presence.typing = everTyped && Clock::now() - lastTyping < std::chrono::seconds(3);
		return presence;
	}

	void Session::onLocalTyping() {
		everTyped = true;
		lastTyping = Clock::now();
	}

	void Session::sendPresenceTo(Peer &peer) {
		for (const auto &entry : remoteViews) {
			if (entry.first == peer.userId) {
				continue;
			}
			ByteWriter w;
			w.u32(entry.first);
			writeView(w, entry.second);
			peer.conn->send(Msg::View, w.buffer);
		}
		for (const auto &entry : remotePresence) {
			if (entry.first == peer.userId) {
				continue;
			}
			ByteWriter w;
			w.u32(entry.first);
			writePresence(w, entry.second);
			peer.conn->send(Msg::Presence, w.buffer);
		}
		if (presenceSent) { // the host's own
			ByteWriter w;
			w.u32(selfId);
			writePresence(w, lastSentPresence);
			peer.conn->send(Msg::Presence, w.buffer);
		}
		const Position view = lastSentView;
		if (view.isValid()) {
			ByteWriter w;
			w.u32(selfId);
			writeView(w, view);
			peer.conn->send(Msg::View, w.buffer);
		}
	}

	Position Session::currentView() const {
		MapTab* tab = g_gui.GetCurrentMapTab();
		Editor* editor = boundEditor();
		if (!tab || !editor || tab->GetEditor() != editor) {
			return Position();
		}
		const Position center = tab->GetScreenCenterPosition();
		return Position(center.x, center.y, tab->GetCanvas()->GetFloor());
	}

	void Session::follow(uint32_t userId) {
		if (userId == selfId || userList.find(userId) == userList.end()) {
			return;
		}
		followId = userId;
		auto view = remoteViews.find(userId);
		if (view != remoteViews.end()) {
			applyFollow(view->second);
		}
		changed();
	}

	void Session::stopFollowing() {
		if (followId != 0) {
			followId = 0;
			changed();
		}
	}

	void Session::applyFollow(const Position &view) {
		g_gui.SetScreenCenterPosition(view, false);
		lastFollowApplied = currentView(); // where the view really ended up (map edges, rounding)
	}

	void Session::applySummon(const std::string &who, const Position &target) {
		g_gui.SetScreenCenterPosition(target);
		lastFollowApplied = currentView();

		ChatLine line;
		line.system = true;
		line.time = nowSeconds();
		line.text = who + " brought everyone to " + std::to_string(target.x) + ", " + std::to_string(target.y) + ", " + std::to_string(target.z);
		storeChat(line);
	}

	void Session::summonAll() {
		const User* me = self();
		const Position view = currentView();
		if (!me || !view.isValid() || (me->role != Role::Host && me->role != Role::Admin)) {
			return;
		}
		ByteWriter w;
		if (currentState == State::Hosting) {
			w.u32(selfId);
			writeView(w, view);
			broadcast(Msg::Summon, w.buffer);
		} else if (currentState == State::Joined && link) {
			writeView(w, view);
			link->send(Msg::Summon, w.buffer);
		}
	}

	// The host saves by itself every few minutes while somebody works with it.
	void Session::onAutosaveTick() {
		if (currentState != State::Hosting || !hostEditor || hostOptions.autosaveMinutes <= 0 || userList.size() < 2) {
			return;
		}
		if (Clock::now() - lastAutosave < std::chrono::minutes(hostOptions.autosaveMinutes)) {
			return;
		}
		Map &map = hostEditor->getMap();
		if (!map.hasFile() || !map.hasChanged()) {
			return; // nothing to write, or nowhere to write it
		}
		lastAutosave = Clock::now();
		hostEditor->saveMap(FileName(), false);
		addSystemChat("The host's map was saved automatically");
	}

	void Session::onViewTick() {
		if (currentState != State::Hosting && currentState != State::Joined) {
			return;
		}
		if (!toastQueue.empty()) {
			const size_t before = toastQueue.size();
			while (!toastQueue.empty() && toastQueue.front().until <= Clock::now()) {
				toastQueue.pop_front();
			}
			if (toastQueue.size() != before && onOverlayChanged) {
				onOverlayChanged();
			}
		}
		const Position view = currentView();
		if (!view.isValid()) {
			return;
		}

		// Moving the camera yourself ends following.
		if (followId != 0 && (std::abs(view.x - lastFollowApplied.x) > 3 || std::abs(view.y - lastFollowApplied.y) > 3 || view.z != lastFollowApplied.z)) {
			stopFollowing();
		}

		if (view != lastSentView) {
			lastSentView = view;
			ByteWriter w;
			if (currentState == State::Hosting) {
				w.u32(selfId);
				writeView(w, view);
				broadcast(Msg::View, w.buffer);
			} else if (link) {
				writeView(w, view);
				link->send(Msg::View, w.buffer);
			}
		}

		if (++pingCounter >= 16) { // every 4 seconds
			pingCounter = 0;
			sendPings();
		}

		// Selection, tool and typing, sent when something changed.
		const Presence presence = currentPresence();
		if (!presenceSent || !samePresence(presence, lastSentPresence)) {
			lastSentPresence = presence;
			presenceSent = true;
			ByteWriter w;
			if (currentState == State::Hosting) {
				w.u32(selfId);
				writePresence(w, presence);
				broadcast(Msg::Presence, w.buffer);
			} else if (link) {
				writePresence(w, presence);
				link->send(Msg::Presence, w.buffer);
			}
		}

		// Somebody who stopped typing without telling us (connection trouble) does not type forever.
		bool expired = false;
		for (auto &entry : remotePresence) {
			if (entry.second.typing && Clock::now() - entry.second.updated > std::chrono::seconds(5)) {
				entry.second.typing = false;
				expired = true;
			}
		}
		if (expired && onPresenceChanged) {
			onPresenceChanged();
		}
	}

	// ---- reserved areas ------------------------------------------------------------------

	void Session::claimArea(const Position &a, const Position &b) {
		const User* me = self();
		if (!me || !canEditMap(me->role) || a.z != b.z) {
			return;
		}
		if (currentState == State::Hosting) {
			hostAddClaim(*me, a, b);
		} else if (currentState == State::Joined && link) {
			ByteWriter w;
			w.u16(static_cast<uint16_t>(a.x));
			w.u16(static_cast<uint16_t>(a.y));
			w.u16(static_cast<uint16_t>(b.x));
			w.u16(static_cast<uint16_t>(b.y));
			w.u8(static_cast<uint8_t>(a.z));
			link->send(Msg::ClaimAdd, w.buffer);
		}
	}

	void Session::releaseMyClaims() {
		const User* me = self();
		if (!me) {
			return;
		}
		std::vector<uint32_t> mine;
		for (const auto &entry : claimList) {
			if (entry.second.ownerId == selfId) {
				mine.push_back(entry.first);
			}
		}
		for (uint32_t id : mine) {
			if (currentState == State::Hosting) {
				hostRemoveClaim(*me, id);
			} else if (link) {
				ByteWriter w;
				w.u32(id);
				link->send(Msg::ClaimRemove, w.buffer);
			}
		}
	}

	void Session::hostAddClaim(const User &actor, const Position &a, const Position &b) {
		constexpr int kMaxSide = 2048;
		constexpr size_t kMaxPerUser = 8;
		if (!canEditMap(actor.role) || !a.isValid() || !b.isValid() || a.z != b.z) {
			return;
		}
		const Position from(std::min(a.x, b.x), std::min(a.y, b.y), a.z);
		const Position to(std::max(a.x, b.x), std::max(a.y, b.y), a.z);
		if (to.x - from.x >= kMaxSide || to.y - from.y >= kMaxSide) {
			return;
		}
		size_t owned = 0;
		for (const auto &entry : claimList) {
			owned += entry.second.ownerId == actor.id ? 1 : 0;
		}
		if (owned >= kMaxPerUser) {
			return;
		}

		Claim claim;
		claim.id = nextClaimId++;
		claim.ownerId = actor.id;
		claim.ownerName = actor.name;
		claim.color = actor.color;
		claim.from = from;
		claim.to = to;
		claimList[claim.id] = claim;

		ByteWriter w;
		writeClaimAdd(w, claim);
		broadcast(Msg::Claims, w.buffer);
		if (onOverlayChanged) {
			onOverlayChanged();
		}
	}

	void Session::hostRemoveClaim(const User &actor, uint32_t claimId) {
		auto it = claimList.find(claimId);
		if (it == claimList.end()) {
			return;
		}
		if (it->second.ownerId != actor.id && actor.role != Role::Host && actor.role != Role::Admin) {
			return;
		}
		claimList.erase(it);

		ByteWriter w;
		w.u8(1);
		w.u32(claimId);
		broadcast(Msg::Claims, w.buffer);
		if (onOverlayChanged) {
			onOverlayChanged();
		}
	}

	void Session::removeClaimsOf(uint32_t userId) {
		std::vector<uint32_t> ids;
		for (const auto &entry : claimList) {
			if (entry.second.ownerId == userId) {
				ids.push_back(entry.first);
			}
		}
		for (uint32_t id : ids) {
			claimList.erase(id);
			ByteWriter w;
			w.u8(1);
			w.u32(id);
			broadcast(Msg::Claims, w.buffer);
		}
		if (!ids.empty() && onOverlayChanged) {
			onOverlayChanged();
		}
	}

	void Session::clientClaims(ByteReader &reader) {
		const uint8_t op = reader.u8();
		if (op == 1) {
			claimList.erase(reader.u32());
		} else if (op == 0) {
			Claim claim;
			claim.id = reader.u32();
			claim.ownerId = reader.u32();
			const int x1 = reader.u16();
			const int y1 = reader.u16();
			const int x2 = reader.u16();
			const int y2 = reader.u16();
			const int z = reader.u8();
			claim.from = Position(std::min(x1, x2), std::min(y1, y2), z);
			claim.to = Position(std::max(x1, x2), std::max(y1, y2), z);
			if (z > kMaxFloor || !claim.from.isValid()) {
				throw ProtocolError("invalid area");
			}
			auto owner = userList.find(claim.ownerId);
			claim.ownerName = owner != userList.end() ? owner->second.name : "?";
			claim.color = owner != userList.end() ? owner->second.color : 0x9E9E9E;
			claimList[claim.id] = claim;
		} else {
			throw ProtocolError("unknown claim operation");
		}
		if (onOverlayChanged) {
			onOverlayChanged();
		}
	}

	void Session::warnAboutClaims(const std::map<Position, std::string> &tiles) {
		if (claimList.empty()) {
			return;
		}
		const Clock::time_point now = Clock::now();
		if (now - lastClaimWarning < std::chrono::seconds(2)) {
			return;
		}
		size_t checked = 0;
		for (const auto &tile : tiles) {
			if (++checked > 5000) {
				break;
			}
			const Position &pos = tile.first;
			for (const auto &entry : claimList) {
				const Claim &claim = entry.second;
				if (claim.ownerId != selfId && pos.z == claim.from.z && pos.x >= claim.from.x && pos.x <= claim.to.x && pos.y >= claim.from.y && pos.y <= claim.to.y) {
					lastClaimWarning = now;
					g_gui.SetStatusText(wxstr("You are editing inside the area reserved by " + claim.ownerName));
					return;
				}
			}
		}
	}

	// ---- saving --------------------------------------------------------------------------

	void Session::onHostSaved(Editor* editor) {
		if (currentState != State::Hosting || editor != hostEditor || !saveOnParticipantsFlag) {
			return;
		}
		ByteWriter w;
		w.str(sessionMapName.substr(0, 255));
		broadcast(Msg::SaveNotice, w.buffer);
	}

	void Session::requestSave() {
		const User* me = self();
		if (currentState == State::Joined && link && me && me->role == Role::Admin) {
			link->send(Msg::SaveRequest);
		}
	}

	void Session::saveLocalCopy() {
		if (!clientEditor || !shareMapFlag || currentState != State::Joined) {
			return;
		}
		if (localSavePath.empty()) {
			wxFileDialog dialog(g_gui.root, "Save a local copy of the shared map", wxEmptyString, wxstr(sessionMapName), "OTBM map (*.otbm)|*.otbm", wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
			if (dialog.ShowModal() != wxID_OK) {
				return; // skipped this time, the next save of the host asks again
			}
			localSavePath = nstr(dialog.GetPath());
		}

		clientEditor->saveMap(FileName(wxstr(localSavePath)), true);
		ChatLine line;
		line.system = true;
		line.time = nowSeconds();
		line.text = "Saved locally to " + localSavePath;
		storeChat(line);
	}

	// ---- whole-map operations ------------------------------------------------------------

	void Session::onWholeMapOperation(Editor* editor) {
		if (currentState != State::Hosting || editor != hostEditor || resyncScheduled) {
			return;
		}
		resyncScheduled = true;
		hop([] { Session::get().runResync(); }); // after the operation that called us returns
	}

	void Session::runResync() {
		resyncScheduled = false;
		if (currentState != State::Hosting || !hostEditor) {
			return;
		}
		// The snapshot already contains everything that is waiting to be sent.
		pending = Pending();
		lastSynced = captureMeta(hostEditor->getMap());

		invalidateSnapshot();
		std::vector<Connection::Ptr> targets;
		for (auto &entry : peers) {
			Peer &peer = entry.second;
			if (!peer.hello || peer.dropping) {
				continue;
			}
			if (peer.streaming) {
				peer.resyncAfter = true; // it gets the new copy when the current download ends
				continue;
			}
			beginPeerResync(peer);
			targets.push_back(peer.conn);
		}
		if (!targets.empty()) {
			streamTo(std::move(targets));
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
		MapComments::setAuthorOverride(std::string());
		if (pumpTimer) {
			pumpTimer->Stop();
		}
		if (metaTimer) {
			metaTimer->Stop();
		}
		if (cursorTimer) {
			cursorTimer->Stop();
		}
		if (viewTimer) {
			viewTimer->Stop();
		}
		if (autosaveTimer) {
			autosaveTimer->Stop();
		}
		if (reconnectTimer) {
			reconnectTimer->Stop();
		}
		reconnectAttempt = 0;
		if (!reconnectPassword.empty()) {
			sodium_memzero(reconnectPassword.data(), reconnectPassword.size());
			reconnectPassword.clear();
		}
		hostRttMs = -1;
		deferredCursor.valid = false;
		followId = 0;
		remoteViews.clear();
		remotePresence.clear();
		presenceSent = false;
		everTyped = false;
		claimList.clear();
		lanList.clear();
		previewChangeList.clear();
		previewConflictList.clear();
		journal.reset();
		historyList.clear();
		historyUserNames.clear();
		historyFilter.clear();
		localSavePath.clear();
		revertLabel.clear();
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
