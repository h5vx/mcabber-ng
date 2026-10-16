/*
 * msgformat.c  -- Format of the messages in the chat windows
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

/* The "head" of a message is the text displayed before it: time, direction
 * mark, nickname...  It is built from a template (see fmt.h) or by a Lua
 * function (mc.format.message).
 */

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "msgformat.h"
#include "hbuf.h"
#include "settings.h"
#include "logprint.h"
#include "mlua.h"
#include "mlua_compat.h"

#define FORMAT_KEY "mcabber.format"   // mc.format values (see the prelude)

typedef enum {
  KIND_IN,
  KIND_OUT,
  KIND_INFO,
  KIND_ERROR,
  KIND_SPECIAL,
  KIND_OTHER,
  KIND_COUNT
} msgkind_t;

static const char *kind_names[KIND_COUNT] = {
  "in", "out", "info", "error", "special", "other"
};

// Default templates: they reproduce the original mcabber format
#define DEFAULT_MSG_TEMPLATE \
  "{?delayed}{#timestamp}{/}{time}{#} {mark} " \
  "{?private}PRIV#<{nick}> {/}" \
  "{?me}{#nick}*{nick}{#} {/}" \
  "{?muc}{!me}{#nick}<{nick}>{#} {/}{/}"

static const char *default_templates[KIND_COUNT] = {
  DEFAULT_MSG_TEMPLATE,     // in
  DEFAULT_MSG_TEMPLATE,     // out
  "{time} {mark} ",         // info
  "{time} {mark} ",         // error
  "{time}   ",              // special (status window)
  "{time}     ",            // other
};

typedef struct {
  time_t timestamp;
  guint flags;
  const char *nick;
  msgkind_t kind;
} msginfo_t;

static GHashTable *templates;   // Template string -> fmt_t (NULL if invalid)

static msgkind_t message_kind(guint flags)
{
  if (flags & HBB_PREFIX_INFO)    return KIND_INFO;
  if (flags & HBB_PREFIX_ERR)     return KIND_ERROR;
  if (flags & HBB_PREFIX_IN)      return KIND_IN;
  if (flags & HBB_PREFIX_OUT)     return KIND_OUT;
  if (flags & HBB_PREFIX_SPECIAL) return KIND_SPECIAL;
  return KIND_OTHER;
}

//  compiled_template(tmpl)
// Compile a template (compiled templates are cached).  Errors are reported
// once.
static const fmt_t *compiled_template(const char *tmpl)
{
  fmt_t *f;
  GError *err = NULL;

  if (!templates)
    templates = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
                                      (GDestroyNotify)fmt_free);
  if (g_hash_table_lookup_extended(templates, tmpl, NULL, (gpointer *)&f))
    return f;
  f = fmt_compile(tmpl, &err);
  if (!f) {
    scr_LogPrint(LPRINT_LOGNORM, "Invalid format \"%s\": %s", tmpl,
                 err->message);
    g_error_free(err);
  }
  g_hash_table_insert(templates, g_strdup(tmpl), f);
  return f;
}

/* -- Access to mc.format -- */

//  push_format(name)
// Push mc.format[name] (or nil).
static lua_State *push_format(const char *name)
{
  lua_State *L = mlua_state();
  if (!L)
    return NULL;
  lua_getfield(L, LUA_REGISTRYINDEX, FORMAT_KEY);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    lua_pushnil(L);
    return L;
  }
  lua_getfield(L, -1, name);
  lua_remove(L, -2);
  return L;
}

//  format_string(name)
// mc.format[name] if it is a string (newly allocated), or NULL.
static gchar *format_string(const char *name)
{
  lua_State *L = push_format(name);
  gchar *s = NULL;
  if (!L)
    return NULL;
  if (lua_type(L, -1) == LUA_TSTRING)
    s = g_strdup(lua_tostring(L, -1));
  lua_pop(L, 1);
  return s;
}

/* -- Fields -- */

static gchar *default_time_format(const msginfo_t *m)
{
  static const char *normal[] = { "%m-%d %H:%M", "%H:%M", "" };
  static const char *special[] = { "%m-%d %H:%M:%S", "%H:%M:%S", "" };
  gchar *custom = format_string("time");
  guint n;

  if (custom)
    return custom;
  n = settings_opt_get_int("time_prefix");
  if (n > 2)
    n = 0;
  return g_strdup(m->kind == KIND_SPECIAL ? special[n] : normal[n]);
}

