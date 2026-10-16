#ifndef __MCABBER_MLUA_H__
#define __MCABBER_MLUA_H__ 1

#include <glib.h>

#include <mcabber/config.h>

#include <lua.h>

void      mlua_init(void);
void      mlua_deinit(void);
lua_State *mlua_state(void);

//  mlua_pcall(L, nargs, nresults, context)
// Like lua_pcall(), with a traceback handler.  Errors are reported to the
// log window (or stdout before ncurses is initialized) and popped.
// Returns 0 on success.
int       mlua_pcall(lua_State *L, int nargs, int nresults, const char *ctx);

int       mlua_dofile(const char *path);
int       mlua_dostring(const char *code, const char *chunkname);

void      mlua_report_error(const char *ctx, const char *msg);

// Command handler for /lua
void      mlua_cmd_lua(char *arg);

// Lua C function: tostring() all arguments and join them with tabs
int       mlua_tostring_all(lua_State *L);

// Registration of the "mc" API (mlua_api.c).
// Expects the "mc" table at the top of the stack.
void      mlua_api_register(lua_State *L);

#endif /* __MCABBER_MLUA_H__ */

/* vim: set et cindent cinoptions=>2\:2(0 ts=2 sw=2:  For Vim users... */
