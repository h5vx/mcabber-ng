-- The original mcabber look: no frames, input line at the bottom of the
-- screen, original formats and colors.
-- Usage: mc.theme(require "mcabber.themes.classic")
---@type table<string, any>
local theme = {
  name = "classic",
  options = { ui_borders = "none", input_position = "bottom" },
  format = {
    message = false, roster = false, chat_status = false,
    main_status = false, icons = false, input_prompt = false,
    members = false,
  },
}

-- Remove the styles set by another theme
for _, name in ipairs {
  "background", "bgstatus", "bgrostersel", "general", "msgin", "msgout",
  "msghl", "info", "timestamp", "readmark", "log", "status", "roster",
  "rostersel", "rosterselmsg", "rosternewmsg", "border", "title",
  "title_active", "nick_self", "badge", "roster_group", "roster_unread",
  "input", "input_prompt", "dim", "accent",
} do
  theme[name] = false
end

return theme
