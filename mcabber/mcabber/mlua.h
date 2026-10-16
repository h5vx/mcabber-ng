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

// TRUE while the main configuration file is being loaded
extern gboolean mlua_startup;

//  mlua_load_config(filename)
// Load the main configuration file (default: <config dir>/init.lua).
// Returns 0 on success, -1 if there is no configuration file and -2 if
// the configuration cannot be loaded (the error is printed on stderr).
int       mlua_load_config(const char *filename);
const char *mlua_config_file(void);

// Key bindings to Lua functions
gboolean  mlua_call_binding(const char *keyname);
gboolean  mlua_has_binding(const char *keyname);
void      mlua_unbind(const char *keyname);
void      mlua_foreach_binding(void (*cb)(const char *key, void *data),
                               void *data);

/* -- Events (mlua_events.c) -- */

typedef struct mlua_event mlua_event_t;

typedef enum {
  MLUA_CONTINUE = 0,
  MLUA_STOP,        // A handler stopped the event
  MLUA_DROP         // A handler asked to drop the event
} mlua_evres_t;

//  mlua_event_new(event)
// Prepare an event for the Lua handlers.  Returns NULL if there is no
// handler for this event (all the mlua_event_* functions accept NULL).
mlua_event_t *mlua_event_new(const char *event);
void mlua_event_set_str(mlua_event_t *ev, const char *key, const char *value);
void mlua_event_set_int(mlua_event_t *ev, const char *key, gint64 value);
void mlua_event_set_bool(mlua_event_t *ev, const char *key, gboolean value);
mlua_evres_t mlua_event_emit(mlua_event_t *ev);
// Read a field after the handlers have run (newly allocated string, or NULL
// if the field is not a string)
gchar *mlua_event_get_str(mlua_event_t *ev, const char *key);
void mlua_event_free(mlua_event_t *ev);
// Emit an event without fields
mlua_evres_t mlua_emit_simple(const char *event);

// Name of a status ("online", "away"...), for enum imstatus values
const char *mlua_status_name(int status);

// Command handler for /lua
void      mlua_cmd_lua(char *arg);

// Lua C function: tostring() all arguments and join them with tabs
int       mlua_tostring_all(lua_State *L);

// Registration of the "mc" API (mlua_api.c).
// Expects the "mc" table at the top of the stack.
void      mlua_api_register(lua_State *L);
void      mlua_api_deinit(lua_State *L);

#endif /* __MCABBER_MLUA_H__ */

/* vim: set et cindent cinoptions=>2\:2(0 ts=2 sw=2:  For Vim users... */
