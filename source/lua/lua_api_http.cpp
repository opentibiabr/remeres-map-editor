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
#include "lua_api_http.h"

#include <cpr/cpr.h>
#include <nlohmann/json.hpp>
#include <thread>
#include <future>
#include <mutex>
#include <atomic>
#include <queue>
#include <condition_variable>
#include <string_view>
#include <functional>

namespace LuaAPI {

	// StreamSession class for managing streaming HTTP requests
	class StreamSession {
	public:
		static constexpr size_t kMaxBufferedBytes = 4 * 1024 * 1024; // 4 MB

		StreamSession() = default;

		bool appendChunk(const std::string &chunk) {
			std::scoped_lock lock(mutex_);
			if (bufferedBytes_ + chunk.size() > kMaxBufferedBytes) {
				hasError_ = true;
				finished_ = true;
				errorMessage_ = "HTTP stream buffer limit exceeded";
				cv_.notify_all();
				return false;
			}
			chunks_.push(chunk);
			bufferedBytes_ += chunk.size();
			cv_.notify_one();
			return true;
		}

		std::string getNextChunk() {
			std::scoped_lock lock(mutex_);
			if (chunks_.empty()) {
				return "";
			}
			std::string chunk = chunks_.front();
			chunks_.pop();
			bufferedBytes_ -= chunk.size();
			return chunk;
		}

		bool hasChunks() {
			std::scoped_lock lock(mutex_);
			return !chunks_.empty();
		}

		void setFinished() {
			std::scoped_lock lock(mutex_);
			finished_ = true;
			cv_.notify_all();
		}

		bool isFinished() {
			std::scoped_lock lock(mutex_);
			return finished_;
		}

		void setError(std::string_view error) {
			std::scoped_lock lock(mutex_);
			hasError_ = true;
			errorMessage_ = error;
			finished_ = true;
			cv_.notify_all();
		}

		bool hasError() {
			std::scoped_lock lock(mutex_);
			return hasError_;
		}

		std::string getError() {
			std::scoped_lock lock(mutex_);
			return errorMessage_;
		}

		void setStatusCode(int code) {
			statusCode_ = code;
		}

		int getStatusCode() {
			return statusCode_;
		}

		void setHeaders(const cpr::Header &headers) {
			std::scoped_lock lock(mutex_);
			responseHeaders_ = headers;
		}

		cpr::Header getHeaders() {
			std::scoped_lock lock(mutex_);
			return responseHeaders_;
		}

		void cancel() {
			finished_ = true;
			cv_.notify_all();
		}

		void setWorker(std::jthread &&t) {
			worker_ = std::move(t);
		}

	private:
		std::queue<std::string> chunks_;
		size_t bufferedBytes_ = 0;
		std::mutex mutex_;
		std::condition_variable cv_;
		std::atomic<bool> finished_ = false;
		std::atomic<bool> hasError_ = false;
		std::string errorMessage_;
		std::atomic<int> statusCode_ = 0;
		cpr::Header responseHeaders_;
		std::jthread worker_;
	};

	static std::map<int, std::shared_ptr<StreamSession>> &streamSessions() {
		static std::map<int, std::shared_ptr<StreamSession>> instance;
		return instance;
	}

	static std::mutex &sessionsMutex() {
		static std::mutex instance;
		return instance;
	}

	static int generateSessionId() {
		static int id = 0;
		return ++id;
	}

	// Security helper: Block localhost and loopback
	static bool isUrlSafe(const std::string &url_str) {
		std::string low = url_str;
		std::transform(low.begin(), low.end(), low.begin(), [](unsigned char c) {
			return static_cast<char>(std::tolower(c));
		});
		// Block common localhost patterns
		if (low.contains("localhost") || low.contains("127.") || low.contains("0.0.0.0") || low.contains("[::1]") || low.contains("[::ffff:127.") || low.contains("//[::")) {
			return false;
		}
		// Block file:// and other dangerous schemes
		if (low.find("file://") == 0 || low.find("ftp://") == 0) {
			return false;
		}
		return true;
	}

	// HTTP GET request
	static sol::table httpGet(sol::this_state ts, const std::string &url, sol::optional<sol::table> optHeaders) {
		sol::state_view lua(ts);
		sol::table result = lua.create_table();

		if (!isUrlSafe(url)) {
			result["error"] = "Security: URL blocked (Localhost access denied)";
			result["ok"] = false;
			return result;
		}

		cpr::Header headers;
		if (optHeaders) {
			sol::table headersTable = *optHeaders;
			for (auto &pair : headersTable) {
				if (pair.first.is<std::string>() && pair.second.is<std::string>()) {
					headers[pair.first.as<std::string>()] = pair.second.as<std::string>();
				}
			}
		}

		cpr::Response response = cpr::Get(cpr::Url { url }, headers, cpr::Timeout { 10000 });

		result["status"] = static_cast<int>(response.status_code);
		result["body"] = response.text;
		result["error"] = response.error.message;
		result["ok"] = response.status_code >= 200 && response.status_code < 300;

		// Parse response headers
		sol::table respHeaders = lua.create_table();
		for (const auto &h : response.header) {
			respHeaders[h.first] = h.second;
		}
		result["headers"] = respHeaders;

		return result;
	}

