-- Theme "mikael-256" (from the original mcabber contrib/themes/mikael-256.txt).
-- * NOTE: Your terminal MUST have 256 color support
-- Usage: mc.theme(require "mcabber.themes.mikael-256")
return {
  name = "mikael-256",
  options = {
    color_background   = "black",
    color_general      = "white",
    color_info         = "magenta",
    color_msgin        = 85,
    color_msgout       = 69,
    color_msghl        = "yellow",
    color_bgstatus     = "blue",
    color_status       = 82,
    color_log          = 244,
    color_roster       = 185,
    color_bgrostersel  = "cyan",
    color_rostersel    = "blue",
    color_rosterselmsg = "red",
    color_rosternewmsg = "red",
    color_readmark     = "red",
    nick_colors        = "green yellow white",
  },
  setup = function()
    mc.color.roster_clear()
    mc.color.roster("f", "*", "yellow")
    mc.color.roster("o", "*", "green")
    mc.color.roster("d", "*", "166")
    mc.color.roster("a", "*", "140")
    mc.color.roster("n", "*", "144")
    mc.color.roster("_", "*", "18")
    mc.color.muc("*", "on")
  end,
}
