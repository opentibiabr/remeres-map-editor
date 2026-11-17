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
#include <future>
#include <memory>
#include <type_traits>
#include <utility>

namespace mcp {

	// Every tool handler touches editor state (Map, Tile, Item, GUI), none of
	// which is thread safe, so handlers always run on the GUI thread while the
	// asio thread blocks here waiting for the result.
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
	// A timed-out call must not run later: the client already got an error and
	// would retry, applying a mutating tool twice. The waiter and the queued
	// lambda race on a shared state; only one of them wins the transition out of
	// Pending. If the lambda already started, the waiter keeps waiting for it.
	template <typename Fn>
	auto callOnGui(Fn &&fn) -> std::invoke_result_t<Fn> {
		using Result = std::invoke_result_t<Fn>;

		if (wxThread::IsMain()) {
			return fn();
		}

		enum State { Pending,
					 Started,
					 Cancelled };

		auto promise = std::make_shared<std::promise<Result>>();
		auto state = std::make_shared<std::atomic<int>>(Pending);
		std::future<Result> future = promise->get_future();

		wxTheApp->CallAfter([promise, state, fn = std::forward<Fn>(fn)]() mutable {
			int expected = Pending;
			if (!state->compare_exchange_strong(expected, Started)) {
				return; // timed out and cancelled while queued
			}
			try {
				if constexpr (std::is_void_v<Result>) {
					fn();
					promise->set_value();
				} else {
					promise->set_value(fn());
				}
			} catch (...) {
				promise->set_exception(std::current_exception());
			}
		});

		if (future.wait_for(std::chrono::seconds(30)) != std::future_status::ready) {
			int expected = Pending;
			if (state->compare_exchange_strong(expected, Cancelled)) {
				throw McpError("timed out waiting for the editor: it may be busy or showing a modal dialog");
			}
			// Already started on the GUI thread: let it finish rather than abandon a half-applied edit.
			future.wait();
		}

		return future.get();
	}

} // namespace mcp

#endif // RME_MCP_GUI_CALL_H
