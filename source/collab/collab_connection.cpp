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

#include "collab_connection.h"

#include <sodium.h>

#include <thread>

namespace collab {

	namespace {
		constexpr char kMagic[8] = { 'R', 'M', 'E', 'C', 'O', 'L', 'A', 'B' };
		constexpr auto kHandshakeTimeout = std::chrono::seconds(15);
		constexpr auto kFlushTimeout = std::chrono::seconds(3);
	}

	Connection::Connection(asio::io_context &io, Callbacks callbacks) :
		io(io), socket(io), deadline(io), callbacks(std::move(callbacks)), resolver(io) { }

	Connection::~Connection() = default;

	Connection::Ptr Connection::makeServer(asio::io_context &io, asio::ip::tcp::socket socket, const crypto::Salt &salt, std::vector<std::shared_ptr<const crypto::SessionKeys>> keysets, Callbacks callbacks) {
		Ptr conn(new Connection(io, std::move(callbacks)));
		conn->socket = std::move(socket);
		conn->serverSide = true;
		conn->salt = salt;
		conn->keysHold = std::move(keysets);
		return conn;
	}

	Connection::Ptr Connection::makeClient(asio::io_context &io, std::string host, uint16_t port, std::string password, Callbacks callbacks) {
		Ptr conn(new Connection(io, std::move(callbacks)));
		conn->host = std::move(host);
		conn->port = port;
		conn->password = std::move(password);
		return conn;
	}

	void Connection::start() {
		asio::post(io, [self = shared_from_this()] {
			if (self->serverSide) {
				self->startServer();
			} else {
				self->startClient();
			}
		});
	}

	void Connection::markAuthenticated() {
		asio::post(io, [self = shared_from_this()] {
			if (!self->closing) {
				self->deadline.cancel();
			}
		});
	}

	// ---- handshake -----------------------------------------------------------------------

	void Connection::startServer() {
		std::error_code ec;
		auto endpoint = socket.remote_endpoint(ec);
		if (!ec) {
			remote = endpoint.address().to_string();
		}

		auto self = shared_from_this();
		deadline.expires_after(kHandshakeTimeout);
		deadline.async_wait([self](const std::error_code &error) {
			if (!error) {
				self->closeNow("Handshake timed out");
			}
		});

		// Plain banner: magic, protocol version, password salt. Our own stream header follows once the
		// first frame has shown which of the passwords the peer used.
		std::vector<uint8_t> hello(kMagic, kMagic + sizeof(kMagic));
		hello.push_back(static_cast<uint8_t>(kProtocolVersion >> 8));
		hello.push_back(static_cast<uint8_t>(kProtocolVersion));
		hello.insert(hello.end(), salt.begin(), salt.end());
		enqueue(std::move(hello));

		asio::async_read(socket, asio::buffer(peerHeader), [self](const std::error_code &error, size_t) {
			if (error) {
				self->closeNow("Connection lost during handshake");
				return;
			}
			self->readFrameHeader();
		});
	}

	void Connection::startClient() {
		auto self = shared_from_this();
		deadline.expires_after(std::chrono::seconds(20));
		deadline.async_wait([self](const std::error_code &error) {
			if (!error) {
				self->closeNow("Timed out");
			}
		});

		resolver.async_resolve(host, std::to_string(port), [self](const std::error_code &error, asio::ip::tcp::resolver::results_type results) {
			if (error) {
				self->closeNow("Could not resolve " + self->host);
				return;
			}
			asio::async_connect(self->socket, results, [self](const std::error_code &error, const asio::ip::tcp::endpoint &endpoint) {
				if (error) {
					self->closeNow("Could not connect to " + self->host + ":" + std::to_string(self->port));
					return;
				}
				self->remote = endpoint.address().to_string();
				asio::async_read(self->socket, asio::buffer(self->banner), [self](const std::error_code &error, size_t) {
					if (error) {
						self->closeNow("Not an RME collaboration server");
						return;
					}
					self->onBanner();
				});
			});
		});
	}

	void Connection::onBanner() {
		if (std::memcmp(banner.data(), kMagic, sizeof(kMagic)) != 0) {
			closeNow("Not an RME collaboration server");
			return;
		}
		const uint16_t version = static_cast<uint16_t>((banner[8] << 8) | banner[9]);
		if (version != kProtocolVersion) {
			closeNow("Incompatible protocol version (host " + std::to_string(version) + ", this editor " + std::to_string(kProtocolVersion) + ")");
			return;
		}
		std::memcpy(salt.data(), banner.data() + 10, salt.size());

		// Argon2 takes a noticeable fraction of a second: keep it off the network thread.
		std::thread([self = shared_from_this(), pw = std::move(password), salt = salt]() mutable {
			auto keys = crypto::deriveKeys(pw, salt);
			sodium_memzero(pw.data(), pw.size());
			std::shared_ptr<const crypto::SessionKeys> shared;
			if (keys) {
				shared = std::make_shared<const crypto::SessionKeys>(*keys);
			}
			asio::post(self->io, [self, shared] {
				if (self->closed) {
					return;
				}
				if (!shared) {
					self->closeNow("Could not derive the session key");
					return;
				}
				self->pusher = std::make_unique<crypto::Pusher>(shared->clientToServer);
				self->enqueue(std::vector<uint8_t>(self->pusher->header().begin(), self->pusher->header().end()));
				self->ready = true;
				if (self->callbacks.onReady) {
					self->callbacks.onReady(self);
				}
				// The host answers with its own stream header once it knows which password we used.
				asio::async_read(self->socket, asio::buffer(self->peerHeader), [self, shared](const std::error_code &error, size_t) {
					if (error) {
						self->closeNow(error == asio::error::eof ? "Connection closed" : "Connection lost");
						return;
					}
					self->puller = std::make_unique<crypto::Puller>(shared->serverToClient, self->peerHeader);
					self->readFrameHeader();
				});
			});
		}).detach();
		password.clear();
	}

