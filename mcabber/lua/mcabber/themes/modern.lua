-- A modern look: rounded frames, a dark palette (24-bit colors, which are
-- approximated on terminals with less colors), status icons and compact
-- formats.
-- Usage: mc.theme(require "mcabber.themes.modern")
--
-- The palette is inspired by "Tokyo Night".
local c = {
  bg      = "#1a1b26",
  bg_alt  = "#24283b",
  bg_sel  = "#2f3549",
  fg      = "#c0caf5",
  fg_dim  = "#565f89",
  comment = "#737aa2",
  blue    = "#7aa2f7",
  cyan    = "#7dcfff",
  green   = "#9ece6a",
  yellow  = "#e0af68",
  orange  = "#ff9e64",
  red     = "#f7768e",
  magenta = "#bb9af7",
}

return {
  name = "modern",

  options = {
    ui_borders = "rounded",
    ui_title_roster = "Contacts",
    ui_title_log = "Log",
    roster_width = 26,
    log_win_height = 4,
    nick_colors = c.blue .. " " .. c.green .. " " .. c.yellow .. " " ..
                  c.magenta .. " " .. c.cyan .. " " .. c.orange,
  },

  background   = c.bg,
  bgstatus     = c.bg_alt,
  bgrostersel  = c.bg_sel,
  general      = { fg = c.fg },
  msgin        = { fg = c.fg },
  msgout       = { fg = c.cyan },
  msghl        = { fg = c.orange, bold = true },
  info         = { fg = c.comment },
  timestamp    = { fg = c.fg_dim },
  readmark     = { fg = c.red },
  log          = { fg = c.comment },
  status       = { fg = c.fg },
  roster       = { fg = c.fg },
  rostersel    = { fg = c.fg, bold = true },
  rosterselmsg = { fg = c.red, bold = true },
  rosternewmsg = { fg = c.yellow, bold = true },

  border           = { fg = c.fg_dim },
  title            = { fg = c.blue, bold = true },
  title_active     = { fg = c.blue, bg = c.bg },
  nick_self        = { fg = c.cyan, bold = true },
  badge            = { fg = c.bg, bg = c.yellow, bold = true },
  roster_group     = { fg = c.magenta, bold = true },
  roster_unread    = { fg = c.yellow, bold = true },
  status_online    = { fg = c.green },
  status_free      = { fg = c.green, bold = true },
  status_away      = { fg = c.yellow },
  status_notavail  = { fg = c.orange },
  status_dnd       = { fg = c.red },
  status_invisible = { fg = c.fg_dim },
  status_offline   = { fg = c.fg_dim },
  status_unknown   = { fg = c.fg_dim },
  status_room      = { fg = c.magenta },
  status_room_out  = { fg = c.fg_dim },
  dim              = { fg = c.fg_dim },
  accent           = { fg = c.blue },

  format = {
    time = "%H:%M",
    message = {
      ["in"] = "{#timestamp}{time}{#} "
               .. "{?private}{#accent}⇢{#} {/}"
               .. "{?me}{#nick}* {nick}{#} {/}"
               .. "{?muc}{!me}{#nick}{nick}{#}{#dim}:{#} {/}{/}"
               .. "{!muc}{!me}{#dim}›{#} {/}{/}",
      out    = "{#timestamp}{time}{#} "
               .. "{?me}{#nick_self}* {nick}{#} {/}"
               .. "{!me}{#nick_self}‹{#} {/}"
               .. "{?receipt}{#dim}…{#}{/}",
      info   = "{#timestamp}{time}{#} {#info}•{#} ",
      error  = "{#timestamp}{time}{#} {#status_dnd}!{#} ",
      default = "{#timestamp}{time}{#} ",
    },
    icons = {
      online = "●", free = "●", away = "◐", notavail = "○", dnd = "⊘",
      invisible = "◌", offline = "·", unknown = "?", room = "#",
      room_out = "#",
    },
    roster = {
      contact = " {?pending}{#roster_unread}{pending}{/}{!pending} {/}"
                .. "{#status_$status}{icon}{#} {name:*}"
                .. "{>}{?unread} {#badge} {unread} {#}{/}",
      room    = " {?pending}{#roster_unread}{pending}{/}{!pending} {/}"
                .. "{#status_$status}{icon}{#} {name:*}"
                .. "{>}{?unread} {#badge} {unread} {#}{/}",
      group   = " {#roster_group}{?folded}▸{/}{!folded}▾{/} {name:*}{#}"
                .. "{?folded}{>}{#dim}{count}{#} {/}",
      special = " {?pending}{#roster_unread}{pending}{/}{!pending} {/}"
                .. "{#accent}≡{#} {name:*}",
    },
    chat_status = " {?locked}{#roster_unread}⏸{#} {/}"
                  .. "{#status_$status}{icon}{#} {#title}{name:*}{#}"
                  .. "{?resource}{#dim}/{resource}{#}{/}"
                  .. "{?msg} {#dim}— {msg:*}{#}{/} "
                  .. "{>}{?chatstate} {#dim}{chatstate}{#} {/}",
    main_status = " {?unread}{#roster_unread}{unread}{#} {/}"
                  .. "{#status_$status}{icon}{#} {status}"
                  .. "{?msg}{#dim} — {msg:*}{#}{/}"
                  .. "{>}{?info}{info} {/}{#dim}{jid}{#} ",
  },
}
