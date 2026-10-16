-- mlua_prelude.lua -- The parts of the "mc" API written in Lua.
-- This file is compiled into mcabber-ng; it receives the "mc" table.
local mc = ...

-- Return values of event handlers
mc.DROP = "drop"
mc.STOP = "stop"

mc.cmd = mc.command

-- mc.opt.name reads an option, mc.opt.name = value sets it
mc.opt = setmetatable({}, {
  __index = function(_, k) return mc.get(k) end,
  __newindex = function(_, k, v) mc.set(k, v) end,
})

-- Quote a command argument
local function quote(s)
  s = tostring(s)
  if s == "" or s:find('[%s"\\]') then
    return '"' .. s:gsub('[\\"]', '\\%0') .. '"'
  end
  return s
end
mc.quote = quote

mc.color = {}

-- mc.color.roster("ofd", "*@example.org", "brightgreen")
function mc.color.roster(statuses, jid, color)
  return mc.command(("color roster %s %s %s"):format(statuses, quote(jid), color))
end

function mc.color.roster_clear()
  return mc.command("color roster clear")
end

-- mode: "on", "preset", "off" or "-"
function mc.color.muc(room, mode)
  return mc.command(("color muc %s %s"):format(quote(room), mode or "on"))
end

function mc.color.mucnick(nick, color)
  return mc.command(("color mucnick %s %s"):format(quote(nick), color))
end

mc.module = {}

function mc.module.load(name)
  return mc.command("module load " .. name)
end

function mc.module.unload(name)
  return mc.command("module unload " .. name)
end

-- mc.status("away", "Lunch")
function mc.status(status, message)
  if message then
    return mc.command(("status %s %s"):format(status, message))
  end
  return mc.command("status " .. status)
end
