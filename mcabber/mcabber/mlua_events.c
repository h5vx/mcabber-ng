/*
 * mlua_events.c -- Events for the Lua handlers (mc.on)
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

// The handlers table and the dispatcher are created by the prelude
#define HANDLERS_KEY  "mcabber.handlers"
#define EMIT_KEY      "mcabber.emit"

struct mlua_event {
  gchar *name;
  int ref;      // The event table
};

//  has_handlers(L, event)
static gboolean has_handlers(lua_State *L, const char *event)
{
  gboolean found = FALSE;

  lua_getfield(L, LUA_REGISTRYINDEX, HANDLERS_KEY);
  if (lua_istable(L, -1)) {
    lua_getfield(L, -1, event);
    found = lua_istable(L, -1) && mlua_rawlen(L, -1) > 0;
    lua_pop(L, 1);
  }
  lua_pop(L, 1);
  return found;
}

mlua_event_t *mlua_event_new(const char *event)
{
  lua_State *L = mlua_state();
  mlua_event_t *ev;

  if (!L || !has_handlers(L, event))
    return NULL;
  ev = g_new0(mlua_event_t, 1);
  ev->name = g_strdup(event);
  lua_newtable(L);
  ev->ref = luaL_ref(L, LUA_REGISTRYINDEX);
  return ev;
}

// Push the event table and the key; the caller pushes the value and
// calls set_field().
static lua_State *prepare_field(mlua_event_t *ev, const char *key)
{
  lua_State *L = mlua_state();
  if (!ev || !L)
    return NULL;
  lua_rawgeti(L, LUA_REGISTRYINDEX, ev->ref);
  lua_pushstring(L, key);
  return L;
}

static void set_field(lua_State *L)
{
  lua_settable(L, -3);
  lua_pop(L, 1);
}

void mlua_event_set_str(mlua_event_t *ev, const char *key, const char *value)
{
  lua_State *L = prepare_field(ev, key);
  if (!L)
    return;
  if (value)
    lua_pushstring(L, value);
  else
    lua_pushnil(L);
  set_field(L);
}

void mlua_event_set_int(mlua_event_t *ev, const char *key, gint64 value)
{
  lua_State *L = prepare_field(ev, key);
  if (!L)
    return;
  lua_pushnumber(L, (lua_Number)value);
  set_field(L);
}

void mlua_event_set_bool(mlua_event_t *ev, const char *key, gboolean value)
{
  lua_State *L = prepare_field(ev, key);
  if (!L)
    return;
  lua_pushboolean(L, value);
  set_field(L);
}

mlua_evres_t mlua_event_emit(mlua_event_t *ev)
{
  lua_State *L = mlua_state();
  mlua_evres_t res = MLUA_CONTINUE;
  gchar *ctx;

  if (!ev || !L)
    return MLUA_CONTINUE;

  lua_getfield(L, LUA_REGISTRYINDEX, EMIT_KEY);
  if (!lua_isfunction(L, -1)) {
    lua_pop(L, 1);
    return MLUA_CONTINUE;
  }
  lua_pushstring(L, ev->name);
  lua_rawgeti(L, LUA_REGISTRYINDEX, ev->ref);
  ctx = g_strdup_printf("event %s", ev->name);
  if (!mlua_pcall(L, 2, 1, ctx)) {
    const char *r = lua_tostring(L, -1);
    if (r && !strcmp(r, "drop"))
      res = MLUA_DROP;
    else if (r && !strcmp(r, "stop"))
      res = MLUA_STOP;
    lua_pop(L, 1);
  }
  g_free(ctx);
  return res;
}

gchar *mlua_event_get_str(mlua_event_t *ev, const char *key)
{
  lua_State *L = mlua_state();
  gchar *value = NULL;

  if (!ev || !L)
    return NULL;
  lua_rawgeti(L, LUA_REGISTRYINDEX, ev->ref);
  lua_getfield(L, -1, key);
  if (lua_type(L, -1) == LUA_TSTRING)
    value = g_strdup(lua_tostring(L, -1));
  lua_pop(L, 2);
  return value;
}

void mlua_event_free(mlua_event_t *ev)
{
  lua_State *L = mlua_state();

  if (!ev)
    return;
  if (L)
    luaL_unref(L, LUA_REGISTRYINDEX, ev->ref);
  g_free(ev->name);
  g_free(ev);
}

mlua_evres_t mlua_emit_simple(const char *event)
{
  mlua_event_t *ev = mlua_event_new(event);
  mlua_evres_t res = mlua_event_emit(ev);
  mlua_event_free(ev);
  return res;
}

/* vim: set et cindent cinoptions=>2\:2(0 ts=2 sw=2:  For Vim users... */
