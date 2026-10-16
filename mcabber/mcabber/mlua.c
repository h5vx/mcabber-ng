/*
 * mlua.c       -- Embedded Lua interpreter
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
#include "settings.h"
#include "logprint.h"
#include "utils.h"

// Maximum nesting of Lua calls coming from C (a handler may send a
// message, which runs handlers again...)
#define MLUA_MAX_DEPTH  8

static lua_State *L;
static int depth;
static gboolean reporting;

lua_State *mlua_state(void)
{
  return L;
}

//  mlua_report_error(context, message)
// Print a (possibly multi-line) Lua error to the log window.
void mlua_report_error(const char *ctx, const char *msg)
{
  gchar **lines, **p;

  if (reporting)    // An error while printing an error...
    return;
  reporting = TRUE;
  if (!msg)
    msg = "(error object is not a string)";
  lines = g_strsplit(msg, "\n", -1);
  for (p = lines; *p; p++) {
    if (p == lines)
      scr_LogPrint(LPRINT_LOGNORM, "Lua error [%s]: %s", ctx ? ctx : "lua", *p);
    else if (**p)
      scr_LogPrint(LPRINT_LOGNORM, "  %s", *p);
  }
  g_strfreev(lines);
  reporting = FALSE;
}

static int traceback_handler(lua_State *L)
{
  if (!lua_isstring(L, 1))
    return 1;
  lua_getfield(L, LUA_GLOBALSINDEX, "debug");
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return 1;
  }
  lua_getfield(L, -1, "traceback");
  if (!lua_isfunction(L, -1)) {
    lua_pop(L, 2);
    return 1;
  }
  lua_pushvalue(L, 1);
  lua_pushinteger(L, 2);
  lua_call(L, 2, 1);
  return 1;
}

int mlua_pcall(lua_State *L, int nargs, int nresults, const char *ctx)
{
  int base, ret;

  if (depth >= MLUA_MAX_DEPTH) {
    lua_pop(L, nargs + 1);
    mlua_report_error(ctx, "Lua calls nested too deeply");
    return -1;
  }

  base = lua_gettop(L) - nargs;   // Function index
  lua_pushcfunction(L, traceback_handler);
  lua_insert(L, base);
  depth++;
  ret = lua_pcall(L, nargs, nresults, base);
  depth--;
  lua_remove(L, base);
  if (ret) {
    mlua_report_error(ctx, lua_tostring(L, -1));
    lua_pop(L, 1);
  }
  return ret;
}

int mlua_dofile(const char *path)
{
  if (!L)
    return -1;
  if (luaL_loadfile(L, path)) {
    mlua_report_error(path, lua_tostring(L, -1));
    lua_pop(L, 1);
    return -1;
  }
  return mlua_pcall(L, 0, 0, path);
}

int mlua_dostring(const char *code, const char *chunkname)
{
  if (!L)
    return -1;
  if (luaL_loadbuffer(L, code, strlen(code), chunkname)) {
    mlua_report_error(chunkname, lua_tostring(L, -1));
    lua_pop(L, 1);
    return -1;
  }
  return mlua_pcall(L, 0, 0, chunkname);
}

//  set_package_path()
// Prepend the user and system Lua directories to package.path.
static void set_package_path(void)
{
  const char *cfgdir = settings_get_mcabber_config_dir();
  GString *path = g_string_new(NULL);

  if (cfgdir)
    g_string_append_printf(path, "%s/lua/?.lua;%s/lua/?/init.lua;",
                           cfgdir, cfgdir);
#ifdef DATA_DIR
  g_string_append(path, DATA_DIR "/mcabber-ng/lua/?.lua;"
                        DATA_DIR "/mcabber-ng/lua/?/init.lua;");
#endif

  lua_getfield(L, LUA_GLOBALSINDEX, "package");
  lua_getfield(L, -1, "path");
  g_string_append(path, lua_tostring(L, -1));
  lua_pop(L, 1);
  lua_pushstring(L, path->str);
  lua_setfield(L, -2, "path");
  lua_pop(L, 1);
  g_string_free(path, TRUE);
}

void mlua_init(void)
{
  if (L)
    return;

  L = luaL_newstate();
  if (!L) {
    scr_LogPrint(LPRINT_LOGNORM, "Cannot create the Lua state!");
    return;
  }
  luaL_openlibs(L);

  // mc = {...}; package.loaded.mcabber = mc
  lua_newtable(L);
  mlua_api_register(L);
  lua_pushvalue(L, -1);
  lua_setfield(L, LUA_GLOBALSINDEX, "mc");
  lua_getfield(L, LUA_GLOBALSINDEX, "package");
  lua_getfield(L, -1, "loaded");
  lua_pushvalue(L, -3);
  lua_setfield(L, -2, "mcabber");
  lua_pop(L, 3);

  set_package_path();
}

void mlua_deinit(void)
{
  if (!L)
    return;
  lua_close(L);
  L = NULL;
}

//  mlua_cmd_lua(code)
// /lua command: run a chunk and print the returned values.
// "return " is tried first, so that "/lua 1+1" prints 2.
void mlua_cmd_lua(char *arg)
{
  gchar *code, *expr;
  int top, n, err;

  if (!L) {
    scr_LogPrint(LPRINT_NORMAL, "Lua is not initialized.");
    return;
  }
  if (!arg || !*arg) {
    scr_LogPrint(LPRINT_NORMAL, "Usage: /lua <code>");
    return;
  }

  code = to_utf8(arg);
  if (!code)
    return;
  top = lua_gettop(L);
  expr = g_strdup_printf("return %s", code);
  err = luaL_loadbuffer(L, expr, strlen(expr), "=lua");
  g_free(expr);
  if (err) {
    lua_pop(L, 1);
    err = luaL_loadbuffer(L, code, strlen(code), "=lua");
  }
  g_free(code);
  if (err) {
    mlua_report_error("lua", lua_tostring(L, -1));
    lua_settop(L, top);
    return;
  }
  if (mlua_pcall(L, 0, LUA_MULTRET, "lua")) {
    lua_settop(L, top);
    return;
  }

  n = lua_gettop(L) - top;
  if (n > 0) {
    // __tostring metamethods may fail, so convert in protected mode
    lua_pushcfunction(L, mlua_tostring_all);
    lua_insert(L, top + 1);
    if (!mlua_pcall(L, n, 1, "lua"))
      scr_LogPrint(LPRINT_NORMAL, "%s", lua_tostring(L, -1));
  }
  lua_settop(L, top);
}

//  mlua_tostring_all(...)
// Lua C function: tostring() all arguments and join them with tabs.
int mlua_tostring_all(lua_State *L)
{
  int i, n = lua_gettop(L);
  luaL_Buffer b;

  luaL_buffinit(L, &b);
  for (i = 1; i <= n; i++) {
    lua_getfield(L, LUA_GLOBALSINDEX, "tostring");
    lua_pushvalue(L, i);
    lua_call(L, 1, 1);
    if (!lua_isstring(L, -1))
      return luaL_error(L, "'tostring' must return a string");
    if (i > 1)
      luaL_addchar(&b, '\t');
    luaL_addvalue(&b);
  }
  luaL_pushresult(&b);
  return 1;
}

/* vim: set et cindent cinoptions=>2\:2(0 ts=2 sw=2:  For Vim users... */
