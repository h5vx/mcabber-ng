---@meta mcabber-ng
-- Type annotations (LuaCATS) of the mcabber-ng configuration API, for
-- lua-language-server: completion and checks while editing init.lua.
-- See "man mcabber-ng" (CONFIGURATION FILE, EVENTS, MESSAGE FORMAT).
--
-- ~/.config/mcabber-ng/.luarc.json:
--   {
--     "runtime.version": "LuaJIT",
--     "workspace.library": [ "/usr/share/mcabber-ng/types",
--                            "/usr/share/mcabber-ng/lua" ]
--   }

---The mcabber-ng API.
---@class mc
mc = {}

---Version of mcabber-ng.
---@type string
mc.version = nil

---Lua implementation: "LuaJIT" or "Lua 5.1".
---@type string
mc.lua_impl = nil

---Configuration directory (~/.config/mcabber-ng).
---@type string
mc.config_dir = nil

---Configuration file being used.
---@type string
mc.config_file = nil

---Return value of an event handler: drop the event (when possible).
mc.DROP = "drop"

---Return value of an event handler: don't run the next handlers.
mc.STOP = "stop"

-- Output --------------------------------------------------------------------

---Print the arguments to the status buffer.
---@param ... any
function mc.print(...) end

---Print the arguments to the log window and the trace log file.
---@param ... any
function mc.log(...) end

---Print the arguments when debugging is enabled (tracelog_level).
---@param ... any
function mc.debug(...) end

-- Options -------------------------------------------------------------------

---@alias mc.OptionValue string|number|boolean

---Set an option, or several with a table.  A boolean is stored as 1 or 0,
---nil removes the option.
---@overload fun(options: mc.Options)
---@param name string
---@param value mc.OptionValue|nil
function mc.set(name, value) end

---Read an option (all the values are strings).
---@param name string
---@return string|nil
function mc.get(name) end

---The options as fields: mc.opt.roster_width = 30, mc.opt.jid...
---@type mc.Options
mc.opt = nil

