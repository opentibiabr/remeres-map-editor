# Collaborative map editing

The **Collaborate** menu (between *Scripts* and *MCP*) lets several editors work on the same map at the same time. One editor hosts a session, the others join it. Everything below happens in the **Collaborate** panel, which docks on the right like the MCP panel.

This replaces the old, hidden "Live" feature, which has been removed.

## Hosting

1. Open the map you want to share.
2. Collaborate → *Host Session...* (or open the panel and use the **Session** tab).
3. Set a **password** (mandatory, at least 6 characters), a port (default `31313`) and what joiners can do (*Editor* or *Viewer*).
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

| Role | Edit the map | Chat and comments | Edit others' comments | Revert / reapply | Kick / change roles |
|---|---|---|---|---|---|
| Host | yes | yes | yes | yes | yes |
| Admin | yes | yes | yes | yes | Editors and Viewers only |
| Editor | yes | yes | own only | no | no |
| Viewer | no | yes | own only | no | no |

The host changes roles (and kicks) from the participants list context menu.

## History (host and admins)

The host records every edit per user in a SQLite file named `<map>.collab.sqlite` next to the map file (for a map that was never saved: `<user data dir>/collab/<timestamp>.sqlite`). A brush stroke is one entry. Houses, towns and so on are listed as informational entries and cannot be reverted.

In the **History** tab:

* **Revert** puts back the tiles an entry changed. A tile that someone else changed *after* that entry is **skipped** and reported as a conflict, unless **Force** is ticked.
* **Reapply** undoes a revert, with the same conflict rules.
* The revert itself is a normal edit by whoever did it: it is replicated and shows up as a new entry.
* **Preview** shows on the map what a revert (or reapply) would do before you apply it: yellow tiles would change, red tiles are conflicts that would be skipped. *Clear preview* removes the overlay; reverting clears it too.
* The history survives restarting the host with the same map.

## Comments

*Show comments* (View menu, `Shift+C`) shows markers on the map; right-click a tile to add, edit, resolve or delete a comment. Comments are stored in `<map>-comments.xml` next to the map, so they work with or without a session, and the file is not part of the OTBM. In a session they are shared with everybody.

## Security

* Every message is encrypted and authenticated (libsodium, XChaCha20-Poly1305 streams, keys derived from the password with Argon2id). The password is never sent. A wrong password is detected by the host on the first message.
* Everything received from the network is treated as hostile: lengths are bounded, readers are bounds-checked, and the host re-checks roles for every request.
* **Protected copies are a convenience guarantee, not DRM.** The editor will not save, export, copy or script a protected map and leaves no file behind, but nothing can stop a determined user from taking screenshots or dumping the memory of the process.
* The password is never stored in the settings.

## Not included (yet)

* A relay or NAT traversal server; reconnecting and resuming after a dropped connection.
* Reverting house, town, waypoint or zone changes (they are only listed).
* Locking areas or merging edits finer than a tile.
* More than one session per editor.
* Comments inside `.otgz` archives.

## Self-check

`canary-map-editor-x64.exe --collab-selfcheck` runs a headless check of the protocol primitives, the crypto, the snapshot and tile codecs, the metadata diff and the history journal, and exits with 0 when everything passes.
