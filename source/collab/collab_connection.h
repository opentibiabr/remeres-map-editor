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

#ifndef RME_COLLAB_CONNECTION_H
#define RME_COLLAB_CONNECTION_H

#include "collab_crypto.h"
#include "collab_protocol.h"

#include <asio.hpp>

#include <deque>
#include <functional>
#include <memory>

namespace collab {

	// One encrypted TCP link. Used by the host (one per peer) and by the client.
	//
	// Threading: the network service runs a single thread, so every handler below runs
	// serialized on it. send() and close() may be called from any thread (they hop onto
	// the service). The callbacks are invoked on the service thread: hop to the GUI thread
	// before touching editor state.
	class Connection : public std::enable_shared_from_this<Connection> {
	public:
		using Ptr = std::shared_ptr<Connection>;

		struct Callbacks {
			// Handshake finished, frames can be exchanged (client side: send Hello now).
			std::function<void(const Ptr &)> onReady;
			// A decrypted frame (type byte + payload).
			std::function<void(const Ptr &, std::vector<uint8_t>)> onFrame;
			// Called exactly once, whatever the reason.
			std::function<void(const Ptr &, const std::string &)> onClosed;
		};

		// Host side: the socket was just accepted. Keys come from the session password.
		static Ptr makeServer(asio::io_context &io, asio::ip::tcp::socket socket, const crypto::Salt &salt, std::shared_ptr<const crypto::SessionKeys> keys, Callbacks callbacks);
		// Client side: resolves and connects, then runs the handshake. The password is only
		// used to derive keys (on a worker thread) and is wiped afterwards.
		static Ptr makeClient(asio::io_context &io, std::string host, uint16_t port, std::string password, Callbacks callbacks);

		~Connection();

		void start();
		void send(Msg type, const std::vector<uint8_t> &payload = {});
		void close(const std::string &reason);
		// Host side: the peer proved it knows the password, stop the handshake deadline.
		void markAuthenticated();

		std::string remoteAddress() const {
			return remote;
		}

	private:
		Connection(asio::io_context &io, Callbacks callbacks);

		void startServer();
		void startClient();
		void onBanner();
		void beginStreams(std::shared_ptr<const crypto::SessionKeys> keys, bool server);
		void readFrameHeader();
		void readFrameBody(uint32_t length);
		void enqueue(std::vector<uint8_t> bytes);
		void pump();
		void closeNow(const std::string &reason);

		asio::io_context &io;
		asio::ip::tcp::socket socket;
		asio::steady_timer deadline;
		Callbacks callbacks;
		bool serverSide = false;
		bool closed = false;
		bool writing = false;
		bool ready = false;
		bool closing = false;
		std::string closeReason;
		std::string remote;

		// Server only
		crypto::Salt salt {};
		std::shared_ptr<const crypto::SessionKeys> keysHold;
		// Client only
		std::string host;
		uint16_t port = 0;
		std::string password;
		std::array<uint8_t, 8 + 2 + crypto::kSaltBytes + crypto::kHeaderBytes> banner {};
		asio::ip::tcp::resolver resolver;

		std::unique_ptr<crypto::Pusher> pusher;
		std::unique_ptr<crypto::Puller> puller;
		crypto::Header peerHeader {};
		std::array<uint8_t, 4> lengthBuffer {};
		std::vector<uint8_t> bodyBuffer;
		std::deque<std::vector<uint8_t>> outbox;
		size_t outboxBytes = 0;
	};

} // namespace collab

#endif