---The options of mcabber-ng (see init.lua.example and the manual page).
---Any other name can be set as well.
---@class mc.Options
---@field attention_char? string
---@field autoaway? integer|boolean
---@field autoreconnect? integer|boolean
---@field beep_on_message? integer|boolean
---@field block_unsubscribed? integer|boolean
---@field buddy_format? integer|boolean
---@field buddy_me_fulljid? integer|boolean
---@field buffer_smart_scrolling? integer|boolean
---@field caps_directory? string
---@field carbons? integer|boolean
---@field clear_unread_on_carbon? integer|boolean
---@field cmdhistory_lines? integer|boolean
---@field color_background? string
---@field color_bgrostersel? string
---@field color_bgstatus? string
---@field color_general? string
---@field color_info? string
---@field color_log? string
---@field color_msghl? string
---@field color_msgin? string
---@field color_msgout? string
---@field color_readmark? string
---@field color_roster? string
---@field color_rosternewmsg? string
---@field color_rostersel? string
---@field color_rosterselmsg? string
---@field color_status? string
---@field color_timestamp? string
---@field completion_ignore_case? integer|boolean
---@field default_server? string
---@field delete_on_reject? integer|boolean
---@field disable_chatstates? integer|boolean
---@field disable_random_resource? integer|boolean
---@field editor? string
---@field editor_tmux? "split"|"window"|"popup"|"off"
---@field editor_tmux_args? string
---@field escdelay? integer|boolean
---@field event_log_dir? string
---@field event_log_files? integer|boolean
---@field eventcmd_checkstatus? integer|boolean
---@field eventcmd_use_nickname? integer|boolean
---@field events_command? string
---@field events_ignore_active_window? integer|boolean
---@field fifo_hide_commands? integer|boolean
---@field fifo_ignore? integer|boolean
---@field fifo_name? string
---@field gpg_home? string
---@field gpg_path? string
---@field help_dirs? string
---@field help_to_current? integer|boolean
---@field ignore_self_presence? integer|boolean
---@field info? string
---@field input_max_rows? integer|boolean
---@field input_position? "chat"|"bottom"
---@field iq_hide_requests? integer|boolean
---@field iq_last_disable? integer|boolean
---@field iq_last_disable_when_notavail? integer|boolean
---@field iq_time_hide? integer|boolean
---@field iq_version_hide? integer|boolean
---@field iq_version_hide_os? integer|boolean
---@field iq_version_hide_version? integer|boolean
---@field jid? string
---@field lang? string
---@field load_logs? integer|boolean
---@field load_muc_logs? integer|boolean
---@field log_display_presence? integer|boolean
---@field log_display_sender? integer|boolean
---@field log_muc_conf? integer|boolean
---@field log_win_height? integer|boolean
---@field log_win_on_top? integer|boolean
---@field logging? integer|boolean
---@field logging_dir? string
---@field logging_ignore_status? integer|boolean
---@field lua_timeout_ms? integer|boolean
---@field max_history_age? integer|boolean
---@field max_history_blocks? integer|boolean
---@field message? string
---@field message_autoaway? string
---@field message_avail? string
---@field message_away? string
---@field message_dnd? string
---@field message_free? string
---@field message_notavail? string
---@field modules_dir? string
---@field muc_auto_whois? integer|boolean
---@field muc_bookmark_autoupdate? integer|boolean
---@field muc_completion_suffix? string
---@field muc_disable_nick_hl? integer|boolean
---@field muc_flag_joins? integer|boolean
---@field muc_flag_on_join? integer|boolean
---@field muc_members? "column"|"overlay"|"none"
---@field muc_members_left? integer|boolean
---@field muc_members_width? integer|boolean
---@field muc_print_jid? integer|boolean
---@field muc_print_status? integer|boolean
---@field nick_colors? string
---@field nickname? string
---@field omemo? integer|boolean
---@field omemo_policy? "auto"|"manual"
---@field omemo_trust? "btbv"|"manual"
---@field otr? integer|boolean
---@field otr_dir? string
---@field password? string
---@field password_eval? string
---@field paste_bracketed? integer|boolean
---@field paste_burst_min? integer|boolean
---@field paste_detect? integer|boolean
---@field paste_mode? "insert"|"confirm"|"send_lines"
---@field paste_timeout_ms? integer|boolean
---@field pgp? integer|boolean
---@field pgp_passphrase? string
---@field pgp_passphrase_retries? integer|boolean
---@field pgp_private_key? string
---@field pinginterval? integer|boolean
---@field port? integer|boolean
---@field priority? integer|boolean
---@field priority_away? integer|boolean
---@field proxy_host? string
---@field proxy_pass? string
---@field proxy_port? integer|boolean
---@field proxy_user? string
---@field resource? string
---@field roster_autolock_resource? integer|boolean
---@field roster_bookmarks? integer|boolean
---@field roster_display_filter? string
---@field roster_hide_domain? integer|boolean
---@field roster_no_leading_space? integer|boolean
---@field roster_show_unread_count? integer|boolean
---@field roster_width? integer|boolean
---@field roster_win_on_right? integer|boolean
---@field server? string
---@field show_status_in_buffer? integer|boolean
---@field spell_enable? integer|boolean
---@field spell_encoding? string
---@field spell_lang? string
---@field ssl? integer|boolean
---@field ssl_ca? string
---@field ssl_ciphers? string
---@field ssl_fingerprint? string
---@field ssl_ignore_checks? integer|boolean
---@field statefile? string
---@field time_prefix? integer|boolean
---@field tls? integer|boolean
---@field tracelog_file? string
---@field tracelog_level? integer|boolean
---@field ui_borders? "none"|"ascii"|"single"|"rounded"|"double"|"heavy"
---@field ui_colors? "8"|"16"|"256"|"truecolor"
---@field ui_title_log? string
---@field ui_title_roster? string
---@field url_hint_chars? string
---@field url_open_command? string
---@field url_regex? string
---@field use_mouse? integer|boolean
---@field vi_mode? integer|boolean

