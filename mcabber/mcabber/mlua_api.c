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

#include "mlua.h"
#include "mlua_compat.h"
#include "logprint.h"

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

static const luaL_Reg mc_functions[] = {
  { "print", l_print },
  { "log",   l_log   },
  { "debug", l_debug },
  { NULL,    NULL    }
};

void mlua_api_register(lua_State *L)
{
  mlua_setfuncs(L, mc_functions);

  lua_pushstring(L, MCABBER_VERSION);
  lua_setfield(L, -2, "version");
  lua_pushstring(L, MCABBER_LUA_IMPL);
  lua_setfield(L, -2, "lua_impl");
}

/* vim: set et cindent cinoptions=>2\:2(0 ts=2 sw=2:  For Vim users... */
