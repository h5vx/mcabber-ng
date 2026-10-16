/*
 * mlua_api.c   -- The "mc" Lua API
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <string.h>

#include "mlua.h"
#include "mlua_compat.h"
#include "logprint.h"
#include "settings.h"
#include "commands.h"
#include "compl.h"
#include "screen.h"
#include "keys.h"
#include "utils.h"

/* -- Printing -- */

//  print_with(L, flags)
// Join the arguments like print() does and send them to the log window.
static int print_with(lua_State *L, unsigned int flags)
{
  lua_pushcfunction(L, mlua_tostring_all);
  lua_insert(L, 1);
  lua_call(L, lua_gettop(L) - 1, 1);
  scr_LogPrint(flags, "%s", lua_tostring(L, -1));
  return 0;
}

// mc.print(...)
static int l_print(lua_State *L)
{
  return print_with(L, LPRINT_NORMAL);
}

// mc.log(...): log window and trace log file
static int l_log(lua_State *L)
{
  return print_with(L, LPRINT_LOGNORM);
}

// mc.debug(...)
static int l_debug(lua_State *L)
{
  return print_with(L, LPRINT_DEBUG);
}

/* -- Options -- */

//  opt_value(L, idx)
// Convert a Lua value to an option value (NULL to unset).
static const char *opt_value(lua_State *L, int idx)
{
  switch (lua_type(L, idx)) {
    case LUA_TNIL:
        return NULL;
    case LUA_TBOOLEAN:
        return lua_toboolean(L, idx) ? "1" : "0";
    case LUA_TNUMBER:
    {
      // Integers are written without decimals
      lua_Number n = lua_tonumber(L, idx);
      if (n == (lua_Number)(long long)n)
        lua_pushfstring(L, "%d", (int)n);
      else
        lua_pushvalue(L, idx);
      lua_replace(L, idx);
      return lua_tostring(L, idx);
    }
    case LUA_TSTRING:
        return lua_tostring(L, idx);
    default:
        luaL_error(L, "invalid value for an option (%s)",
                   luaL_typename(L, idx));
        return NULL;
  }
}

static void set_option(lua_State *L, int kidx, int vidx)
{
  const char *key, *value;

  kidx = mlua_absindex(L, kidx);
  vidx = mlua_absindex(L, vidx);
  if (lua_type(L, kidx) != LUA_TSTRING)
    luaL_error(L, "option names must be strings");
  key = lua_tostring(L, kidx);
  value = opt_value(L, vidx);
  if (value)
    settings_set(SETTINGS_TYPE_OPTION, key, value);
  else
    settings_del(SETTINGS_TYPE_OPTION, key);
}

// mc.set(name, value) or mc.set{ name = value, ... }
static int l_set(lua_State *L)
{
  if (lua_istable(L, 1)) {
    lua_settop(L, 1);
    lua_pushnil(L);
    while (lua_next(L, 1)) {
      // Stack: tbl key value; set_option() may replace the value
      lua_pushvalue(L, -2);
      lua_insert(L, -2);
      set_option(L, -2, -1);
      lua_pop(L, 2);
    }
    return 0;
  }
  luaL_checkstring(L, 1);
  lua_settop(L, 2);
  set_option(L, 1, 2);
  return 0;
}

// mc.get(name) -> string or nil
static int l_get(lua_State *L)
{
  const char *v = settings_opt_get(luaL_checkstring(L, 1));
  if (v)
    lua_pushstring(L, v);
  else
    lua_pushnil(L);
  return 1;
}

/* -- Commands and aliases -- */

// mc.command("command line") -> boolean
// The leading "/" is optional.
static int l_command(lua_State *L)
{
  const char *line = luaL_checkstring(L, 1);
  gchar *cmdline, *locale;

  while (*line == ' ')
    line++;
  if (*line == '/' || *line == ':')
    line++;
  if (!*line)
    return luaL_error(L, "empty command");

  if (mlua_startup) {
    // Only some commands can be used while the configuration file is read
    // (the UI is not initialized yet, and we're not connected).
    const char *end = strchr(line, ' ');
    gchar *name = end ? g_strndup(line, end - line) : g_strdup(line);
    gboolean safe = cmd_is_safe(name);
    if (!safe) {
      scr_log_print(LPRINT_LOGNORM, "Configuration: command \"%s\" cannot "
                    "be used during startup (use mc.on(\"connect\", ...))",
                    name);
    }
    g_free(name);
    if (!safe) {
      lua_pushboolean(L, FALSE);
      return 1;
    }
  }

  cmdline = g_strdup_printf("%c%s", COMMAND_CHAR, line);
  locale = from_utf8(cmdline);
  g_free(cmdline);
  if (locale) {
    process_command(locale, TRUE);
    g_free(locale);
  }
  lua_pushboolean(L, TRUE);
  return 1;
}

