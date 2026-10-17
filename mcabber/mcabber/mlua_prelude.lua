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

-- Quote a string for the shell (os.execute, io.popen): never pass message
-- texts or nicknames to the shell without it.
function mc.shell_quote(s)
  return "'" .. tostring(s):gsub("'", "'\\''") .. "'"
end

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

-- Event handlers -----------------------------------------------------------

local known_events = {
  message_in = true, message_received = true, message_send = true,
  message_out = true, status = true, my_status = true, connect = true,
  disconnect = true, unread = true, subscription = true, receipt = true,
  ready = true, quit = true,
}

local handlers = {}   -- event name -> list of handlers, sorted by priority
local by_id = {}
local next_id = 0
local MAX_ERRORS = 5

-- mc.on(event, function(ev) ... end [, { priority = n }]) -> id
-- Handlers with a lower priority run first.  A handler can return mc.DROP
-- (or false) to drop the event (when possible), or mc.STOP to stop
-- running the other handlers.
function mc.on(event, fn, opts)
  if type(event) ~= "string" then
    error("bad argument #1 to 'on' (event name expected)", 2)
  end
  if type(fn) ~= "function" then
    error("bad argument #2 to 'on' (function expected)", 2)
  end
  local hook = event:sub(1, 5) == "hook-"
  if not known_events[event] and not hook then
    error("unknown event \"" .. event .. "\"", 2)
  end
  local prio = opts and opts.priority or 0
  next_id = next_id + 1
  local h = { id = next_id, fn = fn, priority = prio, errors = 0,
              event = event }
  local list = handlers[event]
  if not list then
    list = {}
    handlers[event] = list
  end
  local pos = #list + 1
  for i, o in ipairs(list) do
    if o.priority > prio then
      pos = i
      break
    end
  end
  table.insert(list, pos, h)
  by_id[h.id] = h
  if hook then
    mc._hook(event)   -- Run the handlers when a module runs this hook
  end
  return h.id
end

-- mc.off(id): remove a handler
function mc.off(id)
  local h = by_id[id]
  if not h then
    return false
  end
  by_id[id] = nil
  h.removed = true
  local list = handlers[h.event]
  for i, o in ipairs(list) do
    if o == h then
      table.remove(list, i)
      break
    end
  end
  return true
end

local traceback = debug.traceback

-- Called by mcabber-ng to run the handlers of an event
local function emit(event, ev)
  local list = handlers[event]
  if not list or #list == 0 then
    return nil
  end
  ev.event = event
  local copy = { unpack(list) }   -- Handlers may call mc.on/mc.off
  for _, h in ipairs(copy) do
    if not h.removed then
      local ok, ret = xpcall(function() return h.fn(ev) end, traceback)
      if ok then
        h.errors = 0
        if ret == mc.DROP or ret == false then
          return "drop"
        elseif ret == mc.STOP then
          return "stop"
        end
      else
        h.errors = h.errors + 1
        mc._report("event " .. event, ret)
        if h.errors >= MAX_ERRORS then
          mc._report("event " .. event, "handler disabled after " ..
                     MAX_ERRORS .. " consecutive errors")
          mc.off(h.id)
        end
      end
    end
  end
  return nil
end

-- Styles ------------------------------------------------------------------

-- A style is a string ("fg=#c0caf5 bg=default bold", "white on blue") or a
-- table ({ fg = "#c0caf5", bold = true }).
local attributes = { "bold", "dim", "italic", "underline", "reverse",
                     "blink", "standout" }

local function style_spec(v)
  if type(v) ~= "table" then
    return v
  end
  local parts = {}
  if v.fg then parts[#parts + 1] = "fg=" .. v.fg end
  if v.bg then parts[#parts + 1] = "bg=" .. v.bg end
  for _, a in ipairs(attributes) do
    if v[a] then parts[#parts + 1] = a end
  end
  return table.concat(parts, " ")
end

-- mc.style(name, style): set a style (nil or false restores the default)
function mc.style(name, spec)
  if spec == false then
    spec = nil
  end
  return mc._style(name, style_spec(spec))
end

-- Formats -----------------------------------------------------------------

-- mc.format.message: template, table of templates per kind of message
--   ("in", "out", "info", "error", "special", "default") or function
-- mc.format.time: default time format (strftime)
-- mc.format.indent: indentation of the following lines of a message
--   (default: aligned with the first line)
-- Assigning a field redraws the buffers; after modifying a table in place,
-- call mc.format.refresh().
local format_values = {}
mc.format = setmetatable({}, {
  __index = function(_, k)
    if k == "refresh" then
      return mc._format_changed
    end
    return format_values[k]
  end,
  __newindex = function(_, k, v)
    format_values[k] = v
    mc._format_changed()
  end,
})

-- Themes ------------------------------------------------------------------

-- mc.theme(t) applies a theme: a table of styles, with optional sections:
--   options = { ... }   (see mc.set), format = { ... } (see mc.format)
function mc.theme(t)
  for k, v in pairs(t) do
    if k == "options" then
      mc.set(v)
    elseif k == "format" then
      for fk, fv in pairs(v) do
        mc.format[fk] = fv
      end
    elseif k == "setup" or type(v) == "function" then
      -- setup is called after the styles have been set; other functions
      -- (like "with" in the provided themes) are not styles
    elseif k == "name" then
      -- Informative only
    else
      mc.style(k, v)
    end
  end
  if type(t.setup) == "function" then
    t.setup()
  end
end

return handlers, emit, format_values