	// ---- framing -------------------------------------------------------------------------

	void Connection::readFrameHeader() {
		auto self = shared_from_this();
		asio::async_read(socket, asio::buffer(lengthBuffer), [self](const std::error_code &error, size_t) {
			if (error) {
				self->closeNow(error == asio::error::eof ? "Connection closed" : "Connection lost");
				return;
			}
			const uint32_t length = (static_cast<uint32_t>(self->lengthBuffer[0]) << 24) | (static_cast<uint32_t>(self->lengthBuffer[1]) << 16) | (static_cast<uint32_t>(self->lengthBuffer[2]) << 8) | self->lengthBuffer[3];
			if (length <= crypto::kOverhead || length > kMaxFrame + crypto::kOverhead) {
				self->closeNow("Protocol error (bad frame length)");
				return;
			}
			self->readFrameBody(length);
		});
	}

	void Connection::readFrameBody(uint32_t length) {
		bodyBuffer.resize(length);
		auto self = shared_from_this();
		asio::async_read(socket, asio::buffer(bodyBuffer), [self](const std::error_code &error, size_t) {
			if (error) {
				self->closeNow("Connection lost");
				return;
			}
			const bool first = !self->puller; // host side: the first frame decides which password was used
			auto plain = first ? self->identifyPeer() : self->puller->decrypt(self->bodyBuffer.data(), self->bodyBuffer.size());
			if (!plain) {
				self->closeNow("Wrong password or corrupted stream");
				return;
			}
			if (first && self->callbacks.onReady) {
				self->callbacks.onReady(self);
			}
			if (self->callbacks.onFrame) {
				self->callbacks.onFrame(self, std::move(*plain));
			}
			if (!self->closed) {
				self->readFrameHeader();
			}
		});
	}

	std::optional<std::vector<uint8_t>> Connection::identifyPeer() {
		for (size_t i = 0; i < keysHold.size(); ++i) {
			auto candidate = std::make_unique<crypto::Puller>(keysHold[i]->clientToServer, peerHeader);
			auto plain = candidate->decrypt(bodyBuffer.data(), bodyBuffer.size());
			if (!plain) {
				continue;
			}
			puller = std::move(candidate);
			pusher = std::make_unique<crypto::Pusher>(keysHold[i]->serverToClient);
			matchedKey = i;
			enqueue(std::vector<uint8_t>(pusher->header().begin(), pusher->header().end()));
			ready = true;
			keysHold.clear();
			return plain;
		}
		return std::nullopt;
	}

	void Connection::send(Msg type, const std::vector<uint8_t> &payload) {
		if (payload.size() + 1 > kMaxFrame) {
			return;
		}
		asio::post(io, [self = shared_from_this(), frame = makeFrame(type, payload)] {
			if (self->closed || self->closing || !self->ready) {
				return;
			}
			std::vector<uint8_t> cipher = self->pusher->encrypt(frame);
			std::vector<uint8_t> bytes;
			bytes.reserve(cipher.size() + 4);
			const uint32_t length = static_cast<uint32_t>(cipher.size());
			bytes.push_back(static_cast<uint8_t>(length >> 24));
			bytes.push_back(static_cast<uint8_t>(length >> 16));
			bytes.push_back(static_cast<uint8_t>(length >> 8));
			bytes.push_back(static_cast<uint8_t>(length));
			bytes.insert(bytes.end(), cipher.begin(), cipher.end());
			self->enqueue(std::move(bytes));
		});
	}

	void Connection::enqueue(std::vector<uint8_t> bytes) {
		if (closed) {
			return;
		}
		outboxBytes += bytes.size();
		outbox.push_back(std::move(bytes));
		if (outboxBytes > kMaxOutbox) {
			closeNow("Connection too slow");
			return;
		}
		pump();
	}

	void Connection::pump() {
		if (writing || closed || outbox.empty()) {
			return;
		}
		writing = true;
		auto self = shared_from_this();
		asio::async_write(socket, asio::buffer(outbox.front()), [self](const std::error_code &error, size_t) {
			if (error) {
				self->closeNow("Connection lost");
				return;
			}
			self->outboxBytes -= self->outbox.front().size();
			self->outbox.pop_front();
			self->writing = false;
			if (self->closing && self->outbox.empty()) {
				self->closeNow(self->closeReason);
				return;
			}
			self->pump();
		});
	}

	// ---- closing -------------------------------------------------------------------------

	void Connection::close(const std::string &reason) {
		asio::post(io, [self = shared_from_this(), reason] {
			if (self->closed || self->closing) {
				return;
			}
			if (!self->writing && self->outbox.empty()) {
				self->closeNow(reason);
				return;
			}
			// Let queued frames (a Bye, a Reject) reach the peer first, but not forever.
			self->closing = true;
			self->closeReason = reason;
			self->deadline.expires_after(kFlushTimeout);
			self->deadline.async_wait([self](const std::error_code &error) {
				if (!error) {
					self->closeNow(self->closeReason);
				}
			});
		});
	}

	void Connection::closeNow(const std::string &reason) {
		if (closed) {
			return;
		}
		closed = true;

		std::error_code ec;
		deadline.cancel();
		resolver.cancel();
		socket.shutdown(asio::ip::tcp::socket::shutdown_both, ec);
		socket.close(ec);

		auto onClosed = std::move(callbacks.onClosed);
		callbacks = Callbacks {};
		if (onClosed) {
			onClosed(shared_from_this(), reason);
		}
	}

} // namespace collab
