#ifndef __MCABBER_MLUA_COMPAT_H__
#define __MCABBER_MLUA_COMPAT_H__ 1

/* We embed LuaJIT or PUC Lua 5.1: only the 5.1 C API is available. */

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#if LUA_VERSION_NUM != 501
# error "mcabber-ng requires the Lua 5.1 API (LuaJIT or Lua 5.1)"
#endif

#define mlua_rawlen(L, idx)   lua_objlen((L), (idx))

// Convert a relative stack index to an absolute one
#define mlua_absindex(L, idx) \
  (((idx) < 0 && (idx) > LUA_REGISTRYINDEX) ? lua_gettop(L) + (idx) + 1 : (idx))

//  mlua_setfuncs(L, fns)
// Register the functions into the table at the top of the stack.
static inline void mlua_setfuncs(lua_State *L, const luaL_Reg *fns)
{
  for ( ; fns->name; fns++) {
    lua_pushcfunction(L, fns->func);
    lua_setfield(L, -2, fns->name);
  }
}

//  mlua_newsubtable(L, name)
// Create t[name] = {} for the table t at the top of the stack and
// leave the new table on the stack.
static inline void mlua_newsubtable(lua_State *L, const char *name)
{
  lua_newtable(L);
  lua_pushvalue(L, -1);
  lua_setfield(L, -3, name);
}

#endif /* __MCABBER_MLUA_COMPAT_H__ */

/* vim: set et cindent cinoptions=>2\:2(0 ts=2 sw=2:  For Vim users... */
