-- Theme "colorcli" (from the original mcabber contrib/themes/colorcli.txt).
-- Custom Color Settings - Colorcli
-- https://github.com/jonasjacek/colorcli
-- * NOTE: Your terminal MUST have 256 color support
-- Usage: mc.theme(require "mcabber.themes.colorcli")
return {
  name = "colorcli",
  options = {
    color_background   = "default",
    color_general      = 24,
    color_info         = 59,
    color_msgin        = "black",
    color_msgout       = 59,
    color_msghl        = 208,
    color_bgstatus     = 24,
    color_status       = 15,
    color_log          = 24,
    color_roster       = 59,
    color_bgrostersel  = 254,
    color_rostersel    = 31,
    color_rosterselmsg = 226,
    color_rosternewmsg = 226,
    color_readmark     = 160,
  },
  setup = function()
    mc.color.roster_clear()
  end,
}
