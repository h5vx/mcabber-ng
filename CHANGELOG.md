# Changes in mcabber-ng

Each release has a section here: its text is the description of the
GitHub release (see `packaging/release-notes.sh`).

## 1.1.0

### Rooms

- Joining a room again (after a reconnection or a restart) no longer shows
  its last messages again as new ones: the history is requested since the
  last message seen, the messages already received are dropped, and the
  topic is displayed only when it changes.  The history of a room joined
  for the first time is not marked as unread.  This state is kept in
  `~/.config/mcabber-ng/rooms.ini`.
- The password of a room is kept when the nickname is changed and by
  `/room bookmark add`.
- New bookmarks panel: `/bookmarks` lists the room bookmarks; join a room,
  toggle its auto-join, edit, create or delete bookmarks.
  `/bookmarks set|del` changes any bookmark from the command line.

### Connection

- Automatic reconnection: also when the first connection fails, after
  5 s, 10 s, 30 s… up to 5 minutes between the attempts (option
  `autoreconnect`).  After the computer has been suspended, the connection
  is checked and opened again if needed.  The rooms you were in are joined
  again.  `/disconnect` stops the reconnections.

### Configuration and themes

- `/source` can reload the configuration: the event handlers and timers of
  the previous run of the file are removed, and the Lua modules are read
  again by `require`.
- Styles can use the theme backgrounds as colors (`background`,
  `bgstatus`, `bgrostersel`).  The `modern` theme follows them, so it can
  be made transparent:
  `mc.theme(require("mcabber.themes.modern").with { bg = "default" })`.

## 1.0.0

First release of mcabber-ng, a fork of mcabber 1.1.2:

- configuration in Lua (options, key bindings with readable names, event
  handlers, message and interface formats, themes);
- multi-line messages, pasted text is not sent line by line;
- modern interface: frames, 256/24-bit colors, themes, room members
  panel, XMPP console;
- OMEMO encryption of one-to-one chats, OTR, PGP;
- link hints (`/url`), external editor (`/editor`, tmux integration);
- packages: universal `.deb` (any Debian/Ubuntu), Fedora, Arch, static
  binaries for x86_64, x86, aarch64, armv7 and riscv64.
