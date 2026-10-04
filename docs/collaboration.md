# Collaborative map editing

The **Collaborate** menu (between *Scripts* and *MCP*) lets several editors work on the same map at the same time. One editor hosts a session, the others join it. Everything below happens in the **Collaborate** panel, which docks on the right like the MCP panel.

This replaces the old, hidden "Live" feature, which has been removed.

## Hosting

1. Open the map you want to share.
2. Collaborate → *Host Session...* (or open the panel and use the **Session** tab).
3. Set a **password** (mandatory, at least 6 characters), a port (default `31313`) and what joiners can do (*Editor*, *Viewer* or *Commenter*). An optional **viewer password** lets people in as Viewers only: give one password to the team and the other to the people who should just look.
   * Choose what **Editors** may change: houses/towns/waypoints/zones, the map properties, and spawns/monsters/npcs. Admins and the host can always change everything. What an editor is not allowed to change is not applied, and their map is put back the way it really is.
   * **Autosave** (minutes) makes the host save the map by itself while somebody is connected (the map needs a file).
4. Decide whether to **share the map**:
   * **Off (default): protected copy.** Participants see and edit the map while connected, but cannot save, export, copy out of it, or use Lua/MCP on it. Nothing is ever written to their disk. When they disconnect (or the host stops), their copy is discarded without asking to save.
   * **On: shared copy.** Participants can *Save As* a local copy whenever they want. If you also tick **Also save on participants' machines**, every time the host saves, each participant's copy is saved too (they choose the file once, later saves are silent).
5. *Start hosting*. Give the participants your address, the port and the password.

Once the session runs, the panel lists the host's local network addresses with a **Copy invite** button (it copies `address:port`; send the password separately). Pasting `address:port` into the Address field of the Join form fills the port too.

The host binds `0.0.0.0:<port>`. Over the internet you need to forward the port on your router, or use a VPN such as Tailscale or ZeroTier. There is no relay server.

Stopping the session, closing the map tab or closing the editor while participants are connected asks first.

## Joining

*Join Session...* → address, port, password. The editor needs the same client data loaded as the host (the map version is checked, the join is refused with an explanation otherwise). The map is then downloaded, which can take a while for a big map; the panel shows the progress.

## What is replicated

* **Every tile edit**, from any user, from any tool: brushes, paste, delete, undo/redo, MCP, Lua. This covers ground, items, house ids, PZ / No-PVP / No-logout / PVP-zone flags, zones on tiles, monsters, monster spawns, NPCs and NPC spawns.
* **Houses, towns, waypoints, zones and the map properties** (description, size). These are synchronized about twice per second.
* **Whole-map operations** (borderize map, randomize map, import, clean-ups...) can only be run by the host. They resend the whole map to the participants.
* **Cursors**: each participant has a color; you see where the others are pointing (a colored tile with their name) and, while the mouse is down, the footprint of their brush.
* **Chat** and comments.

The host is authoritative: when two people change the same tile at the same moment, the host's order wins and everybody ends up with the same result. Ctrl+Z only undoes *your own* edits, and it can overwrite what someone else did to the same tile afterwards; use the History tab to revert safely.

## Working together

* **Follow:** right-click a participant → *Follow*. Your camera (position and floor) tracks theirs until you move your own camera.
* **Bring everyone here** (host and admins): moves every participant's camera to where yours is.
* **Reserved areas:** select tiles and press *Reserve selection* to mark them as yours (one floor, at most 8 areas per person). Everybody sees the rectangle with your name, and editing inside somebody else's area shows a notice in the status bar. It is a hint, nothing is blocked. *Release my areas* removes them; the host and admins can remove anyone's, and an area disappears with its owner's connection.

## Roles

| Role | Edit the map | Chat | Write comments | Edit others' comments | Revert / reapply | Kick / change roles |
|---|---|---|---|---|---|---|
| Host | yes | yes | yes | yes | yes | yes |
| Admin | yes | yes | yes | yes | yes | Editors, Viewers and Commenters only |
| Editor | yes (within what the host allows) | yes | yes | own only | no | no |
| Commenter | no | yes | yes | own only | no | no |
| Viewer | no | yes | no | no | no | no |

