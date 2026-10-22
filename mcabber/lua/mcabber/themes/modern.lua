-- A modern look: rounded frames, a dark palette (24-bit colors, which are
-- approximated on terminals with less colors), status icons, compact
-- formats and a main status line made of rounded "islands".
--
-- Usage: mc.theme(require "mcabber.themes.modern")
--
-- The rounded ends of the islands use Powerline glyphs (a Nerd Font is
-- needed).  Other characters can be chosen:
--   mc.theme(require("mcabber.themes.modern").with { caps = { "▐", "▌" } })
--   mc.theme(require("mcabber.themes.modern").with { caps = false })  -- none
--
-- The background can be changed, e.g. to the one of the terminal
-- (transparent):
--   mc.theme(require("mcabber.themes.modern").with { bg = "default" })
-- or afterwards with mc.theme { background = "default", bgstatus = "default" }:
-- the elements of the theme follow these backgrounds.
--
-- The palette is inspired by "Tokyo Night".
local function make(opts)
opts = opts or {}

local c = {
  bg      = opts.bg or "#1a1b26",
  ink     = "#1a1b26",    -- Text on colored backgrounds
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

-- Ends of the islands of the main status line
local caps = opts.caps
if caps == nil then
  caps = { "\238\130\182", "\238\130\180" }  -- U+E0B6, U+E0B4
elseif caps == false then
  caps = { " ", " " }
end
local L, R = caps[1], caps[2]

---@type mc.Theme
local theme = {
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
  bgstatus     = c.bg,
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
  title_active     = { fg = c.blue },
  nick_self        = { fg = c.cyan, bold = true },
  badge            = { fg = c.ink, bg = c.yellow, bold = true },
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
  input_prompt     = { fg = c.blue, bold = true },
  dim              = { fg = c.fg_dim },
  accent           = { fg = c.blue },
  encrypted        = { fg = c.green },

  format = {
    time = "%H:%M",
    message = {
      ["in"] = "{#timestamp}{time}{#} "
               .. "{?private}{#accent}⇢{#} {/}"
               .. "{?me}{#nick}* {nick}{#} {/}"
               .. "{?muc}{!me}{#nick}{nick}{#}{#dim}:{#} {/}{/}"
               .. "{!muc}{!me}{?encrypted}{#encrypted}›{#}{/}"
               .. "{!encrypted}{#dim}›{#}{/} {/}{/}",
      out    = "{#timestamp}{time}{#} "
               .. "{?me}{#nick_self}* {nick}{#} {/}"
               .. "{!me}{?encrypted}{#encrypted}‹{#}{/}"
               .. "{!encrypted}{#nick_self}‹{#}{/} {/}"
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
                  .. "{?icon}{#status_$status}{icon}{#} {/}{#title}{name:*}{#}"
                  .. "{?resource}{#dim}/{resource}{#}{/}"
                  .. "{?msg} {#dim}— {msg:*}{#}{/} "
                  .. "{>}{?chatstate} {#dim}{chatstate}{#} {/}"
                  .. "{?encryption}{#encrypted}{encryption}{#} {/}",
    input_prompt = "{#input_prompt}{?command}:{/}{!command}❯{/}{#} ",
    main_status = " {#isl_cap_$status}"
                  .. L .. "{#}{#isl_$status}{icon} {status}{#}"
                  .. "{#isl_cap_$status}" .. R .. "{#}"
                  .. "{?unread} {#isl_cap_unread}" .. L .. "{#}"
                  .. "{#isl_unread}{unread}{#}{#isl_cap_unread}" .. R .. "{#}{/}"
                  .. "{?msg} {#isl_cap}" .. L .. "{#}{#isl}{msg:*}{#}"
                  .. "{#isl_cap}" .. R .. "{#}{/}"
                  .. "{>}{?info}{#isl_cap}" .. L .. "{#}{#isl}{info}{#}"
                  .. "{#isl_cap}" .. R .. "{#} {/}"
                  .. "{#isl_cap}" .. L .. "{#}{#isl}{jid}{#}"
                  .. "{#isl_cap}" .. R .. "{#} ",
  },
}

-- Islands: the content has a background color, the ends have it as
-- foreground color on the background of the status line.
local function island(name, bg, fg)
  theme["isl" .. name] = { fg = fg or c.ink, bg = bg, bold = true }
  theme["isl_cap" .. name] = { fg = bg, bg = "bgstatus" }
end
island("", c.bg_sel, c.fg)
island("_unread", c.yellow)
island("_online", c.green)
island("_free", c.green)
island("_away", c.yellow)
island("_notavail", c.orange)
island("_dnd", c.red)
island("_invisible", c.fg_dim)
island("_offline", c.fg_dim)
island("_unknown", c.fg_dim)
theme.isl.bold = nil

return theme
end

local M = make()
-- require("mcabber.themes.modern").with { caps = { "left", "right" } }
M.with = function(opts) return make(opts) end
return M
