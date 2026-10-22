# Changes in mcabber-ng

Each release has a section here: its text is the description of the
GitHub release (see `packaging/release-notes.sh`).

## 1.1.2

- Type annotations of the Lua API (LuaCATS), installed in
  `share/mcabber-ng/types/`: with lua-language-server, the options,
  functions, events and formats are completed and checked while editing
  the configuration (see the README).
- Link hints (`/url`): the labels can be typed whatever the case and the
  keyboard layout (Cyrillic layouts: the letter on the same key).  The
  same for the keys of the `[bookmarks]` buffer.

## 1.1.1

- Rooms: the history sent by a room is displayed again; its messages
  already seen before (and all of it when the room is joined for the first
  time) are not marked as unread, only the ones received while you were
  away.  Joining a room no longer flags it in the roster (option
  `muc_flag_on_join`); joins and leaves are flagged only with
  `muc_flag_joins`, topic changes never.
- The layout options (`log_win_height`, `roster_width`, `ui_borders`…)
  are applied as soon as they change, e.g. after `/source`.
  `log_win_height = 0` hides the log window (its messages are in the
  `[status]` buffer).
- The bookmarks are a special buffer, `[bookmarks]`, in the roster under
  `[status]` (option `roster_bookmarks`); its columns fit their contents
  and the width of the window, and a name equal to the room address is
  shown as a dash.
- In vi mode, the commands put in the input line (bookmark edition) start
  with `:`.
- Release notes: the lines of each paragraph are joined.

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