Anybody can mark a thread resolved or open again.

The host changes roles (and kicks) from the participants list context menu.

## History (host and admins)

The host records every edit per user in a SQLite file named `<map>.collab.sqlite` next to the map file (for a map that was never saved: `<user data dir>/collab/<timestamp>.sqlite`). A brush stroke is one entry. Houses, towns and so on are listed as informational entries and cannot be reverted.

In the **History** tab:

* **Revert** puts back the tiles an entry changed. A tile that someone else changed *after* that entry is **skipped** and reported as a conflict, unless **Force** is ticked.
* **Reapply** undoes a revert, with the same conflict rules.
* The revert itself is a normal edit by whoever did it: it is replicated and shows up as a new entry.
* **Preview** shows on the map what a revert (or reapply) would do before you apply it: yellow tiles would change, red tiles are conflicts that would be skipped. *Clear preview* removes the overlay; reverting clears it too.
* **Mark restore point...** gives the current moment a name. **Restore to point** puts the whole map back to how it was then, undoing everything edited after it (also things that were reverted before), for everybody.
* **Revert user's last minutes** reverts everything the user picked in the filter did in the last N minutes, newest first.
* **Export CSV** writes the history (all of it on the host, the loaded entries for an admin).
* The history survives restarting the host with the same map.

## Comments

*Show comments* (View menu, `Shift+C`) shows markers on the map; right-click a tile to add, edit, reply to, resolve or delete a comment. A comment has a **type** (note, bug, idea, to do; the marker color follows it), an optional **assignee** and can have **replies**, which form a thread. Writing `@name` mentions somebody: they get a toast and a line in the chat, and so does the person a comment is assigned to. In the Comments tab you can filter by type and by "assigned to me", search, and jump with *Next open*.

Comments are stored in `<map>-comments.xml` next to the map, so they work with or without a session, and the file is not part of the OTBM. In a session they are shared with everybody like the rest of the map data.

## Staying connected

* The panel shows the **round trip time** (the host sees everybody's), warns when the connection is slow and says whether your edits are **synced** with the host.
* If the connection drops, the editor **reconnects by itself** (up to 8 attempts, with growing pauses). Your copy of the map stays open but read-only meanwhile; when you are back in, the host sends the map again and your view is kept. The password is only kept in memory for this and wiped when the session ends.
* A big map is **compressed on a background thread** and a map nobody has changed since it was last compressed is sent again as it is, so joining no longer freezes the host for as long. Serializing the map still happens on the interface thread, as in a save.

## Small things

* Your **color** is yours to pick (next to your name); the host keeps it unless somebody already has it.
* *Collaborate → Show Cursors / Show Names* toggle the cursors without opening the panel. Joins, leaves and mentions appear as short messages over the map; there is an option for a sound when a chat message arrives (`data/sounds/message-notification.mp3`; Windows plays the file, other systems use the system beep).
* The tab title shows how many people are in the session, and the selections, tools and typing state of the others are visible (the minimap marks where their cameras are).

## Security

* Every message is encrypted and authenticated (libsodium, XChaCha20-Poly1305 streams, keys derived from the password with Argon2id). The password is never sent. A wrong password is detected by the host on the first message.
* Everything received from the network is treated as hostile: lengths are bounded, readers are bounds-checked, and the host re-checks roles for every request.
* **Protected copies are a convenience guarantee, not DRM.** The editor will not save, export, copy or script a protected map and leaves no file behind, but nothing can stop a determined user from taking screenshots or dumping the memory of the process.
* The password is never stored in the settings.

## Not included (yet)

* A relay or NAT traversal server; resuming only the missed changes after a reconnect (the whole map is sent again).
* Showing what a participant is about to paste.
* Reverting house, town, waypoint or zone changes (they are only listed).
* Locking areas or merging edits finer than a tile.
* More than one session per editor.
* Comments inside `.otgz` archives.

## Self-check

`canary-map-editor-x64.exe --collab-selfcheck` runs a headless check of the protocol primitives, the crypto, the snapshot and tile codecs, the metadata diff and the history journal, and exits with 0 when everything passes.