-- Commands, aliases, key bindings ---------------------------------------------

---Run a command; the leading "/" is optional.
---@param line string
---@return boolean ok
function mc.command(line) end

---Same as mc.command.
---@param line string
---@return boolean ok
function mc.cmd(line) end

---Define an alias ("/name" runs command), or several with a table.
---@overload fun(aliases: table<string, string>)
---@param name string
---@param command string
function mc.alias(name, command) end

---What a key does: a command (without "/") or a Lua function.
---@alias mc.KeyAction string|fun()

---Bind a key, or several with a table.  Key names: "Alt+j", "Ctrl+Up",
---"F5", "Shift+Tab", "Ctrl+Alt+x", "Alt+ж"...
---@overload fun(bindings: table<string, mc.KeyAction>)
---@param key string
---@param action mc.KeyAction
function mc.bind(key, action) end

---Remove the binding of a key.
---@param key string
function mc.unbind(key) end

---The current key bindings.
---@return table<string, mc.KeyAction>
function mc.bindings() end

---Canonical name of a key ("alt+J" -> "Alt+j", legacy codes like "M106").
---@param name string
---@return string|nil name
---@return string|nil error
function mc.key_name(name) end

-- Timers --------------------------------------------------------------------

---Call fn after ms milliseconds (every ms milliseconds if repeat is true:
---returning false stops it).
---@param ms integer
---@param fn fun(): boolean?
---@param rep boolean?
---@return integer id
function mc.timer(ms, fn, rep) end

---Cancel a timer.
---@param id integer
function mc.cancel(id) end

-- Messages and status -----------------------------------------------------

---@class mc.SendOptions
---@field subject string?

---Send a message to a JID.
---@param jid string
---@param text string
---@param opts mc.SendOptions?
---@return boolean ok
function mc.send(jid, text, opts) end

---Send a message to the current buddy.
---@param text string
function mc.say(text) end

---@alias mc.Status "online"|"free"|"dnd"|"notavail"|"away"|"invisible"|"offline"

---Set your status: mc.status("away", "Lunch").
---@param status mc.Status|"avail"
---@param message string?
function mc.status(status, message) end

---Quote a command argument.
---@param s any
---@return string
function mc.quote(s) end

---Quote a string for the shell (os.execute, io.popen).
---@param s any
---@return string
function mc.shell_quote(s) end

-- Current buddy and input line ----------------------------------------------

mc.buddy = {}

---@class mc.Buddy
---@field jid string
---@field name string
---@field type "user"|"room"|"agent"|"group"|"special"
---@field status mc.Status?
---@field resource string?

---The selected buddy.
---@return mc.Buddy|nil
function mc.buddy.current() end

mc.input = {}

---Text of the input line.
---@return string
function mc.input.get() end

---Replace the text of the input line.
---@param text string
function mc.input.set(text) end

---Insert text at the cursor position.
---@param text string
function mc.input.insert(text) end

-- Colors and modules --------------------------------------------------------

mc.color = {}

---Color of the roster items: statuses (e.g. "ofd" or "*"), JID pattern.
---@param statuses string
---@param jid string
---@param color string
function mc.color.roster(statuses, jid, color) end

---Remove the roster color rules.
function mc.color.roster_clear() end

---Nickname coloring mode of a room ("*" for all the rooms).
---@param room string
---@param mode "on"|"preset"|"off"|"-"|nil
function mc.color.muc(room, mode) end

---Color of a nickname in the rooms.
---@param nick string
---@param color string
function mc.color.mucnick(nick, color) end

mc.module = {}

---@param name string
function mc.module.load(name) end

---@param name string
function mc.module.unload(name) end

-- Events ----------------------------------------------------------------------

