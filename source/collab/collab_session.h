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

#ifndef RME_COLLAB_SESSION_H
#define RME_COLLAB_SESSION_H

#include "collab_connection.h"
#include "collab_protocol.h"

#include "../position.h"

#include <chrono>
#include <functional>
#include <map>
#include <unordered_map>

namespace collab {

	struct User {
		uint32_t id = 0;
		std::string name;
		uint32_t color = 0; // 0xRRGGBB
		Role role = Role::Editor;
	};

	struct RemoteCursor {
		Position pos;
		uint8_t brushSize = 0;
		bool mouseDown = false;
		std::chrono::steady_clock::time_point lastUpdate;
	};

	struct ChatLine {
		uint32_t userId = 0;
		std::string name;
		uint32_t color = 0;
		std::string text;
		int64_t time = 0;
		bool system = false;
	};

	enum class State {
		Idle,
		Hosting,
		Connecting,
		Joined,
	};

	// Valid UTF-8 only, control characters replaced by spaces, trimmed, cut to maxChars
	// characters. Empty result means "reject".
	std::string sanitizeText(const std::string &text, size_t maxChars);

	// The one collaboration session of this editor (hosting or joined). GUI thread only:
	// network callbacks hop here with wxTheApp->CallAfter before touching anything.
	class Session {
	public:
		static Session &get();

		State state() const noexcept {
			return currentState;
		}
		bool active() const noexcept {
			return currentState != State::Idle;
		}
		bool isHost() const noexcept {
			return currentState == State::Hosting;
		}

		// Binds the port and starts accepting. The password is mandatory (kMinPassword).
		bool startHosting(const std::string &name, uint16_t port, const std::string &password, Role defaultRole, std::string &error);
		// Asynchronous: progress and errors arrive through status() / onChanged.
		void join(const std::string &name, const std::string &host, uint16_t port, const std::string &password);
		// Stops hosting, leaves, or cancels a pending join.
		void leave();

		void sendChat(const std::string &text);
		void onLocalCursor(const Position &pos, uint8_t brushSize, bool mouseDown);
		void setRole(uint32_t userId, Role role);
		void kick(uint32_t userId);

		uint32_t myId() const noexcept {
			return selfId;
		}
		const User* self() const;
		bool canManage(const User &target) const;
		const std::map<uint32_t, User> &users() const noexcept {
			return userList;
		}
		const std::vector<ChatLine> &chat() const noexcept {
			return chatLog;
		}
		const std::unordered_map<uint32_t, RemoteCursor> &cursors() const noexcept {
			return remoteCursors;
		}
		const std::string &status() const noexcept {
			return statusText;
		}
		uint16_t port() const noexcept {
			return listenPort;
		}

		// Set by the panel. Called on the GUI thread.
		std::function<void()> onChanged;
		std::function<void(const ChatLine &)> onChat;
		// A remote cursor moved: the map views need a repaint.
		std::function<void()> onCursorsChanged;

		// Network callbacks (already on the GUI thread). Public only for the CallAfter lambdas.
		void handleAccepted(const Connection::Ptr &conn, uint64_t generation);
		void handleReady(const Connection::Ptr &conn, uint64_t generation);
		void handleFrame(const Connection::Ptr &conn, const std::vector<uint8_t> &frame, uint64_t generation);
		void handleClosed(const Connection::Ptr &conn, const std::string &reason, uint64_t generation);

	private:
		struct Peer {
			Connection::Ptr conn;
			uint32_t userId = 0;
			bool hello = false;
			bool dropping = false; // closing: ignore its frames, no more broadcasts
			std::chrono::steady_clock::time_point lastCursor;
			std::chrono::steady_clock::time_point chatWindow;
			int chatCount = 0;
		};

		static Connection::Callbacks makeCallbacks(uint64_t generation);
		static void acceptLoop(std::shared_ptr<asio::ip::tcp::acceptor> acceptor, uint64_t generation, crypto::Salt salt, std::shared_ptr<const crypto::SessionKeys> keys);
		void changed();
		void resetToIdle(const std::string &message);
		void addSystemChat(const std::string &text);
		void postChat(uint32_t userId, const std::string &text, bool system);
		void storeChat(const ChatLine &line);

		// Host side
		void hostFrame(Peer &peer, const std::vector<uint8_t> &frame);
		void hostHello(Peer &peer, ByteReader &reader);
		void rejectPeer(Peer &peer, const std::string &reason);
		void dropPeer(const Connection::Ptr &conn, const std::string &reason);
		void broadcast(Msg type, const std::vector<uint8_t> &payload, const Connection* except = nullptr);
		Peer* peerForUser(uint32_t userId);
		uint32_t pickColor() const;
		std::string uniqueName(const std::string &wanted) const;
		static bool mayChangeRole(const User &actor, const User &target, Role newRole);
		static bool mayKick(const User &actor, const User &target);
		void applyRole(const User &actor, uint32_t target, Role role);
		void applyKick(const User &actor, uint32_t target);

		// Client side
		void clientFrame(const std::vector<uint8_t> &frame);
		void clientWelcome(ByteReader &reader);

		State currentState = State::Idle;
		uint64_t generation = 0;
		uint32_t selfId = 0;
		uint16_t listenPort = 0;
		Role defaultRole = Role::Editor;
		std::string selfName;
		std::string statusText;
		uint32_t nextUserId = 1;

		std::map<uint32_t, User> userList;
		std::unordered_map<uint32_t, RemoteCursor> remoteCursors;
		std::vector<ChatLine> chatLog;

		// Host
		std::shared_ptr<asio::ip::tcp::acceptor> acceptor;
		std::unordered_map<const Connection*, Peer> peers;
		// Client
		Connection::Ptr link;

		// Local cursor throttle
		Position lastSentPos;
		uint8_t lastSentBrush = 0;
		bool lastSentDown = false;
		std::chrono::steady_clock::time_point lastSentAt;
	};

} // namespace collab

#endif