static gchar *format_time(const msginfo_t *m, const char *arg)
{
  gchar *fmtstr = arg ? g_strdup(arg) : default_time_format(m);
  char buf[128];
  time_t t = m->timestamp ? m->timestamp : time(NULL);
  size_t len = 0;

  if (*fmtstr)
    len = strftime(buf, sizeof(buf), fmtstr, localtime(&t));
  buf[len] = '\0';
  g_free(fmtstr);
  if (!m->timestamp)  // No time: keep the alignment
    return g_strnfill(fmt_text_width(buf, -1), ' ');
  return g_strdup(buf);
}

//  direction mark of the original mcabber ("<==", "-->", "***"...)
static gchar *format_mark(guint flags)
{
  char dir;

  if (flags & (HBB_PREFIX_INFO | HBB_PREFIX_ERR)) {
    char c = (flags & HBB_PREFIX_INFO) ? '*' : '#';
    dir = c;
    if (flags & HBB_PREFIX_IN)
      dir = '<';
    else if (flags & HBB_PREFIX_OUT)
      dir = '>';
    return g_strdup_printf("%c%c%c", c, dir, c);
  }
  if (flags & HBB_PREFIX_IN) {
    char crypt = '=';
    if (flags & HBB_PREFIX_PGPCRYPT)
      crypt = '~';
    else if (flags & HBB_PREFIX_OTRCRYPT)
      crypt = 'O';
    return g_strdup_printf("<%c=", crypt);
  }
  if (flags & HBB_PREFIX_OUT) {
    char crypt = '-';
    if (flags & HBB_PREFIX_PGPCRYPT)
      crypt = '~';
    else if (flags & HBB_PREFIX_OTRCRYPT)
      crypt = 'O';
    return g_strdup_printf("%c%c>", (flags & HBB_PREFIX_RECEIPT) ? 'r' : '-',
                           crypt);
  }
  return g_strdup("");
}

static const struct {
  const char *name;
  guint flag;
} flag_fields[] = {
  { "me",        HBB_PREFIX_ME },
  { "muc",       HBB_PREFIX_MUC },
  { "private",   HBB_PREFIX_PRIVATE },
  { "delayed",   HBB_PREFIX_DELAYED },
  { "receipt",   HBB_PREFIX_RECEIPT },    // Waiting for a receipt
  { "carbon",    HBB_PREFIX_CARBON },
  { "highlight", HBB_PREFIX_HLIGHT },
  { "own",       HBB_PREFIX_HLIGHT_OUT },
  { "pgp",       HBB_PREFIX_PGPCRYPT },
  { "otr",       HBB_PREFIX_OTRCRYPT },
  { NULL, 0 }
};

static gchar *get_field(const char *name, const char *arg, gpointer data)
{
  const msginfo_t *m = data;
  int i;

  if (!strcmp(name, "time"))
    return format_time(m, arg);
  if (!strcmp(name, "mark"))
    return format_mark(m->flags);
  if (!strcmp(name, "nick"))
    return g_strdup(m->nick);
  if (!strcmp(name, "kind"))
    return g_strdup(kind_names[m->kind]);
  if (!strcmp(name, "encrypted")) {
    if (m->flags & HBB_PREFIX_PGPCRYPT)
      return g_strdup("pgp");
    if (m->flags & HBB_PREFIX_OTRCRYPT)
      return g_strdup("otr");
    return NULL;
  }
  for (i = 0; i < KIND_COUNT; i++)
    if (!strcmp(name, kind_names[i]))
      return m->kind == (msgkind_t)i ? g_strdup("1") : NULL;
  for (i = 0; flag_fields[i].name; i++)
    if (!strcmp(name, flag_fields[i].name))
      return (m->flags & flag_fields[i].flag) ? g_strdup("1") : NULL;
  return NULL;
}

/* -- Lua formatter -- */

//  push_message_table(L, m)
// Table given to a Lua formatter.
static void push_message_table(lua_State *L, const msginfo_t *m)
{
  gchar *v;
  int i;

  lua_newtable(L);
  lua_pushstring(L, kind_names[m->kind]);
  lua_setfield(L, -2, "kind");
  lua_pushnumber(L, m->timestamp ? m->timestamp : time(NULL));
  lua_setfield(L, -2, "time");
  if (m->nick) {
    lua_pushstring(L, m->nick);
    lua_setfield(L, -2, "nick");
  }
  v = format_mark(m->flags);
  lua_pushstring(L, v);
  lua_setfield(L, -2, "mark");
  g_free(v);
  v = get_field("encrypted", NULL, (gpointer)m);
  if (v) {
    lua_pushstring(L, v);
    lua_setfield(L, -2, "encrypted");
    g_free(v);
  }
  for (i = 0; flag_fields[i].name; i++) {
    lua_pushboolean(L, (m->flags & flag_fields[i].flag) != 0);
    lua_setfield(L, -2, flag_fields[i].name);
  }
}