---@class mc.OnOptions
---@field priority integer? Handlers with a lower priority run first.

---What a handler can return: mc.DROP (or false) drops the event, mc.STOP
---stops the next handlers.
---@alias mc.HandlerResult "drop"|"stop"|false|nil

---@class mc.Event
---@field event string Name of the event

---@class mc.MessageInEvent: mc.Event
---@field jid string
---@field resource string?
---@field nick string? Sender in a room
---@field text string Can be changed
---@field muc boolean Room message
---@field private boolean Private message from a room participant
---@field time integer
---@field delayed boolean
---@field carbon boolean
---@field error boolean
---@field encrypted "omemo"|"pgp"|"otr"|nil

---@class mc.MessageReceivedEvent: mc.MessageInEvent
---@field attention boolean
---@field highlight boolean Your nickname is in a room message
---@field own boolean Your message in a room
---@field active boolean The window of this contact is open

---@class mc.MessageSendEvent: mc.Event
---@field jid string
---@field bare_jid string
---@field text string Can be changed
---@field subject string? Can be changed
---@field muc boolean
---@field private boolean

---@class mc.MessageOutEvent: mc.Event
---@field jid string
---@field nick string?
---@field text string
---@field muc boolean
---@field private boolean
---@field time integer
---@field carbon boolean Sent by another of your clients
---@field encrypted "omemo"|"pgp"|"otr"|nil

---@class mc.StatusEvent: mc.Event
---@field jid string
---@field resource string?
---@field old mc.Status
---@field new mc.Status
---@field message string?
---@field priority integer
---@field time integer

---@class mc.MyStatusEvent: mc.Event
---@field old mc.Status
---@field new mc.Status
---@field message string?

---@class mc.UnreadEvent: mc.Event
---@field unread integer
---@field attention integer
---@field muc_unread integer
---@field muc_attention integer

---@class mc.SubscriptionEvent: mc.Event
---@field type "subscribe"|"unsubscribe"|"subscribed"|"unsubscribed"
---@field jid string
---@field message string?

---@class mc.ReceiptEvent: mc.Event
---@field jid string
---@field id string

---@class mc.UrlOpenEvent: mc.Event
---@field url string Can be changed
---@field command string? The command given to /url

---Add an event handler; returns its id (for mc.off).
---@overload fun(event: "message_in", fn: fun(ev: mc.MessageInEvent): mc.HandlerResult, opts: mc.OnOptions?): integer
---@overload fun(event: "message_received", fn: fun(ev: mc.MessageReceivedEvent): mc.HandlerResult, opts: mc.OnOptions?): integer
---@overload fun(event: "message_send", fn: fun(ev: mc.MessageSendEvent): mc.HandlerResult, opts: mc.OnOptions?): integer
---@overload fun(event: "message_out", fn: fun(ev: mc.MessageOutEvent): mc.HandlerResult, opts: mc.OnOptions?): integer
---@overload fun(event: "status", fn: fun(ev: mc.StatusEvent): mc.HandlerResult, opts: mc.OnOptions?): integer
---@overload fun(event: "my_status", fn: fun(ev: mc.MyStatusEvent): mc.HandlerResult, opts: mc.OnOptions?): integer
---@overload fun(event: "unread", fn: fun(ev: mc.UnreadEvent): mc.HandlerResult, opts: mc.OnOptions?): integer
---@overload fun(event: "subscription", fn: fun(ev: mc.SubscriptionEvent): mc.HandlerResult, opts: mc.OnOptions?): integer
---@overload fun(event: "receipt", fn: fun(ev: mc.ReceiptEvent): mc.HandlerResult, opts: mc.OnOptions?): integer
---@overload fun(event: "url_open", fn: fun(ev: mc.UrlOpenEvent): mc.HandlerResult, opts: mc.OnOptions?): integer
---@overload fun(event: "connect"|"disconnect"|"ready"|"quit", fn: fun(ev: mc.Event): mc.HandlerResult, opts: mc.OnOptions?): integer
---@param event string An event, or "hook-..." for the hooks of the modules
---@param fn fun(ev: table): mc.HandlerResult
---@param opts mc.OnOptions?
---@return integer id
function mc.on(event, fn, opts) end

