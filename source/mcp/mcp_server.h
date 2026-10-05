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

#ifndef RME_MCP_SERVER_H
#define RME_MCP_SERVER_H

#include "mcp_common.h"

#include <asio.hpp>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace mcp {

	// A minimal MCP server speaking the Streamable HTTP transport over
	// loopback, so an external LLM client can drive the editor that is already
	// running. Only POST /mcp is implemented; SSE streaming is not needed
	// because every tool call is a single request/response round trip.
	class Server {
	public:
		enum class LogLevel {
			Info,
			Error
		};
		using LogCallback = std::function<void(LogLevel level, const std::string &message)>;

		static Server &get();

		// Binds to 127.0.0.1:port. Returns false and fills lastError on
		// failure (port already taken is the usual case).
		bool start(uint16_t port);
		void stop();

		bool isRunning() const noexcept {
			return running;
		}
		uint16_t getPort() const noexcept {
			return port;
		}
		const std::string &getLastError() const noexcept {
			return lastError;
		}
		std::string getEndpointUrl() const;

		// When false, tools declaring mutates=true are refused. Mirrors the
		// "Allow write" checkbox on the MCP panel.
		void setWriteAllowed(bool allowed) noexcept {
			writeAllowed = allowed;
		}
		bool isWriteAllowed() const noexcept {
			return writeAllowed;
		}

		// Invoked from the network thread; the panel marshals to the GUI.
		void setLogCallback(LogCallback callback);

	private:
		Server() = default;
		~Server();

		class Session;
		friend class Session;

		void accept(std::shared_ptr<asio::ip::tcp::acceptor> listener);
		void log(LogLevel level, const std::string &message);

		// Dispatches one JSON-RPC request. Returns std::nullopt for
		// notifications, which must not produce a response body.
		std::optional<json> handleRequest(const json &request);
		json handleToolCall(const json &params);

		// The server owns its io_context and thread instead of borrowing the shared
		// NetworkConnection one: the Live editing server stops that service on
		// close, which would pull it out from under the MCP sockets.
		asio::io_context context;
		std::unique_ptr<asio::executor_work_guard<asio::io_context::executor_type>> workGuard;
		std::thread ioThread;
		// Only touched on the GUI thread; handlers capture their own copy.
		std::shared_ptr<asio::ip::tcp::acceptor> acceptor;

		// Open client connections, so stop() can cut them off.
		std::mutex sessionMutex;
		std::vector<std::weak_ptr<Session>> sessions;

		std::atomic<bool> running { false };
		// Tool calls handed to the worker pool and not finished yet.
		std::atomic<int> inFlight { 0 };
		std::atomic<bool> writeAllowed { false };
		uint16_t port = 0;
		std::string lastError;
		LogCallback logCallback;
		std::mutex logMutex;

		// Declared last so it is destroyed first: tool calls still draining at
		// exit join here while the members above (log callback, mutexes) are
		// still alive. Tool calls block on the GUI thread (up to the callOnGui
		// timeout), so they run here rather than on the io thread.
		asio::thread_pool workers { 2 };
	};

} // namespace mcp

#endif // RME_MCP_SERVER_H