//  append_lua_result(L, idx, out)
// A formatter returns a string, or a list of segments: "text" or
// { "text", "style" }.
static gboolean append_lua_result(lua_State *L, int idx, sstr_t *out)
{
  int i, n;

  if (lua_type(L, idx) == LUA_TSTRING) {
    sstr_append(out, lua_tostring(L, idx), -1, NULL);
    return TRUE;
  }
  if (!lua_istable(L, idx))
    return FALSE;
  n = mlua_rawlen(L, idx);
  for (i = 1; i <= n; i++) {
    lua_rawgeti(L, idx, i);
    if (lua_type(L, -1) == LUA_TSTRING) {
      sstr_append(out, lua_tostring(L, -1), -1, NULL);
    } else if (lua_istable(L, -1)) {
      const char *text, *style;
      lua_rawgeti(L, -1, 1);
      lua_rawgeti(L, -2, 2);
      text = lua_tostring(L, -2);
      style = lua_tostring(L, -1);
      if (text)
        sstr_append(out, text, -1, style);
      lua_pop(L, 2);
    }
    lua_pop(L, 1);
  }
  return TRUE;
}

//  lua_head(m, out)
// Use mc.format.message.  Returns FALSE if the default template must be
// used.
static gboolean lua_head(const msginfo_t *m, sstr_t *out)
{
  lua_State *L = push_format("message");
  gboolean done = FALSE;
  int top;

  if (!L)
    return FALSE;
  top = lua_gettop(L);

  // A table of templates (or functions) per kind of message
  if (lua_istable(L, -1)) {
    lua_getfield(L, -1, kind_names[m->kind]);
    if (lua_isnil(L, -1)) {
      lua_pop(L, 1);
      lua_getfield(L, -1, "default");
    }
    lua_remove(L, -2);
  }

  if (lua_type(L, -1) == LUA_TSTRING) {
    const fmt_t *f = compiled_template(lua_tostring(L, -1));
    if (f) {
      fmt_render(f, get_field, (gpointer)m, 0, out);
      done = TRUE;
    }
  } else if (lua_isfunction(L, -1)) {
    push_message_table(L, m);
    if (!mlua_pcall(L, 1, 1, "mc.format.message"))
      done = append_lua_result(L, -1, out);
  }
  lua_settop(L, top - 1);
  return done;
}

/* -- Public functions -- */

void msgfmt_head(time_t timestamp, guint flags, const char *nick,
                 sstr_t *out)
{
  msginfo_t m = { timestamp, flags, nick, message_kind(flags) };

  if (!lua_head(&m, out)) {
    const fmt_t *f = compiled_template(default_templates[m.kind]);
    if (f)
      fmt_render(f, get_field, &m, 0, out);
  }
}

gchar *msgfmt_head_text(time_t timestamp, guint flags, const char *nick)
{
  sstr_t *s = sstr_new();
  gchar *text;

  msgfmt_head(timestamp, flags, nick, s);
  text = g_strdup(s->text->str);
  sstr_free(s);
  return text;
}

//  head_size(timestamp, flags, nick, headw, contw)
// hbuf_head_fn: the following lines are aligned with the text of the first
// line, unless mc.format.indent is a number.
static void head_size(time_t timestamp, guint flags, const char *nick,
                      guint *headw, guint *contw)
{
  sstr_t *s = sstr_new();
  lua_State *L;

  msgfmt_head(timestamp, flags, nick, s);
  *headw = *contw = s->width;
  sstr_free(s);

  L = push_format("indent");
  if (L) {
    if (lua_type(L, -1) == LUA_TNUMBER)
      *contw = MAX(0, (int)lua_tonumber(L, -1));
    lua_pop(L, 1);
  }
}

void msgfmt_init(void)
{
  hbuf_set_head_func(head_size);
}

/* vim: set et cindent cinoptions=>2\:2(0 ts=2 sw=2:  For Vim users... */