---Remove an event handler.
---@param id integer
---@return boolean removed
function mc.off(id) end

-- Styles and themes -----------------------------------------------------------

---Colors: names ("red", "brightblue"), 0-255, "#rrggbb", "default", or
---the theme backgrounds "background", "bgstatus", "bgrostersel".
---@alias mc.Color string|integer

---@class mc.StyleTable
---@field fg mc.Color?
---@field bg mc.Color?
---@field bold boolean?
---@field dim boolean?
---@field italic boolean?
---@field underline boolean?
---@field reverse boolean?
---@field blink boolean?
---@field standout boolean?

---A style: a table, or a string like "fg=#c0caf5 bg=default bold".
---@alias mc.Style string|mc.StyleTable

---Set a style (general, msgin, msgout, msghl, info, timestamp, readmark,
---status, log, roster, rostersel, border, title, nick, badge,
---status_<status>, link_hint... or any name used in formats).  nil or
---false restores the default.
---@param name string
---@param style mc.Style|false|nil
function mc.style(name, style) end

---A theme: styles (name = style), the backgrounds, and optional sections.
---@class mc.Theme
---@field name string?
---@field options mc.Options? Passed to mc.set
---@field format mc.Formats? Passed to mc.format
---@field setup fun()? Called after the styles have been set
---@field background mc.Color?
---@field bgstatus mc.Color?
---@field bgrostersel mc.Color?
---@field [string] mc.Style|false|any Styles (false: the default style)

---Apply a theme: mc.theme(require "mcabber.themes.modern").
---@param t mc.Theme
function mc.theme(t) end

-- Formats ---------------------------------------------------------------------

---A segment of a formatted line: text, or { text, style name }.
---@alias mc.Segment string|{ [1]: string, [2]: string? }

---Fields given to a message format function.
---@class mc.MessageInfo
---@field time integer
---@field mark string The original "<==", "-->", "***"...
---@field nick string?
---@field kind string
---@field me boolean? Action ("/me")
---@field muc boolean? Room message
---@field private boolean? Private message in a room
---@field delayed boolean?
---@field receipt boolean? Waiting for a receipt
---@field carbon boolean?
---@field highlight boolean?
---@field own boolean? Your message in a room
---@field encrypted "omemo"|"pgp"|"otr"|nil
---@field ["in"] boolean?
---@field out boolean?
---@field info boolean?
---@field error boolean?
---@field special boolean?

---A template ("{time} {#nick}{nick:>10.10}{#} | "), templates per kind of
---message (in, out, info, error, special, default), or a function
---(a string, segments, or nil for the default format).
---@alias mc.MessageFormat string|table<string, string>|fun(m: mc.MessageInfo): (string|mc.Segment[]|nil)

---A template, or a function receiving the fields.
---@alias mc.LineFormat string|fun(fields: table): (string|mc.Segment[]|nil)

---Formats of the messages and of the interface (false: the default).
---Assigning a field redraws the buffers; after changing a table in place,
---call mc.format.refresh().
---@class mc.Formats
---@field time string|false? strftime format of the times
---@field indent integer|false? Indentation of the following lines of a message
---@field message mc.MessageFormat|false?
---@field roster mc.LineFormat|table<"contact"|"room"|"group"|"special", string>|false?
---@field chat_status mc.LineFormat|false?
---@field main_status mc.LineFormat|false?
---@field input_prompt mc.LineFormat|false?
---@field members mc.LineFormat|false?
---@field icons table<string, string>|false? Status icons (online, away, room...)
---@field refresh fun()? Redraw after changing a table in place (mc.format only)
mc.format = {}
