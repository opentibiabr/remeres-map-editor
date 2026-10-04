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
#include "collab_journal.h"
#include "collab_meta.h"
#include "collab_protocol.h"

#include "../position.h"

#include <chrono>
#include <deque>
#include <optional>
#include <memory>
#include <functional>
#include <map>
#include <unordered_map>

class Editor;
class Map;
class Tile;
class wxTimer;

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

	// A rectangle of one floor an editor marked as theirs. Only a hint, nothing is enforced.
	struct Claim {
		uint32_t id = 0;
		uint32_t ownerId = 0;
		std::string ownerName;
		uint32_t color = 0;
		Position from;
		Position to;
	};

	// What somebody is doing besides pointing: their selection, their tool, whether they type in the chat.
	struct Presence {
		bool hasSelection = false;
		Position selFrom;
		Position selTo;
		std::string tool;
		bool typing = false;
		std::chrono::steady_clock::time_point updated;
	};

	struct ChatLine {
		uint32_t userId = 0;
		std::string name;
		uint32_t color = 0;
		std::string text;
		int64_t time = 0;
		bool system = false;
	};

	// What the host allows its Editors to change; Admins and the host can always change everything.
	struct HostOptions {
		bool editorsMeta = true; // houses, towns, waypoints, zones
		bool editorsProps = true; // map description and size
		bool editorsSpawns = true; // spawns, monsters, npcs
		int autosaveMinutes = 0; // 0: never
		std::string viewerPassword; // optional second password: whoever uses it joins as a Viewer
	};

	struct Toast {
		std::string text;
		std::chrono::steady_clock::time_point until;
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
		Session();
		~Session();

		// Set before startHosting().
		HostOptions hostOptions;

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
		// editor is the map being shared; shareMap lets participants keep and save a copy.
		bool startHosting(Editor* editor, const std::string &name, uint16_t port, const std::string &password, Role defaultRole, bool shareMap, bool saveOnParticipants, std::string &error);
		// Asynchronous: progress and errors arrive through status() / onChanged.
		void join(const std::string &name, const std::string &host, uint16_t port, const std::string &password);
		// Stops hosting, leaves, or cancels a pending join.
		void leave();

		// Closing the map tab / the app while hosting asks first. False means "do not close".
		bool confirmCloseEditor(Editor* editor);
		bool confirmShutdown();
		// The editor is being destroyed: end the session that depends on it.
		void onEditorClosing(Editor* editor);
		// Snapshot download progress while joining, 0-100.
		int downloadPercent() const;

		// ---- editor hooks (GUI thread) ----
		// A tile of the shared map was swapped by an action (commit, undo or redo).
		// actionType is an ActionIdentifier; before is the tile that was replaced (may be null).
		void onTileCommitted(Editor &editor, int actionType, const Position &pos, const Tile* before);
		// A whole-map operation changed the map behind the undo queue: send everyone a fresh copy.
		void onWholeMapOperation(Editor* editor);
		// Viewers cannot edit; remote updates being applied always pass.
		bool canEdit(const Editor* editor) const;
		// The host saved the shared map: participants that keep a copy save theirs too.
		void onHostSaved(Editor* editor);

		// ---- history (host and admins) ----
		bool canSeeHistory() const;
		// beforeId 0 loads the newest page, otherwise the page before that entry. A non-empty
		// userFilter only lists that user's entries (the host does the filtering), and keeps
		// applying to entries that arrive later.
		void requestHistory(int64_t beforeId, const std::string &userFilter = std::string());
		void revertEntry(int64_t entryId, bool force);
		void reapplyEntry(int64_t entryId, bool force);
		// Admins ask the host to save the map.
		void requestSave();
		// Named restore points and bulk reverts (host and admins).
		void markRestorePoint(const std::string &name);
		// Puts the map back to how it was at that restore point (an entry with kRestorePoint).
		void restoreToPoint(int64_t entryId);
		// Reverts everything a user did in the last minutes, newest first.
		void revertRecent(const std::string &user, int minutes, bool force);
		// Writes the loaded history (all of it on the host) as CSV.
		bool exportHistoryCsv(const std::string &path);
		const std::vector<JournalEntry> &history() const noexcept {
			return historyList;
		}
		// Everybody who has edited the map (the filter choices), not only the loaded page.
		const std::vector<std::string> &historyUsers() const noexcept {
			return historyUserNames;
		}
		std::function<void()> onHistoryChanged;
		std::function<void(const std::string &)> onHistoryResult;

		// Short messages drawn over the map (somebody joined, mentioned you...).
		void pushToast(const std::string &text);
		const std::deque<Toast> &toasts() const noexcept {
			return toastQueue;
		}
		// "[3] " in front of the tab title of the shared map, empty for other maps.
		std::string titleMark(const Editor* editor) const;

		void sendChat(const std::string &text);
		void onLocalCursor(const Position &pos, uint8_t brushSize, bool mouseDown);
		void setRole(uint32_t userId, Role role);
		void kick(uint32_t userId);

		// ---- following and gathering ----
		// Follows another participant's camera until the user moves their own.
		void follow(uint32_t userId);
		void stopFollowing();
		uint32_t followedUser() const noexcept {
			return followId;
		}
		// Host and admins: everybody's camera goes to where mine is.
		void summonAll();

		// ---- presence ----
		const std::unordered_map<uint32_t, Presence> &presences() const noexcept {
			return remotePresence;
		}
		// Where the others' cameras are (the minimap shows them).
		const std::unordered_map<uint32_t, Position> &views() const noexcept {
			return remoteViews;
		}
		// The chat input changed: others see "typing...".
		void onLocalTyping();

		// ---- reserved areas ----
		void claimArea(const Position &a, const Position &b);
		void releaseMyClaims();
		const std::map<uint32_t, Claim> &claims() const noexcept {
			return claimList;
		}

		// ---- inviting ----
		// Local network addresses of the host, for the invite.
		const std::vector<std::string> &lanAddresses() const noexcept {
			return lanList;
		}

		// ---- revert preview (host and admins) ----
		void previewEntry(int64_t entryId, bool reapply, bool force);
		void clearPreview();
		const std::vector<Position> &previewChanges() const noexcept {
			return previewChangeList;
		}
		const std::vector<Position> &previewConflicts() const noexcept {
			return previewConflictList;
		}

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
		bool sharesMap() const noexcept {
			return shareMapFlag;
		}

		// Set by the panel. Called on the GUI thread.
		std::function<void()> onChanged;
		std::function<void(const ChatLine &)> onChat;
		// Somebody's selection, tool or typing state changed.
		std::function<void()> onPresenceChanged;
		// Comments were added, changed or removed by somebody else.
		std::function<void()> onCommentsChanged;
		// A remote cursor moved: the map views need a repaint.
		std::function<void()> onCursorsChanged;
		// Claims or the revert preview changed: the map views need a repaint.
		std::function<void()> onOverlayChanged;

		// Network callbacks (already on the GUI thread). Public only for the CallAfter lambdas.
		void handleAccepted(const Connection::Ptr &conn, uint64_t generation);
		void handleReady(const Connection::Ptr &conn, uint64_t generation);
		void handleFrame(const Connection::Ptr &conn, const std::vector<uint8_t> &frame, uint64_t generation);
		void handleClosed(const Connection::Ptr &conn, const std::string &reason, uint64_t generation);
		void onPumpTick();
		void onMetaTick();
		void onCursorTick();
		void onViewTick();

	private:
		struct Peer {
			Connection::Ptr conn;
			uint32_t userId = 0;
			bool hello = false;
			bool dropping = false; // closing: ignore its frames, no more broadcasts
			// Map download: other traffic waits until the whole snapshot is out, so the peer
			// never sees an update before the map it applies to.
			bool streaming = false;
			bool resyncAfter = false; // a whole-map operation happened while it was downloading
			std::shared_ptr<const std::string> snapshot;
			size_t snapshotSent = 0;
			std::vector<std::pair<Msg, std::vector<uint8_t>>> deferred;
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
		void sendTo(Peer &peer, Msg type, const std::vector<uint8_t> &payload);
		void startStreaming(Peer &peer, std::shared_ptr<const std::string> snapshot);
		void resyncPeer(Peer &peer, std::shared_ptr<const std::string> snapshot);
		void runResync();
		void pumpSnapshots();
		void finishJoin();
		int confirmStopPrompt();
		Peer* peerForUser(uint32_t userId);
		uint32_t pickColor(uint32_t preferred = 0) const;
		std::string uniqueName(const std::string &wanted) const;
		static bool mayChangeRole(const User &actor, const User &target, Role newRole);
		static bool mayKick(const User &actor, const User &target);
		void applyRole(const User &actor, uint32_t target, Role role);
		void applyKick(const User &actor, uint32_t target);

		// Client side
		void clientFrame(const std::vector<uint8_t> &frame);
		void clientSnapshotFrame(Msg type, ByteReader &reader);
		void clientTileUpdate(ByteReader &reader);
		void clientMetaOps(ByteReader &reader);
		void clientClaims(ByteReader &reader);

		// Presence
		Presence currentPresence() const;
		void sendPresenceTo(Peer &peer);

		// Views, claims
		Position currentView() const;
		void applyFollow(const Position &view);
		void applySummon(const std::string &who, const Position &target);
		void hostAddClaim(const User &actor, const Position &a, const Position &b);
		void hostRemoveClaim(const User &actor, uint32_t claimId);
		void removeClaimsOf(uint32_t userId);
		void warnAboutClaims(const std::map<Position, std::string> &tiles);

		// History and saving
		struct RevertOutcome {
			bool ok = false;
			uint32_t applied = 0;
			uint32_t skipped = 0;
			std::vector<Position> conflicts;
			std::vector<Position> changes; // dry runs only: the tiles that would change
			std::string message;
		};
		RevertOutcome executeRevert(const User &actor, int64_t entryId, bool reapply, bool force, bool dryRun = false);
		void runHistoryAction(int64_t entryId, bool reapply, bool force);
		RevertOutcome executeRestore(const User &actor, int64_t pointId);
		RevertOutcome executeRevertRecent(const User &actor, const std::string &user, int minutes, bool force);
		void applyAsAction(const User &actor, std::vector<Tile*> &tiles, const std::string &label);
		void replyHistoryResult(Peer &peer, int64_t entryId, const RevertOutcome &outcome);
		void recordHistory(uint32_t origin, int actionType, const std::vector<JournalTile> &rows);
		void recordMetaInfo(uint32_t origin, const std::vector<MetaOp> &ops);
		void publishEntry(const JournalEntry &entry);
		void mergeHistory(const std::vector<JournalEntry> &entries, bool replace);
		void hostHistoryFrame(Peer &peer, const User &actor, Msg type, ByteReader &reader);
		void clientHistoryFrame(Msg type, ByteReader &reader);
		void saveLocalCopy();
		void sendCursor(const Position &pos, uint8_t brushSize, bool mouseDown);

		// Live replication
		Editor* boundEditor() const noexcept {
			return hostEditor ? hostEditor : clientEditor;
		}
		void scheduleFlush();
		void flushPending(uint32_t origin, uint32_t originSeq);
		void beginApply(uint32_t origin, uint32_t seq);
		void endApply();
		std::vector<Tile*> readTiles(ByteReader &reader, uint32_t count, Map &map);
		void applyTiles(std::vector<Tile*> &tiles, uint32_t origin, uint32_t seq);
		void hostTileBatch(Peer &peer, const User &actor, ByteReader &reader);
		void hostMetaOps(Peer &peer, const User &actor, ByteReader &reader);
		bool mayApplyMeta(const User &actor, const MetaOp &op);
		void correctMeta(Peer &peer, const std::vector<MetaOp> &rejected);
		void sendTilesTo(Peer &peer, const std::vector<Position> &positions);
		void notifyAboutComment(const MetaOp &op, const std::optional<std::string> &previous);
		void applyIncomingMeta(const std::vector<MetaOp> &ops);
		void sendMetaOps(const std::vector<MetaOp> &ops, const Connection* except);
		void clientWelcome(ByteReader &reader);

		State currentState = State::Idle;
		uint64_t generation = 0;
		uint32_t selfId = 0;
		uint16_t listenPort = 0;
		Role defaultRole = Role::Editor;
		std::string selfName;
		Editor* hostEditor = nullptr; // hosting: the shared map
		Editor* clientEditor = nullptr; // joined: our copy of it
		bool shareMapFlag = false;
		bool saveOnParticipantsFlag = false;
		std::string sessionMapName;
		bool welcomed = false;
		struct Download {
			uint32_t total = 0;
			uint32_t chunks = 0;
			uint32_t received = 0;
			bool begun = false;
			std::string data;
		} download;
		std::unique_ptr<wxTimer> pumpTimer;
		std::unique_ptr<wxTimer> metaTimer;
		std::unique_ptr<wxTimer> cursorTimer;
		std::unique_ptr<wxTimer> viewTimer;
		uint32_t followId = 0;
		Position lastFollowApplied;
		Position lastSentView;
		std::unordered_map<uint32_t, Position> remoteViews;
		std::map<uint32_t, Claim> claimList;
		uint32_t nextClaimId = 1;
		std::chrono::steady_clock::time_point lastClaimWarning;
		std::vector<std::string> lanList;
		std::vector<Position> previewChangeList;
		std::deque<Toast> toastQueue;
		std::unordered_map<uint32_t, Presence> remotePresence;
		Presence lastSentPresence;
		bool presenceSent = false;
		bool everTyped = false;
		std::chrono::steady_clock::time_point lastTyping;
		std::vector<Position> previewConflictList;
		struct DeferredCursor {
			Position pos;
			uint8_t brushSize = 0;
			bool mouseDown = false;
			bool valid = false;
		} deferredCursor;

		// Tiles changed locally and not sent yet, first "before" state per tile (host only).
		struct Pending {
			std::map<Position, std::string> before;
			int type = 0;
		} pending;
		bool flushScheduled = false;
		bool applying = false; // a remote update is being applied: do not send it back
		uint32_t applyOrigin = 0;
		uint32_t applySeq = 0;
		uint32_t clientSeq = 0;
		std::map<Position, uint32_t> lastSentSeq; // newest local edit per tile still in flight
		MetaState lastSynced;
		PendingHouseTiles pendingHouseTiles;
		bool resyncScheduled = false;
		bool resyncing = false; // client: a new snapshot of the same session is downloading

		std::unique_ptr<Journal> journal; // host only
		std::vector<JournalEntry> historyList; // newest first
		std::vector<std::string> historyUserNames;
		std::string historyFilter; // empty: all users
		void noteHistoryUser(const std::string &name);
		std::string revertLabel; // label of the revert being applied, empty otherwise
		std::string localSavePath; // shared copy: where "also save on participants' machines" writes
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