static void set_alias(lua_State *L, int kidx, int vidx)
{
  const char *name, *value;

  kidx = mlua_absindex(L, kidx);
  vidx = mlua_absindex(L, vidx);
  if (lua_type(L, kidx) != LUA_TSTRING)
    luaL_error(L, "alias names must be strings");
  name = lua_tostring(L, kidx);
  if (!*name || strchr(name, ' '))
    luaL_error(L, "invalid alias name \"%s\"", name);
  if (cmd_get(name))
    luaL_error(L, "\"%s\" is a command and cannot be an alias", name);

  if (lua_isnil(L, vidx) || (lua_isboolean(L, vidx) && !lua_toboolean(L, vidx))) {
    if (settings_get(SETTINGS_TYPE_ALIAS, name)) {
      settings_del(SETTINGS_TYPE_ALIAS, name);
      compl_del_category_word(COMPL_CMD, name);
    }
    return;
  }
  value = lua_tostring(L, vidx);
  if (!value)
    luaL_error(L, "the value of alias \"%s\" must be a string", name);
  while (*value == '/' || *value == ':')
    value++;
  if (!settings_get(SETTINGS_TYPE_ALIAS, name))
    compl_add_category_word(COMPL_CMD, name);
  settings_set(SETTINGS_TYPE_ALIAS, name, value);
}

// mc.alias(name, command) or mc.alias{ name = command, ... }
static int l_alias(lua_State *L)
{
  if (lua_istable(L, 1)) {
    lua_settop(L, 1);
    lua_pushnil(L);
    while (lua_next(L, 1)) {
      set_alias(L, -2, -1);
      lua_pop(L, 1);
    }
    return 0;
  }
  luaL_checkstring(L, 1);
  lua_settop(L, 2);
  set_alias(L, 1, 2);
  return 0;
}

/* -- Key bindings -- */

//  check_key(L, idx)
// Return the canonical name of the key at idx (to be freed).
static gchar *check_key(lua_State *L, int idx)
{
  const char *name;
  GError *err = NULL;
  keyspec_t k;

  if (lua_type(L, idx) != LUA_TSTRING)
    luaL_error(L, "key names must be strings");
  name = lua_tostring(L, idx);

  if (!key_parse(name, &k, &err)) {
    if (!key_from_legacy(name, &k)) {
      // Like luaL_error(), with the position of the caller
      luaL_where(L, 1);
      lua_pushstring(L, err->message);
      g_error_free(err);
      lua_concat(L, 2);
      lua_error(L);
    }
    g_error_free(err);
  }
  return key_format(&k);
}

static void set_binding(lua_State *L, int kidx, int vidx)
{
  gchar *key;
  int t;

  kidx = mlua_absindex(L, kidx);
  vidx = mlua_absindex(L, vidx);
  key = check_key(L, kidx);
  t = lua_type(L, vidx);

  if (t == LUA_TFUNCTION) {
    settings_del(SETTINGS_TYPE_BINDING, key);
    lua_getfield(L, LUA_REGISTRYINDEX, "mcabber.bindings");
    lua_pushvalue(L, vidx);
    lua_setfield(L, -2, key);
    lua_pop(L, 1);
  } else if (t == LUA_TSTRING) {
    const char *cmd = lua_tostring(L, vidx);
    while (*cmd == '/' || *cmd == ':')
      cmd++;
    scr_bind_key(key, cmd, NULL);
  } else if (t == LUA_TNIL || (t == LUA_TBOOLEAN && !lua_toboolean(L, vidx))) {
    scr_bind_key(key, NULL, NULL);
  } else {
    g_free(key);
    luaL_error(L, "a key must be bound to a command string or a function");
  }
  g_free(key);
}

// mc.bind(key, command|function) or mc.bind{ [key] = command|function }
static int l_bind(lua_State *L)
{
  if (lua_istable(L, 1)) {
    lua_settop(L, 1);
    lua_pushnil(L);
    while (lua_next(L, 1)) {
      set_binding(L, -2, -1);
      lua_pop(L, 1);
    }
    return 0;
  }
  lua_settop(L, 2);
  set_binding(L, 1, 2);
  return 0;
}

// mc.unbind(key)
static int l_unbind(lua_State *L)
{
  gchar *key = check_key(L, 1);
  scr_bind_key(key, NULL, NULL);
  g_free(key);
  return 0;
}

static void add_binding_cb(char *k, char *v, void *param)
{
  lua_State *L = param;
  lua_pushstring(L, v);
  lua_setfield(L, -2, k);
}

