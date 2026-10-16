-- The original mcabber look.
-- Usage: mc.theme(require "mcabber.themes.classic")
return {
  name = "classic",
  options = { ui_borders = "none" },
  format = {
    message = false, roster = false, chat_status = false,
    main_status = false, icons = false, input_prompt = false,
  },
}