	// HTTP POST request
	static sol::table httpPost(sol::this_state ts, const std::string &url, const std::string &body, sol::optional<sol::table> optHeaders) {
		sol::state_view lua(ts);
		sol::table result = lua.create_table();

		if (!isUrlSafe(url)) {
			result["error"] = "Security: URL blocked (Localhost access denied)";
			result["ok"] = false;
			return result;
		}

		cpr::Header headers;
		if (optHeaders) {
			sol::table headersTable = *optHeaders;
			for (auto &pair : headersTable) {
				if (pair.first.is<std::string>() && pair.second.is<std::string>()) {
					headers[pair.first.as<std::string>()] = pair.second.as<std::string>();
				}
			}
		}

		cpr::Response response = cpr::Post(
			cpr::Url { url },
			cpr::Body { body },
			headers,
			cpr::Timeout { 10000 }
		);

		result["status"] = static_cast<int>(response.status_code);
		result["body"] = response.text;
		result["error"] = response.error.message;
		result["ok"] = response.status_code >= 200 && response.status_code < 300;

		// Parse response headers
		sol::table respHeaders = lua.create_table();
		for (const auto &h : response.header) {
			respHeaders[h.first] = h.second;
		}
		result["headers"] = respHeaders;

		return result;
	}

	// Helper function to convert Lua table to JSON
	static nlohmann::json luaToJson(sol::object obj, std::unordered_set<const void*> &visited) {
		if (obj.is<bool>()) {
			return obj.as<bool>();
		} else if (obj.is<int>()) {
			return obj.as<int>();
		} else if (obj.is<double>()) {
			return obj.as<double>();
		} else if (obj.is<std::string>()) {
			return obj.as<std::string>();
		} else if (obj.is<sol::table>()) {
			sol::table tbl = obj.as<sol::table>();
			auto ptr = tbl.pointer();
			if (!visited.insert(ptr).second) {
				throw sol::error("Cyclic table detected in JSON conversion");
			}

			// Check if it's an array (sequential integer keys starting at 1)
			bool isArray = true;
			size_t expectedKey = 1;
			for (auto &pair : tbl) {
				if (!pair.first.is<size_t>() || pair.first.as<size_t>() != expectedKey) {
					isArray = false;
					break;
				}
				expectedKey++;
			}

			nlohmann::json result;
			if (isArray && expectedKey > 1) {
				result = nlohmann::json::array();
				for (auto &pair : tbl) {
					result.push_back(luaToJson(pair.second, visited));
				}
			} else {
				result = nlohmann::json::object();
				for (auto &pair : tbl) {
					std::string key;
					if (pair.first.is<std::string>()) {
						key = pair.first.as<std::string>();
					} else if (pair.first.is<int>()) {
						key = std::to_string(pair.first.as<int>());
					} else {
						continue;
					}
					result[key] = luaToJson(pair.second, visited);
				}
			}
			visited.erase(ptr);
			return result;
		} else if (obj.is<sol::nil_t>()) {
			return nullptr;
		}
		return nullptr;
	}

	// HTTP POST with JSON body
	static sol::table httpPostJson(sol::this_state ts, const std::string &url, sol::table jsonBody, sol::optional<sol::table> optHeaders) {
		sol::state_view lua(ts);

		std::unordered_set<const void*> visited;
		std::string jsonStr = luaToJson(jsonBody, visited).dump();

		// Add Content-Type header if not present
		sol::table headers = lua.create_table();
		headers["Content-Type"] = "application/json";

		if (optHeaders) {
			for (auto &pair : *optHeaders) {
				if (pair.first.is<std::string>() && pair.second.is<std::string>()) {
					headers[pair.first.as<std::string>()] = pair.second.as<std::string>();
				}
			}
		}

		return httpPost(ts, url, jsonStr, headers);
	}