// mc.bindings() -> { [key] = command|function }
static int l_bindings(lua_State *L)
{
  lua_newtable(L);
  settings_foreach(SETTINGS_TYPE_BINDING, add_binding_cb, L);
  lua_getfield(L, LUA_REGISTRYINDEX, "mcabber.bindings");
  lua_pushnil(L);
  while (lua_next(L, -2)) {
    lua_pushvalue(L, -2);
    lua_insert(L, -2);
    lua_settable(L, -5);
  }
  lua_pop(L, 1);
  return 1;
}

// mc.key_name(name) -> canonical name, or nil and an error message
static int l_key_name(lua_State *L)
{
  GError *err = NULL;
  gchar *canon = key_canonicalize(luaL_checkstring(L, 1), &err);
  if (!canon) {
    lua_pushnil(L);
    lua_pushstring(L, err->message);
    g_error_free(err);
    return 2;
  }
  lua_pushstring(L, canon);
  g_free(canon);
  return 1;
}

/* -- Timers -- */

typedef struct {
  lua_State *L;
  int ref;          // The function
  gboolean repeat;
  guint id;
} mlua_timer_t;

static GHashTable *timers;   // id -> mlua_timer_t

static void timer_destroy(gpointer data)
{
  mlua_timer_t *t = data;
  if (mlua_state())
    luaL_unref(t->L, LUA_REGISTRYINDEX, t->ref);
  g_free(t);
}

static gboolean timer_cb(gpointer data)
{
  mlua_timer_t *t = data;
  lua_State *L = t->L;
  gpointer id = GUINT_TO_POINTER(t->id);
  gboolean again = t->repeat;

  lua_rawgeti(L, LUA_REGISTRYINDEX, t->ref);
  if (!mlua_pcall(L, 0, 1, "timer")) {
    // Returning false stops a repeating timer
    if (lua_isboolean(L, -1) && !lua_toboolean(L, -1))
      again = FALSE;
    lua_pop(L, 1);
  }
  // The timer may have been cancelled by the function itself
  if (g_hash_table_lookup(timers, id) != t)
    return FALSE;
  if (!again)
    g_hash_table_remove(timers, id);
  return again;
}

// mc.timer(milliseconds, function [, repeat]) -> id
static int l_timer(lua_State *L)
{
  lua_Number ms = luaL_checknumber(L, 1);
  mlua_timer_t *t;

  luaL_checktype(L, 2, LUA_TFUNCTION);
  if (ms < 0)
    ms = 0;
  t = g_new0(mlua_timer_t, 1);
  t->L = L;
  t->repeat = lua_toboolean(L, 3);
  lua_pushvalue(L, 2);
  t->ref = luaL_ref(L, LUA_REGISTRYINDEX);
  t->id = g_timeout_add_full(G_PRIORITY_DEFAULT, (guint)ms, timer_cb, t,
                             NULL);
  g_hash_table_insert(timers, GUINT_TO_POINTER(t->id), t);
  lua_pushinteger(L, t->id);
  return 1;
}

// mc.cancel(id)
static int l_cancel(lua_State *L)
{
  guint id = luaL_checkinteger(L, 1);
  if (g_hash_table_lookup(timers, GUINT_TO_POINTER(id))) {
    g_source_remove(id);
    g_hash_table_remove(timers, GUINT_TO_POINTER(id));
    lua_pushboolean(L, TRUE);
  } else {
    lua_pushboolean(L, FALSE);
  }
  return 1;
}

static const luaL_Reg mc_functions[] = {
  { "print",    l_print    },
  { "log",      l_log      },
  { "debug",    l_debug    },
  { "set",      l_set      },
  { "get",      l_get      },
  { "command",  l_command  },
  { "alias",    l_alias    },
  { "bind",     l_bind     },
  { "unbind",   l_unbind   },
  { "bindings", l_bindings },
  { "key_name", l_key_name },
  { "timer",    l_timer    },
  { "cancel",   l_cancel   },
  { NULL,       NULL       }
};

void mlua_api_register(lua_State *L)
{
  if (!timers)
    timers = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL,
                                   timer_destroy);

  mlua_setfuncs(L, mc_functions);

  lua_pushstring(L, MCABBER_VERSION);
  lua_setfield(L, -2, "version");
  lua_pushstring(L, MCABBER_LUA_IMPL);
  lua_setfield(L, -2, "lua_impl");
}

void mlua_api_deinit(lua_State *L)
{
  GHashTableIter iter;
  gpointer id;

  if (!timers)
    return;
  g_hash_table_iter_init(&iter, timers);
  while (g_hash_table_iter_next(&iter, &id, NULL))
    g_source_remove(GPOINTER_TO_UINT(id));
  g_hash_table_destroy(timers);
  timers = NULL;
}

/* vim: set et cindent cinoptions=>2\:2(0 ts=2 sw=2:  For Vim users... */
