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

#ifndef RME_MCP_GUI_CALL_H
#define RME_MCP_GUI_CALL_H

#include "mcp_common.h"

#include <wx/app.h>
#include <wx/thread.h>

#include <atomic>
#include <chrono>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <type_traits>
#include <utility>

namespace mcp {

	// GUI thread only. Tool handlers must not nest: a Lua script can call
	// app.yield(), which drains pending CallAfter callbacks, and a second tool
	// starting inside it (say map_open closing a clean untitled tab) would free
	// the editor the first one still holds raw pointers to. While a handler is
	// running, later tasks wait here and are re-queued once it has finished.
	inline bool &guiBusy() {
		static bool busy = false;
		return busy;
	}

	inline std::deque<std::function<void()>> &deferredGuiTasks() {
		static std::deque<std::function<void()>> tasks;
		return tasks;
	}

	inline void runExclusiveOnGui(std::function<void()> task) {
		if (guiBusy()) {
			deferredGuiTasks().push_back(std::move(task));
			return;
		}

		guiBusy() = true;
		struct Release {
			~Release() {
				guiBusy() = false;
				auto &deferred = deferredGuiTasks();
				while (!deferred.empty()) {
					wxTheApp->CallAfter([queued = std::move(deferred.front())]() mutable {
						runExclusiveOnGui(std::move(queued));
					});
					deferred.pop_front();
				}
			}
		} release;
		task();
	}

	// Every tool handler touches editor state (Map, Tile, Item, GUI), none of
	// which is thread safe, so handlers always run on the GUI thread while the
	// worker thread blocks here waiting for the result.
	//
	// The timeout matters: if the editor sits in a modal dialog the GUI thread
	// never drains its idle queue, and without a deadline the socket would hang
	// until the user happens to click OK.
	//
	// After a timeout this returns while the queued lambda is still pending, so
	// fn must own everything it touches: capturing a reference to a caller
	// local would dangle when it finally runs. The promise is held by shared_ptr
	// for the same reason.
	//
	// A timed-out or aborted call must not run later: the client already got an
	// error and would retry, applying a mutating tool twice. The waiter and the
	// queued lambda race on a shared state; only one of them wins the transition
	// out of Pending. If the lambda already started, the waiter keeps waiting
	// for it.
	//
	// shouldAbort is polled from the waiting thread (the server passes "has it
	// been stopped?"), so shutting the server down does not leave workers
	// waiting out the full timeout.
	template <typename Fn>
	auto callOnGui(Fn &&fn, std::function<bool()> shouldAbort = nullptr) -> std::invoke_result_t<Fn> {
		using Result = std::invoke_result_t<Fn>;

		if (wxThread::IsMain()) {
			return fn();
		}

		enum State { Pending,
					 Started,
					 Cancelled };

		auto promise = std::make_shared<std::promise<Result>>();
		auto state = std::make_shared<std::atomic<int>>(Pending);
		auto task = std::make_shared<std::decay_t<Fn>>(std::forward<Fn>(fn));
		std::future<Result> future = promise->get_future();

		wxTheApp->CallAfter([promise, state, task]() {
			runExclusiveOnGui([promise, state, task]() {
				int expected = Pending;
				if (!state->compare_exchange_strong(expected, Started)) {
					return; // timed out and cancelled while queued
				}
				try {
					if constexpr (std::is_void_v<Result>) {
						(*task)();
						promise->set_value();
					} else {
						promise->set_value((*task)());
					}
				} catch (...) {
					promise->set_exception(std::current_exception());
				}
			});
		});

		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
		while (future.wait_for(std::chrono::milliseconds(50)) != std::future_status::ready) {
			const bool aborted = shouldAbort && shouldAbort();
			if (!aborted && std::chrono::steady_clock::now() < deadline) {
				continue;
			}
			int expected = Pending;
			if (state->compare_exchange_strong(expected, Cancelled)) {
				throw McpError(aborted ? "the MCP server was stopped" : "timed out waiting for the editor: it may be busy or showing a modal dialog");
			}
			// Already started on the GUI thread: let it finish rather than abandon a half-applied edit.
			future.wait();
			break;
		}

		return future.get();
	}

} // namespace mcp

#endif // RME_MCP_GUI_CALL_H