	// Start a streaming POST request - returns session ID
	static sol::table httpPostStream(sol::this_state ts, const std::string &url, const std::string &body, sol::optional<sol::table> optHeaders) {
		sol::state_view lua(ts);
		sol::table result = lua.create_table();

		if (!isUrlSafe(url)) {
			result["error"] = "Security: URL blocked (Localhost access denied)";
			result["ok"] = false;
			return result;
		}

		auto session = std::make_shared<StreamSession>();
		int sessionId;

		{
			std::scoped_lock lock(sessionsMutex());
			sessionId = generateSessionId();
			streamSessions()[sessionId] = session;
		}

		cpr::Header headers;
		if (optHeaders) {
			sol::table headersTable = *optHeaders;
			for (auto &pair : headersTable) {
				if (pair.first.is<std::string>() && pair.second.is<std::string>()) {
					headers[pair.first.as<std::string>()] = pair.second.as<std::string>();
				}
			}
		}

		// Start the streaming request in a separate thread
		session->setWorker(std::jthread([session, url, body, headers](std::stop_token) {
			std::function<bool(std::string_view, intptr_t)> writeCallback = [session](std::string_view data, intptr_t /*userdata*/) {
				return session->appendChunk(std::string(data));
			};

			cpr::Response response = cpr::Post(
				cpr::Url { url },
				cpr::Body { body },
				headers,
				cpr::WriteCallback { writeCallback, 0 },
				cpr::Timeout { 30000 }
			);

			session->setStatusCode(static_cast<int>(response.status_code));
			session->setHeaders(response.header);

			if (response.error) {
				session->setError(response.error.message);
			} else {
				session->setFinished();
			}
		}));

		result["sessionId"] = sessionId;
		result["ok"] = true;

		return result;
	}

	// Start a streaming POST request with JSON body - returns session ID
	static sol::table httpPostJsonStream(sol::this_state ts, const std::string &url, sol::table jsonBody, sol::optional<sol::table> optHeaders) {
		sol::state_view lua(ts);

		std::unordered_set<const void*> visited;
		std::string jsonStr = luaToJson(jsonBody, visited).dump();

		// Add Content-Type header if not present
		sol::table headers = lua.create_table();
		headers["Content-Type"] = "application/json";

		if (optHeaders) {
			for (auto &pair : *optHeaders) {
				if (pair.first.is<std::string>() && pair.second.is<std::string>()) {
					headers[pair.first.as<std::string>()] = pair.second.as<std::string>();
				}
			}
		}

		return httpPostStream(ts, url, jsonStr, headers);
	}

	// Read available chunks from a stream session
	static sol::table httpStreamRead(sol::this_state ts, int sessionId) {
		sol::state_view lua(ts);
		sol::table result = lua.create_table();

		std::shared_ptr<StreamSession> session;
		{
			std::scoped_lock lock(sessionsMutex());
			auto it = streamSessions().find(sessionId);
			if (it == streamSessions().end()) {
				result["ok"] = false;
				result["error"] = "Invalid session ID";
				result["finished"] = true;
				return result;
			}
			session = it->second;
		}

		// Collect all available chunks
		std::string data;
		while (session->hasChunks()) {
			data += session->getNextChunk();
		}

		result["data"] = data;
		result["finished"] = session->isFinished();
		result["hasError"] = session->hasError();

		if (session->hasError()) {
			result["error"] = session->getError();
			result["ok"] = false;
		} else if (session->isFinished()) {
			int status = session->getStatusCode();
			result["ok"] = status >= 200 && status < 300;
		} else {
			result["ok"] = true;
		}

		if (session->isFinished()) {
			result["status"] = session->getStatusCode();

			// Return headers when finished
			sol::table respHeaders = lua.create_table();
			for (const auto &h : session->getHeaders()) {
				respHeaders[h.first] = h.second;
			}
			result["headers"] = respHeaders;
		}

		return result;
	}

	// Close and cleanup a stream session
	static bool httpStreamClose(int sessionId) {
		std::scoped_lock lock(sessionsMutex());
		if (auto it = streamSessions().find(sessionId); it != streamSessions().end()) {
			it->second->cancel();
			streamSessions().erase(it);
			return true;
		}
		return false;
	}

	// Check if a stream session is finished
	static sol::table httpStreamStatus(sol::this_state ts, int sessionId) {
		sol::state_view lua(ts);
		sol::table result = lua.create_table();

		std::shared_ptr<StreamSession> session;
		{
			std::scoped_lock lock(sessionsMutex());
			auto it = streamSessions().find(sessionId);
			if (it == streamSessions().end()) {
				result["valid"] = false;
				result["finished"] = true;
				return result;
			}
			session = it->second;
		}

		result["valid"] = true;
		result["finished"] = session->isFinished();
		result["hasError"] = session->hasError();
		result["hasData"] = session->hasChunks();

		if (session->hasError()) {
			result["error"] = session->getError();
		}

		if (session->isFinished()) {
			result["status"] = session->getStatusCode();
		}

		return result;
	}

	void registerHttp(sol::state &lua) {
		sol::table http = lua.create_named_table("http");

		// Basic HTTP methods
		http["get"] = httpGet;
		http["post"] = httpPost;
		http["postJson"] = httpPostJson;

		// Streaming HTTP methods
		http["postStream"] = httpPostStream;
		http["postJsonStream"] = httpPostJsonStream;
		http["streamRead"] = httpStreamRead;
		http["streamClose"] = httpStreamClose;
		http["streamStatus"] = httpStreamStatus;
	}

} // namespace LuaAPI
