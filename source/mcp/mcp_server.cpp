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

#include "mcp_server.h"

#include "mcp_gui_call.h"
#include "mcp_tools.h"

#include "../common.h"
#include "../definitions.h"
#include "../net_connection.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <istream>
#include <sstream>
#include <unordered_map>

namespace mcp {

	namespace {

		// Bumping this is a protocol change; clients negotiate against it.
		constexpr const char* PROTOCOL_VERSION = "2025-06-18";
		constexpr size_t MAX_BODY_BYTES = 8 * 1024 * 1024;

		// JSON-RPC error codes we actually emit.
		constexpr int ERROR_PARSE = -32700;
		constexpr int ERROR_INVALID_REQUEST = -32600;
		constexpr int ERROR_METHOD_NOT_FOUND = -32601;
		constexpr int ERROR_INVALID_PARAMS = -32602;
		constexpr int ERROR_INTERNAL = -32603;

		std::string toLower(std::string value) {
			std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
				return static_cast<char>(std::tolower(c));
			});
			return value;
		}

		std::string trim(const std::string &value) {
			const size_t first = value.find_first_not_of(" \t\r\n");
			if (first == std::string::npos) {
				return "";
			}
			const size_t last = value.find_last_not_of(" \t\r\n");
			return value.substr(first, last - first + 1);
		}

		// The MCP spec requires local HTTP servers to validate Origin: without
		// it any web page the user has open could reach this port through DNS
		// rebinding and drive the editor. A real MCP client sends no Origin at
		// all, so absence is the normal case.
		bool isOriginAllowed(const std::string &origin) {
			if (origin.empty()) {
				return true;
			}
			const std::string value = toLower(origin);
			static const char* prefixes[] = {
				"http://localhost", "https://localhost",
				"http://127.0.0.1", "https://127.0.0.1",
				"http://[::1]", "https://[::1]"
			};
			for (const char* prefix : prefixes) {
				if (value.rfind(prefix, 0) == 0) {
					// Guard against "http://localhost.evil.com".
					const size_t length = std::strlen(prefix);
					if (value.size() == length || value[length] == ':' || value[length] == '/') {
						return true;
					}
				}
			}
			return false;
		}

		json makeError(const json &id, int code, const std::string &message) {
			return json {
				{ "jsonrpc", "2.0" },
				{ "id", id.is_null() ? json(nullptr) : id },
				{ "error", json { { "code", code }, { "message", message } } }
			};
		}

		json makeResult(const json &id, json result) {
			return json {
				{ "jsonrpc", "2.0" },
				{ "id", id },
				{ "result", std::move(result) }
			};
		}

	} // namespace

	// ----------------------------------------------------------------------
	// Session
	// ----------------------------------------------------------------------

	class Server::Session : public std::enable_shared_from_this<Server::Session> {
	public:
		Session(Server &server, asio::ip::tcp::socket socket) :
			server(server), socket(std::move(socket)) { }

		void start() {
			readHeaders();
		}

	private:
		void readHeaders() {
			auto self = shared_from_this();
			asio::async_read_until(socket, buffer, "\r\n\r\n", [this, self](const std::error_code &error, size_t bytes) {
				if (error) {
					return;
				}
				onHeaders(bytes);
			});
		}

		void onHeaders(size_t headerBytes) {
			std::istream stream(&buffer);
			std::string requestLine;
			std::getline(stream, requestLine);
			requestLine = trim(requestLine);

			std::string method;
			std::string target;
			{
				std::istringstream parts(requestLine);
				parts >> method >> target;
			}

			std::unordered_map<std::string, std::string> headers;
			std::string line;
			while (std::getline(stream, line)) {
				line = trim(line);
				if (line.empty()) {
					break;
				}
				const size_t colon = line.find(':');
				if (colon == std::string::npos) {
					continue;
				}
				headers[toLower(trim(line.substr(0, colon)))] = trim(line.substr(colon + 1));
			}

			// async_read_until may have pulled part of the body along with the
			// headers; whatever is left in the streambuf is body already read.
			(void)headerBytes;

			if (!isOriginAllowed(headers["origin"])) {
				server.log(LogLevel::Error, "rejected a request with Origin: " + headers["origin"]);
				respond(403, json { { "error", "origin not allowed" } }.dump(), false);
				return;
			}

			if (method == "GET") {
				// No SSE stream: every call here is request/response.
				respond(405, json { { "error", "this server only accepts POST " + std::string("/mcp") } }.dump(), true, "Allow: POST\r\n");
				return;
			}
			if (method == "DELETE") {
				respond(200, "", true);
				return;
			}
			if (method != "POST") {
				respond(405, json { { "error", "unsupported method" } }.dump(), true, "Allow: POST\r\n");
				return;
			}

			size_t contentLength = 0;
			const auto it = headers.find("content-length");
			if (it != headers.end()) {
				try {
					contentLength = static_cast<size_t>(std::stoull(it->second));
				} catch (...) {
					respond(400, json { { "error", "bad Content-Length" } }.dump(), false);
					return;
				}
			}

			if (contentLength > MAX_BODY_BYTES) {
				respond(413, json { { "error", "request body too large" } }.dump(), false);
				return;
			}

			const size_t available = buffer.size();
			if (available >= contentLength) {
				std::string body(contentLength, '\0');
				stream.read(body.data(), static_cast<std::streamsize>(contentLength));
				handleBody(body);
				return;
			}

			auto self = shared_from_this();
			const size_t remaining = contentLength - available;
			asio::async_read(socket, buffer, asio::transfer_exactly(remaining), [this, self, contentLength](const std::error_code &error, size_t) {
				if (error) {
					return;
				}
				std::istream bodyStream(&buffer);
				std::string body(contentLength, '\0');
				bodyStream.read(body.data(), static_cast<std::streamsize>(contentLength));
				handleBody(body);
			});
		}

		void handleBody(const std::string &body) {
			json request;
			try {
				request = json::parse(body);
			} catch (const std::exception &e) {
				respond(400, makeError(nullptr, ERROR_PARSE, std::string("invalid JSON: ") + e.what()).dump(), true);
				return;
			}

			// A batch is a JSON array of requests; respond with an array of the
			// responses that are not notifications.
			if (request.is_array()) {
				json responses = json::array();
				for (const json &entry : request) {
					if (auto response = server.handleRequest(entry)) {
						responses.push_back(std::move(*response));
					}
				}
				if (responses.empty()) {
					respond(202, "", true);
				} else {
					respond(200, responses.dump(), true);
				}
				return;
			}

			if (auto response = server.handleRequest(request)) {
				respond(200, response->dump(), true);
			} else {
				// Notifications get an accepted-with-no-content reply.
				respond(202, "", true);
			}
		}

		void respond(int status, const std::string &body, bool keepAlive, const std::string &extraHeaders = "") {
			static const std::unordered_map<int, const char*> REASONS {
				{ 200, "OK" }, { 202, "Accepted" }, { 400, "Bad Request" }, { 403, "Forbidden" }, { 405, "Method Not Allowed" }, { 413, "Payload Too Large" }, { 500, "Internal Server Error" }
			};
			const auto reason = REASONS.find(status);

			std::ostringstream out;
			out << "HTTP/1.1 " << status << " " << (reason != REASONS.end() ? reason->second : "Unknown") << "\r\n"
				<< "Content-Type: application/json\r\n"
				<< "Content-Length: " << body.size() << "\r\n"
				<< "Connection: " << (keepAlive ? "keep-alive" : "close") << "\r\n"
				<< extraHeaders
				<< "\r\n"
				<< body;

			auto self = shared_from_this();
			response = out.str();
			asio::async_write(socket, asio::buffer(response), [this, self, keepAlive](const std::error_code &error, size_t) {
				if (error || !keepAlive) {
					std::error_code ignored;
					socket.shutdown(asio::ip::tcp::socket::shutdown_both, ignored);
					return;
				}
				readHeaders();
			});
		}

		Server &server;
		asio::ip::tcp::socket socket;
		asio::streambuf buffer;
		std::string response;
	};

	// ----------------------------------------------------------------------
	// Server
	// ----------------------------------------------------------------------

	Server &Server::get() {
		static Server instance;
		return instance;
	}

	bool Server::start(uint16_t requestedPort) {
		if (running) {
			return true;
		}

		lastError.clear();

		NetworkConnection &connection = NetworkConnection::getInstance();
		if (!connection.start()) {
			lastError = "could not start the network service";
			return false;
		}

		try {
			auto &service = connection.get_service();
			acceptor = std::make_shared<asio::ip::tcp::acceptor>(service);

			// Loopback only: the editor must never be reachable from the LAN.
			const asio::ip::tcp::endpoint endpoint(asio::ip::make_address("127.0.0.1"), requestedPort);
			acceptor->open(endpoint.protocol());
			acceptor->set_option(asio::ip::tcp::acceptor::reuse_address(true));
			acceptor->bind(endpoint);
			acceptor->listen();
		} catch (const std::exception &e) {
			lastError = fmt::format("could not listen on 127.0.0.1:{} ({})", requestedPort, e.what());
			acceptor.reset();
			return false;
		}

		port = requestedPort;
		running = true;

		ToolRegistry::get().ensureRegistered();
		log(LogLevel::Info, fmt::format("listening on {} with {} tools ({})", getEndpointUrl(), ToolRegistry::get().all().size(), writeAllowed ? "writes allowed" : "read-only"));

		accept();
		return true;
	}

	void Server::stop() {
		if (!running) {
			return;
		}
		running = false;

		if (acceptor) {
			std::error_code ignored;
			acceptor->close(ignored);
			acceptor.reset();
		}
		log(LogLevel::Info, "server stopped");
	}

	std::string Server::getEndpointUrl() const {
		return fmt::format("http://127.0.0.1:{}/mcp", port);
	}

	void Server::setLogCallback(LogCallback callback) {
		std::lock_guard<std::mutex> lock(logMutex);
		logCallback = std::move(callback);
	}

	void Server::log(LogLevel level, const std::string &message) {
		LogCallback callback;
		{
			std::lock_guard<std::mutex> lock(logMutex);
			callback = logCallback;
		}
		if (callback) {
			callback(level, message);
		}
	}

	void Server::accept() {
		if (!running || !acceptor) {
			return;
		}

		auto socket = std::make_shared<asio::ip::tcp::socket>(NetworkConnection::getInstance().get_service());
		acceptor->async_accept(*socket, [this, socket](const std::error_code &error) {
			if (!error && running) {
				std::make_shared<Session>(*this, std::move(*socket))->start();
			}
			accept();
		});
	}

	std::optional<json> Server::handleRequest(const json &request) {
		if (!request.is_object() || !request.contains("method") || !request["method"].is_string()) {
			return makeError(nullptr, ERROR_INVALID_REQUEST, "not a JSON-RPC request object");
		}

		const std::string method = request["method"].get<std::string>();
		const json id = request.contains("id") ? request["id"] : json(nullptr);
		const bool isNotification = !request.contains("id");
		const json params = request.contains("params") ? request["params"] : json::object();

		if (isNotification) {
			// notifications/initialized and friends need no reply.
			return std::nullopt;
		}

		try {
			if (method == "initialize") {
				return makeResult(id, json { { "protocolVersion", PROTOCOL_VERSION }, { "capabilities", json { { "tools", json { { "listChanged", false } } } } }, { "serverInfo", json { { "name", "remeres-map-editor" }, { "version", __RME_VERSION__ } } }, { "instructions", "Tools for reading and editing the OTBM map currently open in Remere's Map Editor. "
																																																																				  "Start with map_info, then map_read_region (mode=summary) to orient yourself in an area, "
																																																																				  "and map_render_region to actually see it. Writes go through run_lua and are refused unless "
																																																																				  "the user enabled writing in the editor's MCP panel." } });
			}

			if (method == "ping") {
				return makeResult(id, json::object());
			}

			if (method == "tools/list") {
				ToolRegistry &registry = ToolRegistry::get();
				registry.ensureRegistered();

				json tools = json::array();
				for (const Tool &tool : registry.all()) {
					tools.push_back(json {
						{ "name", tool.name },
						{ "description", tool.description },
						{ "inputSchema", tool.inputSchema } });
				}
				return makeResult(id, json { { "tools", std::move(tools) } });
			}

			if (method == "tools/call") {
				return makeResult(id, handleToolCall(params));
			}

			return makeError(id, ERROR_METHOD_NOT_FOUND, "unknown method: " + method);
		} catch (const McpError &e) {
			return makeError(id, ERROR_INVALID_PARAMS, e.what());
		} catch (const std::exception &e) {
			return makeError(id, ERROR_INTERNAL, e.what());
		}
	}

	json Server::handleToolCall(const json &params) {
		ToolRegistry &registry = ToolRegistry::get();
		registry.ensureRegistered();

		if (!params.is_object() || !params.contains("name") || !params["name"].is_string()) {
			throw McpError("tools/call requires a name");
		}

		const std::string name = params["name"].get<std::string>();
		const json arguments = params.contains("arguments") && params["arguments"].is_object()
			? params["arguments"]
			: json::object();

		const Tool* tool = registry.find(name);
		if (!tool) {
			throw McpError("unknown tool: " + name);
		}

		// A tool failure is reported as a result with isError set, not as a
		// JSON-RPC error, so the model reads the message and retries sensibly.
		auto failure = [](const std::string &message) {
			json result = textResult(message);
			result["isError"] = true;
			return result;
		};

		if (tool->mutates && !writeAllowed) {
			log(LogLevel::Error, fmt::format("refused {}: writes are disabled", name));
			return failure(
				"This tool modifies the map, but the editor's MCP panel is in read-only mode. "
				"Ask the user to tick \"Allow write operations\" in the MCP Server panel."
			);
		}

		const auto started = std::chrono::steady_clock::now();
		try {
			// Editor state is not thread safe, so the handler runs on the GUI
			// thread while this network thread waits.
			json result = callOnGui([tool, arguments]() {
				return tool->handler(arguments);
			});

			const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);
			log(LogLevel::Info, fmt::format("{} ok ({} ms)", name, elapsed.count()));
			return result;
		} catch (const McpError &e) {
			log(LogLevel::Error, fmt::format("{} failed: {}", name, e.what()));
			return failure(e.what());
		} catch (const std::exception &e) {
			log(LogLevel::Error, fmt::format("{} crashed: {}", name, e.what()));
			return failure(std::string("internal error: ") + e.what());
		}
	}

} // namespace mcp
