/*
 * screen.c     -- UI stuff
 *
 * Copyright (C) 2005-2014 Mikael Berthe <mikael@lilotux.net>
 * Parts of this file come from the Cabber project <cabber@ajmacias.com>
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <unistd.h>

#include <config.h>
#include <locale.h>
#include <assert.h>
#ifdef USE_SIGWINCH
# include <sys/ioctl.h>
# include <termios.h>
# include <unistd.h>
#endif

#ifdef HAVE_LOCALCHARSET_H
# include <localcharset.h>
#else
# include <langinfo.h>
#endif

#ifdef WITH_ENCHANT
# include <enchant.h>
#else
# ifdef WITH_ASPELL
#  include <aspell.h>
# endif
#endif

#include "screen.h"
#include "utf8.h"
#include "hbuf.h"
#include "commands.h"
#include "compl.h"
#include "roster.h"
#include "histolog.h"
#include "settings.h"
#include "mlua.h"
#ifdef HAVE_OMEMO
# include "omemo.h"
#endif
#include "layout.h"
#include "inputlayout.h"
#include "paste.h"
#include "msgformat.h"
#include "style.h"
#include "xmlsplit.h"
#include "utils.h"
#include "xmpp.h"
#include "main.h"

int COLOR_ATTRIB[COLOR_max];

#define get_color(col)      (COLOR_PAIR(col)|COLOR_ATTRIB[col])

#define DEFAULT_LOG_WIN_HEIGHT (5)
#define DEFAULT_ROSTER_WIDTH    24
#define CHAT_WIN_HEIGHT (Layout.chat.h)

#define DEFAULT_ATTENTION_CHAR '!'

const char *LocaleCharSet = "C";

static unsigned short int Log_Win_Height;
static int Input_Win_Height = 1;
static ui_layout_t Layout;
static unsigned short int Roster_Width;
static gboolean colors_stalled = FALSE;

// Default attention sign trigger levels
static guint ui_attn_sign_prio_level_muc = ROSTER_UI_PRIO_MUC_HL_MESSAGE;
static guint ui_attn_sign_prio_level     = ROSTER_UI_PRIO_ATTENTION_MESSAGE;

static void scr_cancel_current_completion(void);
static void scr_end_current_completion(void);
static void scr_insert_text(const char*);
static guint input_text_width(void);
static void scr_handle_tab(gboolean fwd);

static void scr_glog_print(const gchar *log_domain, GLogLevelFlags log_level,
                           const gchar *message, gpointer user_data);

#ifdef XEP0085
static gboolean scr_chatstates_timeout(gpointer data);
#endif

#if defined(WITH_ENCHANT) || defined(WITH_ASPELL)
static void spellcheck(char *, char *);
#endif

static void open_chat_window(void);
static void clear_inputline(void);

static GHashTable *winbufhash;

typedef struct {
  GList    *hbuf;
  GList    *top;      // If top is NULL, we'll display the last lines
  char      cleared;  // For ex, user has issued a /clear command...
  char      lock;
  char      refcount; // refcount > 0 if there are other users of this struct
                      // e.g. with symlinked history
  int       width;    // Width used to wrap the lines of hbuf
} buffdata_t;

typedef struct {
  WINDOW *win;
  PANEL  *panel;
  buffdata_t *bd;
  gchar  *jid;        // NULL for the special buffer
} winbuf_t;

static rect_t chat_rect(gboolean room);
static void draw_members(void);
static void place_chatstatus(void);

static void winbuf_free(gpointer data)
{
  winbuf_t *w = data;
  g_free(w->jid);
  g_free(w);
}

struct dimensions {
  int l;
  int c;
};

static WINDOW *rosterWnd, *chatWnd, *activechatWnd, *inputWnd, *logWnd;
static WINDOW *mainstatusWnd, *chatstatusWnd;
static PANEL *rosterPanel, *chatPanel, *activechatPanel, *inputPanel;
static PANEL *mainstatusPanel, *chatstatusPanel;
static PANEL *logPanel;
static WINDOW *frameWnd;      // Frames (ui_borders), behind the other windows
static PANEL *framePanel;
static WINDOW *membersWnd;    // Members of the current room
static PANEL *membersPanel;
static gboolean members_hidden;   // "/members hide"
static gboolean members_shown;    // The members panel is displayed
static winbuf_t *consoleWindow;   // XMPP console
static gboolean console_shown;
static GList *console_buddy;      // Current buddy when it was displayed
static void console_show(void);
static int maxY, maxX;
static int prev_chatwidth;
static winbuf_t *statusWindow;
static winbuf_t *currentWindow;
static GList  *statushbuf;

static int roster_hidden;
static int chatmode;
static int multimode;
static char *multiline, *multimode_subj;

static bool Curses;
static bool log_win_on_top;
static bool roster_win_on_right;
static guint autoaway_source = 0;

static char       inputLine[INPUTLINE_LENGTH+1];
#if defined(WITH_ENCHANT) || defined(WITH_ASPELL)
static char       maskLine[INPUTLINE_LENGTH+1];
#endif
static char      *ptr_inputline;
static guint input_scroll;   // First visible row of the input line
static il_layout_t InputLayout;
static int    completion_started;
static GList *cmdhisto;
static GList *cmdhisto_cur;
static guint  cmdhisto_nblines;
static char   cmdhisto_backup[INPUTLINE_LENGTH+1];

static int    chatstate; /* (0=active, 1=composing, 2=paused) */
static bool   lock_chatstate;
static time_t chatstate_timestamp;
static guint  chatstate_timeout_id = 0;

int _update_roster;
int utf8_mode;
gboolean chatstates_disabled;
gboolean Autoaway;

gboolean vi_mode;

#define MAX_KEYSEQ_LENGTH 16

// Escape sequences that ncurses doesn't know (without the leading Esc)
typedef struct {
  char *seqstr;
  keyspec_t spec;
} keyseq_t;

GSList *keyseqlist;
static void add_keyseq(char *seqstr, guint mods, gint key);

static void scr_write_in_window(const char *winId, const char *text,
                                const char *nick, time_t timestamp,
                                unsigned int prefix_flags, int force_show,
                                gpointer xep184);

static void scr_write_message(const char *bjid, const char *text,
                              const char *nick, time_t timestamp,
                              guint prefix_flags, gpointer xep184);

void scr_update_buddy_window(void);
void scr_set_chatmode(int enable);

#define SPELLBADCHAR 5

#if defined(WITH_ENCHANT) || defined(WITH_ASPELL)
typedef struct {
#ifdef WITH_ENCHANT
  EnchantBroker *broker;
  EnchantDict *checker;
#endif
#ifdef WITH_ASPELL
  AspellConfig *config;
  AspellSpeller *checker;
#endif
} spell_checker_t;

GSList* spell_checkers = NULL;
#endif

// User colors (roster rules, nick colors): a style, or an internal color
typedef struct {
  int color_pair;       // Internal color (COLOR_*), if !has_spec
  int color_attrib;
  gboolean has_spec;
  style_spec_t spec;
  guint gen;            // Color generation of "cached"
  int cached;
} ccolor_t;

static int compose_color(ccolor_t *col);

typedef struct {
  char *status, *wildcard;
  ccolor_t *color;
  GPatternSpec *compiled;
} rostercolor_t;

static GSList *rostercolrules = NULL;

static GHashTable *muccolors = NULL, *nickcolors = NULL;

typedef struct {
  bool manual; // Manually set?
  ccolor_t *color;
} nickcolor_t;

static int nickcolcount = 0;
static ccolor_t ** nickcols = NULL;
static muccol_t glob_muccol = MC_OFF;

/* Functions */

/* -- Colors and styles -- */

#ifdef HAVE_TIGETFLAG
// From <term.h>, which can't be included here (it defines "lines"...)
extern int tigetflag(const char *capname);
#endif

static gboolean colors_started;  // start_color() has been called
static int color_depth = 8;     // 8, 16, 256 or 0x1000000 (direct color)
static GHashTable *pair_cache;  // "fg:bg" -> color pair
static int next_pair;
static guint color_gen = 1;     // Incremented when colors are reset
static GHashTable *theme;       // Style name -> specification (mc.style)
static GHashTable *style_cache; // Style name -> attributes (int)
static int general_fg = COLOR_WHITE, general_bg = COLOR_BLACK;

static void detect_color_depth(void)
{
  const char *opt = settings_opt_get("ui_colors");

  color_depth = COLORS;
#ifdef HAVE_TIGETFLAG
  if (COLORS > 256 && tigetflag("RGB") > 0)
    color_depth = 0x1000000;
#endif
  if (COLORS >= 0x1000000)
    color_depth = 0x1000000;
  if (opt && *opt && g_ascii_strcasecmp(opt, "auto")) {
    if (!g_ascii_strcasecmp(opt, "truecolor"))
      color_depth = 0x1000000;
    else if (atoi(opt) > 0)
      color_depth = MIN(atoi(opt), color_depth);
  }
  if (color_depth > 256 && color_depth < 0x1000000)
    color_depth = 256;
}

//  resolve_color(color, &attrs)
// Terminal color number for a style color, -1 for the default color and
// -2 if the color is not set.  Colors that the terminal doesn't have are
// approximated (A_BOLD is added to attrs for bright colors on 8-color
// terminals).
static int resolve_color(const style_color_t *c, int *attrs)
{
  guint32 rgb;

  switch (c->kind) {
    case STYLE_COLOR_UNSET:
        return -2;
    case STYLE_COLOR_DEFAULT:
        return -1;
    case STYLE_COLOR_INDEX:
        if (c->value < 8 ||
            (color_depth <= 256 && c->value < (guint)color_depth))
          return c->value;
        if (c->value < 16 && color_depth < 16) {
          if (attrs)
            *attrs |= A_BOLD;
          return c->value - 8;
        }
        rgb = style_palette_rgb(c->value);
        break;
    default:
        rgb = c->value;
  }
  if (color_depth >= 0x1000000)
    return rgb < 8 ? 0 : (int)rgb;  // Values < 8 are palette colors
  if (color_depth >= 256)
    return style_rgb_to_256(rgb);
  {
    int n = style_rgb_to_16(rgb);
    if (n >= 8 && color_depth < 16) {
      if (attrs)
        *attrs |= A_BOLD;
      n -= 8;
    }
    return n;
  }
}

static void reset_pairs(void)
{
  if (pair_cache)
    g_hash_table_remove_all(pair_cache);
  else
    pair_cache = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
  if (style_cache)
    g_hash_table_remove_all(style_cache);
  else
    style_cache = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
  next_pair = COLOR_max;
  color_gen++;
}

static void set_pair(int pair, int fg, int bg)
{
#ifdef HAVE_INIT_EXTENDED_PAIR
  init_extended_pair(pair, fg, bg);
#else
  init_pair(pair, fg, bg);
#endif
}

//  ui_pair(fg, bg)
// Get a color pair (allocated when needed).
static int ui_pair(int fg, int bg)
{
  static gboolean warned;
  int max_pair = MIN(COLOR_PAIRS - 1, 255);
  gchar *key;
  gpointer p;

  if (!colors_started)
    return 0;
  if (!pair_cache)
    reset_pairs();
  key = g_strdup_printf("%d:%d", fg, bg);
  if (g_hash_table_lookup_extended(pair_cache, key, NULL, &p)) {
    g_free(key);
    return GPOINTER_TO_INT(p);
  }
  if (next_pair > max_pair) {
    if (!warned)
      scr_LogPrint(LPRINT_LOGNORM, "Too many colors are used.");
    warned = TRUE;
    g_free(key);
    return 0;
  }
  set_pair(next_pair, fg, bg);
  g_hash_table_insert(pair_cache, key, GINT_TO_POINTER(next_pair));
  return next_pair++;
}

static int style_attrs(guint a)
{
  int attrs = 0;
  if (a & STYLE_BOLD)      attrs |= A_BOLD;
  if (a & STYLE_DIM)       attrs |= A_DIM;
#ifdef A_ITALIC
  if (a & STYLE_ITALIC)    attrs |= A_ITALIC;
#endif
  if (a & STYLE_UNDERLINE) attrs |= A_UNDERLINE;
  if (a & STYLE_REVERSE)   attrs |= A_REVERSE;
  if (a & STYLE_BLINK)     attrs |= A_BLINK;
  if (a & STYLE_STANDOUT)  attrs |= A_STANDOUT;
  return attrs;
}

//  spec_attr(spec, def_fg, def_bg)
// Attributes (color pair included) for a style specification.
static int spec_attr(const style_spec_t *spec, int def_fg, int def_bg)
{
  int attrs = style_attrs(spec->attrs);
  int fg = resolve_color(&spec->fg, &attrs);
  int bg = resolve_color(&spec->bg, NULL);
  if (fg == -2) fg = def_fg;
  if (bg == -2) bg = def_bg;
  return COLOR_PAIR(ui_pair(fg, bg)) | attrs;
}

//  legacy_color(name, &attrs)
// Colors of the color_* options: "bright" means bold.
static int legacy_color(const char *name, int *attrs)
{
  style_color_t c;

  if (!strncmp(name, "bright", 6)) {
    if (attrs)
      *attrs |= A_BOLD;
    name += 6;
  }
  if (!style_parse_color(name, &c, NULL)) {
    scr_LogPrint(LPRINT_LOGNORM, "ERROR: Wrong color: %s", name);
    return -1;
  }
  return resolve_color(&c, NULL);
}

static ccolor_t *get_user_color(const char *color)
{
  ccolor_t *ccol;
  style_spec_t spec;
  GError *err = NULL;

  if (!style_parse(color, &spec, &err)) {
    scr_LogPrint(LPRINT_LOGNORM, "ERROR: Wrong color: %s (%s)", color,
                 err->message);
    g_error_free(err);
    return NULL;
  }
  ccol = g_new0(ccolor_t, 1);
  ccol->has_spec = TRUE;
  ccol->spec = spec;
  return ccol;
}

static int compose_color(ccolor_t *col)
{
  if (!col->has_spec)
    return COLOR_PAIR(col->color_pair) | col->color_attrib;
  if (col->gen != color_gen) {
    col->cached = spec_attr(&col->spec, general_fg, general_bg);
    col->gen = color_gen;
  }
  return col->cached;
}

//  scr_set_style(name, spec)
// Set a style of the theme (mc.style).  Returns FALSE (and sets err) if
// the specification is invalid.
gboolean scr_set_style(const char *name, const char *spec, GError **err)
{
  if (!theme)
    theme = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
  if (!spec) {
    g_hash_table_remove(theme, name);
  } else {
    style_spec_t st;
    if (!style_parse(spec, &st, err))
      return FALSE;
    g_hash_table_insert(theme, g_strdup(name), g_strdup(spec));
  }
  colors_stalled = TRUE;
  return TRUE;
}

static const char *theme_spec(const char *name)
{
  return theme ? g_hash_table_lookup(theme, name) : NULL;
}

// Default specifications of the styles that are not internal colors
static const struct {
  const char *name, *spec;
} default_styles[] = {
  { "border",           "dim" },
  { "title",            "bold" },
  { "title_active",     "bold" },
  { "nick_self",        "cyan" },
  { "badge",            "reverse" },
  { "roster_group",     "bold" },
  { "roster_unread",    "red" },
  { "status_online",    "green" },
  { "status_free",      "brightgreen" },
  { "status_away",      "yellow" },
  { "status_notavail",  "magenta" },
  { "status_dnd",       "red" },
  { "status_invisible", "dim" },
  { "status_offline",   "dim" },
  { "status_unknown",   "dim" },
  { "status_room",      "magenta" },
  { "status_room_out",  "dim" },
  { "dim",              "dim" },
  { "accent",           "blue" },
  { "input",            "" },
  { "input_prompt",     "bold" },
  { NULL, NULL }
};

//  custom_style_attr(name, &attr)
// Attributes of a style that is not an internal color.
static gboolean custom_style_attr(const char *name, int *attr)
{
  const char *spec = theme_spec(name);
  gpointer cached;
  style_spec_t st;
  int i;

  if (!style_cache)
    reset_pairs();
  if (g_hash_table_lookup_extended(style_cache, name, NULL, &cached)) {
    *attr = GPOINTER_TO_INT(cached);
    return TRUE;
  }
  if (!spec) {
    for (i = 0; default_styles[i].name; i++)
      if (!strcmp(default_styles[i].name, name)) {
        spec = default_styles[i].spec;
        break;
      }
  }
  if (!spec || !style_parse(spec, &st, NULL))
    return FALSE;
  *attr = spec_attr(&st, general_fg, general_bg);
  g_hash_table_insert(style_cache, g_strdup(name), GINT_TO_POINTER(*attr));
  return TRUE;
}

static void ensure_string_htable(GHashTable **table,
                                 GDestroyNotify value_destroy_func)
{
  if (*table) // Have it already
    return;
  *table = g_hash_table_new_full(g_str_hash, g_str_equal,
      g_free, value_destroy_func);
}

// Sets the coloring mode for given MUC
// The MUC room does not need to be in the roster at that time
// muc - the JID of room
// type - the new type
void scr_muc_color(const char *muc, muccol_t type)
{
  gchar *muclow = g_utf8_strdown(muc, -1);
  if (type == MC_REMOVE) { // Remove it
    if (strcmp(muc, "*")) {
      if (muccolors && g_hash_table_lookup(muccolors, muclow))
        g_hash_table_remove(muccolors, muclow);
    } else {
      scr_LogPrint(LPRINT_NORMAL, "Can not remove global coloring mode");
    }
    g_free(muclow);
  } else { // Add or overwrite
    if (strcmp(muc, "*")) {
      muccol_t *value = g_new(muccol_t, 1);
      *value = type;
      ensure_string_htable(&muccolors, g_free);
      g_hash_table_replace(muccolors, muclow, value);
    } else {
      glob_muccol = type;
      g_free(muclow);
    }
  }
  // Need to redraw?
  if (chatmode &&
      ((buddy_search_jid(muc) == current_buddy) || !strcmp(muc, "*")))
    scr_update_buddy_window();
}

// Sets the color for nick in MUC
// If color is "-", the color is marked as automaticly assigned and is
// not used if the room is in the "preset" mode
void scr_muc_nick_color(const char *nick, const char *color)
{
  char *snick, *mnick;
  bool need_update = FALSE;
  snick = g_strdup_printf("<%s>", nick);
  mnick = g_strdup_printf("*%s ", nick);
  if (!strcmp(color, "-")) { // Remove the color
    if (nickcolors) {
      nickcolor_t *nc = g_hash_table_lookup(nickcolors, snick);
      if (nc) { // Have this nick already
        nc->manual = FALSE;
        nc = g_hash_table_lookup(nickcolors, mnick);
        assert(nc); // Must have both at the same time
        nc->manual = FALSE;
      }// Else -> no color saved, nothing to delete
    }
    g_free(snick); // They are not saved in the hash
    g_free(mnick);
    need_update = TRUE;
  } else {
    ccolor_t *cl = get_user_color(color);
    if (!cl) {
      scr_LogPrint(LPRINT_NORMAL, "No such color name");
      g_free(snick);
      g_free(mnick);
    } else {
      nickcolor_t *nc = g_new(nickcolor_t, 1);
      ensure_string_htable(&nickcolors, NULL);
      nc->manual = TRUE;
      nc->color = cl;
      // Free the struct, if any there already
      g_free(g_hash_table_lookup(nickcolors, mnick));
      // Save the new ones
      g_hash_table_replace(nickcolors, mnick, nc);
      g_hash_table_replace(nickcolors, snick, nc);
      need_update = TRUE;
    }
  }
  if (need_update && chatmode &&
      (buddy_gettype(BUDDATA(current_buddy)) & ROSTER_TYPE_ROOM))
    scr_update_buddy_window();
}

static void free_rostercolrule(rostercolor_t *col)
{
  g_free(col->status);
  g_free(col->wildcard);
  g_free(col->color);
  g_pattern_spec_free(col->compiled);
  g_free(col);
}

// Removes all roster coloring rules
void scr_roster_clear_color(void)
{
  GSList *head;
  for (head = rostercolrules; head; head = g_slist_next(head)) {
    free_rostercolrule(head->data);
  }
  g_slist_free(rostercolrules);
  rostercolrules = NULL;
  scr_update_roster();
}

// Adds, modifies or removes roster coloring rule
// color set to "-" removes the rule,
// otherwise it is modified (if exists) or added
//
// Returns weather it was successfull (therefore the roster should be
// redrawed) or not. If it failed, for example because of invalid color
// name, it also prints the error.
bool scr_roster_color(const char *status, const char *wildcard,
                      const char *color)
{
  GSList *head;
  GSList *found = NULL;
  for (head = rostercolrules; head; head = g_slist_next(head)) {
    rostercolor_t *rc = head->data;
    if ((!strcmp(status, rc->status)) && (!strcmp(wildcard, rc->wildcard))) {
      found = head;
      break;
    }
  }
  if (!strcmp(color,"-")) { // Delete the rule
    if (found) {
      free_rostercolrule(found->data);
      rostercolrules = g_slist_delete_link(rostercolrules, found);
      scr_update_roster();
      return TRUE;
    } else {
      scr_LogPrint(LPRINT_NORMAL, "No such color rule, nothing removed");
      return FALSE;
    }
  } else {
    ccolor_t *cl = get_user_color(color);
    if (!cl) {
      scr_LogPrint(LPRINT_NORMAL, "No such color name");
      return FALSE;
    }
    if (found) {
      rostercolor_t *rc = found->data;
      g_free(rc->color);
      rc->color = cl;
    } else {
      rostercolor_t *rc = g_new(rostercolor_t, 1);
      rc->status = g_strdup(status);
      rc->wildcard = g_strdup(wildcard);
      rc->compiled = g_pattern_spec_new(wildcard);
      rc->color = cl;
      rostercolrules = g_slist_prepend(rostercolrules, rc);
    }
    scr_update_roster();
    return TRUE;
  }
}

//  parse_colors()
// Initialize the internal colors from the theme (mc.style) and the color_*
// options.
static void parse_colors(void)
{
  static const struct {
    int color;
    const char *name;
    int def_fg;
    const char *bg_option;
  } colors[] = {
    { COLOR_GENERAL,       "general",      COLOR_WHITE,  "background" },
    { COLOR_MSGOUT,        "msgout",       COLOR_CYAN,   "background" },
    { COLOR_MSGHL,         "msghl",        COLOR_YELLOW, "background" },
    { COLOR_STATUS,        "status",       COLOR_WHITE,  "bgstatus" },
    { COLOR_LOG,           "log",          COLOR_WHITE,  "background" },
    { COLOR_ROSTER,        "roster",       COLOR_GREEN,  "background" },
    { COLOR_ROSTERSEL,     "rostersel",    COLOR_BLUE,   "bgrostersel" },
    { COLOR_ROSTERSELNMSG, "rosterselmsg", COLOR_RED,    "bgrostersel" },
    { COLOR_ROSTERNMSG,    "rosternewmsg", COLOR_RED,    "background" },
    { COLOR_INFO,          "info",         COLOR_WHITE,  "background" },
    { COLOR_MSGIN,         "msgin",        COLOR_WHITE,  "background" },
    { COLOR_READMARK,      "readmark",     COLOR_RED,    "background" },
    { COLOR_TIMESTAMP,     "timestamp",    COLOR_WHITE,  "background" },
  };
  static const char *bg_defaults[][2] = {
    { "background",  "black" },
    { "bgstatus",    "blue" },
    { "bgrostersel", "cyan" },
  };
  int bgs[3];
  guint i;

  if (!colors_started)
    return;

  detect_color_depth();
  reset_pairs();
  memset(COLOR_ATTRIB, 0, sizeof(COLOR_ATTRIB));

  // Background colors: theme ("background", "bgstatus", "bgrostersel"),
  // or color_* options
  for (i = 0; i < 3; i++) {
    const char *spec = theme_spec(bg_defaults[i][0]);
    gchar *opt = g_strdup_printf("color_%s", bg_defaults[i][0]);
    const char *v = settings_opt_get(opt);
    style_color_t c;
    g_free(opt);
    if (spec && style_parse_color(spec, &c, NULL))
      bgs[i] = resolve_color(&c, NULL);
    else
      bgs[i] = legacy_color(v ? v : bg_defaults[i][1], NULL);
  }

  init_pair(1, COLOR_BLACK, COLOR_WHITE);
  init_pair(2, COLOR_WHITE, COLOR_BLACK);

  for (i = 0; i < G_N_ELEMENTS(colors); i++) {
    const char *spec = theme_spec(colors[i].name);
    int bg = bgs[0], fg = colors[i].def_fg, attrs = 0;
    style_spec_t st;

    if (!strcmp(colors[i].bg_option, "bgstatus"))
      bg = bgs[1];
    else if (!strcmp(colors[i].bg_option, "bgrostersel"))
      bg = bgs[2];

    if (spec && style_parse(spec, &st, NULL)) {
      int c;
      attrs = style_attrs(st.attrs);
      c = resolve_color(&st.fg, &attrs);
      if (c != -2) fg = c;
      c = resolve_color(&st.bg, NULL);
      if (c != -2) bg = c;
    } else {
      gchar *opt = g_strdup_printf("color_%s", colors[i].name);
      const char *v = settings_opt_get(opt);
      g_free(opt);
      if (v)
        fg = legacy_color(v, &attrs);
    }
    set_pair(colors[i].color, fg, bg);
    COLOR_ATTRIB[colors[i].color] = attrs;
    if (colors[i].color == COLOR_GENERAL) {
      general_fg = fg;
      general_bg = bg;
    }
  }

  if (!nickcols) {
    char *ncolors = g_strdup(settings_opt_get("nick_colors"));
    if (ncolors) {
      char *ncolor_start, *ncolor_end;
      ncolor_start = ncolor_end = ncolors;

      while (*ncolor_end)
        ncolor_end++;

      while (ncolors < ncolor_end && *ncolors) {
        if ((*ncolors == ' ') || (*ncolors == '\t')) {
          ncolors++;
        } else {
          char *end = ncolors;
          ccolor_t *cl;
          while (*end && (*end != ' ') && (*end != '\t'))
            end++;
          *end = '\0';
          cl = get_user_color(ncolors);
          if (!cl) {
            scr_LogPrint(LPRINT_NORMAL, "Unknown color %s", ncolors);
          } else {
            nickcols = g_realloc(nickcols, (++nickcolcount) * sizeof *nickcols);
            nickcols[nickcolcount-1] = cl;
          }
          ncolors = end+1;
        }
      }
      g_free(ncolor_start);
    }
    if (!nickcols) { // Fallback to have something
      nickcolcount = 1;
      nickcols = g_new0(ccolor_t*, 1);
      *nickcols = g_new0(ccolor_t, 1);
      (*nickcols)->color_pair = COLOR_GENERAL;
      (*nickcols)->color_attrib = A_NORMAL;
    }
  }

  colors_stalled = FALSE;
}

static void init_keycodes(void)
{
  // Most sequences are decoded by ncurses (terminfo) or by the generic
  // CSI decoder in scr_getch().  These are the remaining ones.

  // rxvt
  add_keyseq("O5A", KMOD_CTRL, KN_UP);
  add_keyseq("O5B", KMOD_CTRL, KN_DOWN);
  add_keyseq("O5C", KMOD_CTRL, KN_RIGHT);
  add_keyseq("O5D", KMOD_CTRL, KN_LEFT);
  add_keyseq("O6A", KMOD_CTRL|KMOD_SHIFT, KN_UP);
  add_keyseq("O6B", KMOD_CTRL|KMOD_SHIFT, KN_DOWN);
  add_keyseq("O6C", KMOD_CTRL|KMOD_SHIFT, KN_RIGHT);
  add_keyseq("O6D", KMOD_CTRL|KMOD_SHIFT, KN_LEFT);
  add_keyseq("O2A", KMOD_SHIFT, KN_UP);
  add_keyseq("O2B", KMOD_SHIFT, KN_DOWN);
  add_keyseq("O2C", KMOD_SHIFT, KN_RIGHT);
  add_keyseq("O2D", KMOD_SHIFT, KN_LEFT);
  add_keyseq("[5^", KMOD_CTRL, KN_PGUP);
  add_keyseq("[6^", KMOD_CTRL, KN_PGDN);
  add_keyseq("[5@", KMOD_CTRL|KMOD_SHIFT, KN_PGUP);
  add_keyseq("[6@", KMOD_CTRL|KMOD_SHIFT, KN_PGDN);
  add_keyseq("[7@", KMOD_CTRL|KMOD_SHIFT, KN_HOME);
  add_keyseq("[8@", KMOD_CTRL|KMOD_SHIFT, KN_END);
  add_keyseq("[7^", KMOD_CTRL, KN_HOME);
  add_keyseq("[8^", KMOD_CTRL, KN_END);
  add_keyseq("[2^", KMOD_CTRL, KN_INS);
  add_keyseq("[3^", KMOD_CTRL, KN_DEL);
  add_keyseq("[5$", KMOD_SHIFT, KN_PGUP);
  add_keyseq("[6$", KMOD_SHIFT, KN_PGDN);
  add_keyseq("[7$", KMOD_SHIFT, KN_HOME);
  add_keyseq("[8$", KMOD_SHIFT, KN_END);
  add_keyseq("[2$", KMOD_SHIFT, KN_INS);
  add_keyseq("[3$", KMOD_SHIFT, KN_DEL);

  // PuTTY (in application cursor mode)
  add_keyseq("[A", KMOD_CTRL, KN_UP);
  add_keyseq("[B", KMOD_CTRL, KN_DOWN);
  add_keyseq("[C", KMOD_CTRL, KN_RIGHT);
  add_keyseq("[D", KMOD_CTRL, KN_LEFT);

  // screen
  add_keyseq("Oa", KMOD_CTRL, KN_UP);
  add_keyseq("Ob", KMOD_CTRL, KN_DOWN);
  add_keyseq("Oc", KMOD_CTRL, KN_RIGHT);
  add_keyseq("Od", KMOD_CTRL, KN_LEFT);
  add_keyseq("[a", KMOD_SHIFT, KN_UP);
  add_keyseq("[b", KMOD_SHIFT, KN_DOWN);
  add_keyseq("[c", KMOD_SHIFT, KN_RIGHT);
  add_keyseq("[d", KMOD_SHIFT, KN_LEFT);

  // SS3 sequences, when ncurses doesn't know them
  add_keyseq("OA", 0, KN_UP);
  add_keyseq("OB", 0, KN_DOWN);
  add_keyseq("OC", 0, KN_RIGHT);
  add_keyseq("OD", 0, KN_LEFT);
  add_keyseq("OH", 0, KN_HOME);
  add_keyseq("OF", 0, KN_END);
  add_keyseq("OP", 0, KN_F(1));
  add_keyseq("OQ", 0, KN_F(2));
  add_keyseq("OR", 0, KN_F(3));
  add_keyseq("OS", 0, KN_F(4));
  add_keyseq("OM", 0, KN_ENTER);
}

//  scr_bind_key(name, command)
// Bind a command line to a key (given by its name, like "Alt+j").
// command == NULL removes the binding.  Returns FALSE if the key name
// cannot be parsed.
gboolean scr_bind_key(const char *name, const char *command, GError **err)
{
  gchar *canon = key_canonicalize(name, NULL);
  if (!canon) {
    // Maybe an old-style key code ("M106", "265"...)
    keyspec_t k;
    if (!key_from_legacy(name, &k)) {
      g_free(key_canonicalize(name, err)); // Sets the error
      return FALSE;
    }
    canon = key_format(&k);
  }
  mlua_unbind(canon);
  if (command)
    settings_set(SETTINGS_TYPE_BINDING, canon, command);
  else
    settings_del(SETTINGS_TYPE_BINDING, canon);
  g_free(canon);
  return TRUE;
}

//  scr_init_bindings()
// Create default key bindings
void scr_init_bindings(void)
{
  static const struct { const char *key, *cmd; } defaults[] = {
    { "Backspace",  "iline char_bdel" },
    { "Ctrl+h",     "iline char_bdel" },
    { "Del",        "iline char_fdel" },
    { "Left",       "iline bchar" },
    { "Right",      "iline fchar" },
    { "Ctrl+g",     "iline compl_cancel" },
    { "Up",         "iline line_up_or_hist" },
    { "Down",       "iline line_down_or_hist" },
    { "PgUp",       "roster up" },
    { "PgDn",       "roster down" },
    { "Home",       "iline iline_start" },
    { "Ctrl+a",     "iline iline_start" },
    { "End",        "iline iline_end" },
    { "Ctrl+e",     "iline iline_end" },
    { "Ctrl+Home",  "iline buffer_start" },
    { "Ctrl+End",   "iline buffer_end" },
    { "Alt+Enter",  "iline newline" },
    { "Ctrl+j",     "iline newline" },
    { "Ctrl+o",     "iline iline_accept_down_hist" },
    { "Ctrl+u",     "iline iline_bdel" },
    { "Ctrl+k",     "iline iline_fdel" },
    { "Ctrl+p",     "buffer up" },
    { "Ctrl+n",     "buffer down" },
    { "Ctrl+t",     "iline char_swap" },
    { "Ctrl+w",     "iline word_bdel" },
    { "Alt+b",      "iline bword" },
    { "Ctrl+Left",  "iline bword" },
    { "Alt+f",      "iline fword" },
    { "Ctrl+Right", "iline fword" },
    { "Alt+d",      "iline word_fdel" },
    { "Ctrl+l",     "screen_refresh" },
    { "Esc",        "chat_disable --show-roster" },
    { "Alt+Esc",    "chat_disable" },
    { "Ctrl+d",     "iline send_multiline" },
    { "Alt+u",      "iline word_upcase" },
    { "Alt+l",      "iline word_downcase" },
    { "Alt+c",      "iline word_capit" },
    { "F1",         "help" },
    { NULL, NULL }
  };
  int i;

  for (i = 0; defaults[i].key; i++)
    scr_bind_key(defaults[i].key, defaults[i].cmd, NULL);
}

//  is_speckey(key)
// Return TRUE if key is a special code, i.e. no char should be displayed on
// the screen.  It's not very nice, it's a workaround for the systems where
// isprint(KEY_PPAGE) returns TRUE...
static int is_speckey(int key)
{
  // ncurses key codes (KEY_*, function keys, run-time defined keys)
  return key == 127 || key >= KEY_MIN;
}

void scr_init_locale_charset(void)
{
  setlocale(LC_ALL, "");
#ifdef HAVE_LOCALCHARSET_H
  LocaleCharSet = locale_charset();
#else
  LocaleCharSet = nl_langinfo(CODESET);
#endif
  utf8_mode = (strcmp(LocaleCharSet, "UTF-8") == 0);
}

gboolean scr_curses_status(void)
{
  return Curses;
}

static gchar *scr_vi_mode_guard(const gchar *key, const gchar *new_value)
{
  int new_mode = 0;
  if (new_value)
    new_mode = atoi(new_value);
  if (new_mode == 0 || new_mode == 1)
    vi_mode = new_mode;
  return g_strdup(new_value);
}

static gchar *scr_color_guard(const gchar *key, const gchar *new_value)
{
  if (g_strcmp0(settings_opt_get(key), new_value))
    colors_stalled = TRUE;
  return g_strdup(new_value);
}

// Bracketed paste mode: the terminal sends \e[200~ before and \e[201~
// after pasted text.  write() is used so that it works in signal handlers.
static gboolean bracketed_paste;

static void bracketed_paste_disable(void)
{
  if (bracketed_paste) {
    static const char seq[] = "\033[?2004l";
    ssize_t ret = write(STDOUT_FILENO, seq, sizeof(seq) - 1);
    (void)ret;  // Nothing we can do if it fails
    bracketed_paste = FALSE;
  }
}

static void bracketed_paste_enable(void)
{
  static gboolean atexit_done;
  static const char seq[] = "\033[?2004h";
  const char *opt = settings_opt_get("paste_bracketed");

  if (opt && !atoi(opt))
    return;
  if (write(STDOUT_FILENO, seq, sizeof(seq) - 1) < 0)
    return;
  bracketed_paste = TRUE;
  if (!atexit_done) {
    atexit(bracketed_paste_disable);
    atexit_done = TRUE;
  }
}

void scr_init_curses(void)
{
  /* Key sequences initialization */
  init_keycodes();
  il_layout_init(&InputLayout);

  initscr();
  raw();
  noecho();
  nonl();
  intrflush(stdscr, FALSE);
  start_color();
  use_default_colors();
  colors_started = TRUE;
  bracketed_paste_enable();
#ifdef NCURSES_MOUSE_VERSION
  if (settings_opt_get_int("use_mouse"))
    mousemask(ALL_MOUSE_EVENTS, NULL);
#endif

  if (settings_opt_get("escdelay")) {
#ifdef HAVE_ESCDELAY
    ESCDELAY = (unsigned) settings_opt_get_int("escdelay");
#else
    scr_LogPrint(LPRINT_LOGNORM, "ERROR: no ESCDELAY support.");
#endif
  }

  // Set up vi_mode guard
  settings_set_guard("vi_mode", scr_vi_mode_guard);
  if (settings_opt_get_int("vi_mode") == 1)
    vi_mode = true;

  parse_colors();

  settings_set_guard("color_background", scr_color_guard);
  settings_set_guard("color_general", scr_color_guard);
  settings_set_guard("color_info", scr_color_guard);
  settings_set_guard("color_msgin", scr_color_guard);
  settings_set_guard("color_msgout", scr_color_guard);
  settings_set_guard("color_msghl", scr_color_guard);
  settings_set_guard("color_bgstatus", scr_color_guard);
  settings_set_guard("color_status", scr_color_guard);
  settings_set_guard("color_log", scr_color_guard);
  settings_set_guard("color_roster", scr_color_guard);
  settings_set_guard("color_bgrostersel", scr_color_guard);
  settings_set_guard("color_rostersel", scr_color_guard);
  settings_set_guard("color_rosterselmsg", scr_color_guard);
  settings_set_guard("color_rosternewmsg", scr_color_guard);
  settings_set_guard("color_timestamp", scr_color_guard);

  getmaxyx(stdscr, maxY, maxX);
  Log_Win_Height = DEFAULT_LOG_WIN_HEIGHT;
  // Note scr_draw_main_window() should be called early after scr_init_curses()
  // to update Log_Win_Height and set max{X,Y}

  inputLine[0] = 0;
  ptr_inputline = inputLine;

  Curses = TRUE;

  g_log_set_handler("GLib", G_LOG_LEVEL_MASK, scr_glog_print, NULL);
  return;
}

void scr_terminate_curses(void)
{
  if (!Curses) return;
  bracketed_paste_disable();
  clear();
  refresh();
  endwin();
  Curses = FALSE;
  return;
}

void scr_beep(void)
{
  beep();
}

// This and following belongs to dynamic setting of time prefix
static int timepreflengths[] = {
  // (length of the corresponding timeprefix + 5)
  17,
  11,
  6
};

guint scr_getprefixwidth(void)
{
  guint n = settings_opt_get_int("time_prefix");
  return timepreflengths[(n < 3 ? n : 0)];
}

//  scr_gettextwidth()
// Width of the chat window (the text width depends on the head of each
// message).
guint scr_gettextwidth(void)
{
  return chat_rect(FALSE).w;
}

guint scr_gettextheight(void)
{
  return Layout.chat.h;
}

guint scr_getlogwinheight(void)
{
  return Log_Win_Height;
}

//  scr_print_logwindow(string)
// Display the string in the log window.
// Note: The string must be in the user's locale!
void scr_print_logwindow(const char *string)
{
  time_t timestamp;
  char strtimestamp[64];

  timestamp = time(NULL);
  strftime(strtimestamp, 48, "[%H:%M:%S]", localtime(&timestamp));
  if (Curses) {
    wprintw(logWnd, "\n%s %s", strtimestamp, string);
    update_panels();
  } else {
    printf("%s %s\n", strtimestamp, string);
  }
}

//  scr_log_print(...)
// Display a message in the log window and in the status buffer.
// Add the message to the tracelog file if the log flag is set.
// This function will convert from UTF-8 unless the LPRINT_NOTUTF8 flag is set.
void scr_log_print(unsigned int flag, const char *fmt, ...)
{
  time_t timestamp;
  char strtimestamp[64];
  char *buffer, *btext;
  char *convbuf1 = NULL, *convbuf2 = NULL;
  va_list ap;

  if (!(flag & ~LPRINT_NOTUTF8)) return; // Shouldn't happen

  timestamp = time(NULL);
  strftime(strtimestamp, 48, "[%H:%M:%S]", localtime(&timestamp));
  va_start(ap, fmt);
  btext = g_strdup_vprintf(fmt, ap);
  va_end(ap);

  if (flag & LPRINT_NORMAL) {
    char *buffer_locale;
    char *buf_specialwindow;

    buffer = g_strdup_printf("%s %s", strtimestamp, btext);

    // Convert buffer to current locale for wprintw()
    if (!(flag & LPRINT_NOTUTF8))
      buffer_locale = convbuf1 = from_utf8(buffer);
    else
      buffer_locale = buffer;

    if (!buffer_locale) {
      wprintw(logWnd,
              "\n%s*Error: cannot convert string to locale.", strtimestamp);
      update_panels();
      g_free(buffer);
      g_free(btext);
      return;
    }

    // For the special status buffer, we need utf-8, but without the timestamp
    if (flag & LPRINT_NOTUTF8)
      buf_specialwindow = convbuf2 = to_utf8(btext);
    else
      buf_specialwindow = btext;

    if (Curses) {
      wprintw(logWnd, "\n%s", buffer_locale);
      update_panels();
      scr_write_in_window(NULL, buf_specialwindow, NULL, timestamp,
                          HBB_PREFIX_SPECIAL, FALSE, NULL);
    } else {
      printf("%s\n", buffer_locale);
      // ncurses are not initialized yet, so we call directly hbuf routine
      hbuf_add_line(&statushbuf, buf_specialwindow, NULL, timestamp,
        HBB_PREFIX_SPECIAL, 0, 0, NULL);
    }

    g_free(convbuf1);
    g_free(convbuf2);
    g_free(buffer);
  }

  if (flag & (LPRINT_LOG|LPRINT_DEBUG)) {
    strftime(strtimestamp, 23, "[%Y-%m-%d %H:%M:%S]", localtime(&timestamp));
    buffer = g_strdup_printf("%s %s\n", strtimestamp, btext);
    ut_write_log(flag, buffer);
    g_free(buffer);
  }
  g_free(btext);
}

// This is a GLogFunc for Glib log messages
static void scr_glog_print(const gchar *log_domain, GLogLevelFlags log_level,
                           const gchar *message, gpointer user_data)
{
  scr_log_print(LPRINT_NORMAL, "[%s] %s", log_domain, message);
}

static winbuf_t *scr_search_window(const char *winId, int special)
{
  char *id;
  winbuf_t *wbp;

  if (special)
    return statusWindow; // Only one special window atm.

  if (!winId)
    return NULL;

  id = g_strdup(winId);
  mc_strtolower(id);
  wbp = g_hash_table_lookup(winbufhash, id);
  g_free(id);
  return wbp;
}

int scr_buddy_buffer_exists(const char *bjid)
{
  return (scr_search_window(bjid, FALSE) != NULL);
}

//  scr_new_buddy(title, dontshow)
// Note: title (aka winId/jid) can be NULL for special buffers
/* -- Geometry of the chat window -- */

//  chat_rect(room)
// Position of the chat window: rooms leave room for the members column.
static rect_t chat_rect(gboolean room)
{
  rect_t r = Layout.chat;

  if (room || !Layout.members.w)
    return r;
  r.w += Layout.members.w + (Layout.borders ? 1 : 0);
  if (Layout.members.x < Layout.chat.x)
    r.x = Layout.members.x;
  return r;
}

static gboolean winbuf_is_room(winbuf_t *w)
{
  return w && w->jid && roster_find(w->jid, jidsearch, ROSTER_TYPE_ROOM);
}

//  sync_buffer_width(w)
// Rewrap the lines of the buffer if the width of its window has changed.
static int sync_buffer_width(winbuf_t *w)
{
  int width = chat_rect(winbuf_is_room(w)).w;

  if (w->bd->width != width) {
    w->bd->top = hbuf_previous_persistent(w->bd->top);
    hbuf_rebuild(&w->bd->hbuf, width);
    w->bd->width = width;
  }
  return width;
}

//  place_chat_window(w)
// Move/resize the (shared) chat window for this buffer.
static void place_chat_window(winbuf_t *w)
{
  rect_t r = chat_rect(winbuf_is_room(w));
  int y, x, h, ww;

  getbegyx(w->win, y, x);
  getmaxyx(w->win, h, ww);
  if (y != r.y || x != r.x || h != MAX(r.h, 1) || ww != MAX(r.w, 1)) {
    wresize(w->win, MAX(r.h, 1), MAX(r.w, 1));
    mvwin(w->win, r.y, r.x);
    if (w->panel)
      replace_panel(w->panel, w->win);
    werase(w->win);
  }
  sync_buffer_width(w);
}

static winbuf_t *scr_new_buddy(const char *title, int dont_show)
{
  winbuf_t *tmp;
  char *id;

  tmp = g_new0(winbuf_t, 1);

  tmp->win = activechatWnd;
  tmp->panel = activechatPanel;
  tmp->jid = g_strdup(title);

  if (!dont_show) {
    currentWindow = tmp;
  } else {
    if (currentWindow)
      top_panel(currentWindow->panel);
    else
      top_panel(chatPanel);
  }
  update_panels();

  // If title is NULL, this is a special buffer
  if (!title) {
    tmp->bd = g_new0(buffdata_t, 1);
    return tmp;
  }

  id = hlog_get_log_jid(title);
  if (id) {
    // This is a symlinked history log file.
    // Let's check if the target JID buffer has already been created.
    winbuf_t *wb = scr_search_window(id, FALSE);
    if (!wb)
      wb = scr_new_buddy(id, TRUE);
    tmp->bd = wb->bd;
    tmp->bd->refcount++;
    g_free(id);
  } else {  // Load buddy history from file (if enabled)
    tmp->bd = g_new0(buffdata_t, 1);
    tmp->bd->width = chat_rect(roster_find(title, jidsearch,
                                           ROSTER_TYPE_ROOM) != NULL).w;
    hlog_read_history(title, &tmp->bd->hbuf, tmp->bd->width);

    // Set a readmark to separate new content
    hbuf_set_readmark(tmp->bd->hbuf, TRUE);
  }

  id = g_strdup(title);
  mc_strtolower(id);
  g_hash_table_insert(winbufhash, id, tmp);

  return tmp;
}

//  nick_color(nick, flags, &attr)
// Color of a nickname in a room (nick_colors option, /color mucnick and
// /color muc).  Returns FALSE if the nickname is not colored.
static gboolean nick_color(const char *nick, guint flags, int *attr)
{
  char *mucjid, *key;
  nickcolor_t *actual = NULL;
  muccol_t type, *typetmp;

  if (!nick || !current_buddy)
    return FALSE;

  type = glob_muccol;
  mucjid = g_utf8_strdown(CURRENT_JID, -1);
  if (muccolors) {
    typetmp = g_hash_table_lookup(muccolors, mucjid);
    if (typetmp)
      type = *typetmp;
  }
  g_free(mucjid);

  // The nickname colors are stored with the "<nick>" and "*nick " keys
  key = g_strdup_printf("<%s>", nick);
  // Need to generate a color for the specified nick?
  if ((type == MC_ALL) && (!nickcolors ||
      !g_hash_table_lookup(nickcolors, key))) {
    nickcolor_t *nc;
    const char *p = key;
    unsigned int nicksum = 0;
    nc = g_new(nickcolor_t, 1);
    ensure_string_htable(&nickcolors, NULL);
    while (*p)
      nicksum += *p++;
    nc->color = nickcols[nicksum % nickcolcount];
    nc->manual = FALSE;
    g_hash_table_insert(nickcolors, g_strdup(key), nc);
    g_hash_table_insert(nickcolors, g_strdup_printf("*%s ", nick), nc);
  }
  if (nickcolors)
    actual = g_hash_table_lookup(nickcolors, key);
  g_free(key);
  if (actual && ((type == MC_ALL) || (actual->manual))
      && (flags & HBB_PREFIX_IN) && !(flags & HBB_PREFIX_HLIGHT_OUT)) {
    *attr = compose_color(actual->color);
    return TRUE;
  }
  return FALSE;
}

//  internal_style(name)
// Internal color (COLOR_*) of a style name, or -1.
static int internal_style(const char *name)
{
  static const struct { const char *name; int color; } styles[] = {
    { "general",      COLOR_GENERAL },
    { "msgout",       COLOR_MSGOUT },
    { "msghl",        COLOR_MSGHL },
    { "status",       COLOR_STATUS },
    { "log",          COLOR_LOG },
    { "roster",       COLOR_ROSTER },
    { "rostersel",    COLOR_ROSTERSEL },
    { "rosterselmsg", COLOR_ROSTERSELNMSG },
    { "rosternewmsg", COLOR_ROSTERNMSG },
    { "info",         COLOR_INFO },
    { "msgin",        COLOR_MSGIN },
    { "readmark",     COLOR_READMARK },
    { "timestamp",    COLOR_TIMESTAMP },
    { NULL, 0 }
  };
  int i;
  for (i = 0; styles[i].name; i++)
    if (!strcmp(name, styles[i].name))
      return styles[i].color;
  return -1;
}

//  style_attr(style, line_color, nick, flags)
// Attributes for a style name used in a message format.
static int style_attr(const char *style, int line_color, const char *nick,
                      guint flags)
{
  int color, attr;

  if (!style)
    return get_color(line_color);
  if (!strcmp(style, "nick"))
    return nick_color(nick, flags, &attr) ? attr : (int)get_color(line_color);
  color = internal_style(style);
  if (color >= 0)
    return get_color(color);
  if (custom_style_attr(style, &attr))
    return attr;
  return get_color(line_color);
}

//  run_attr(style, base)
// Attributes for a style of a format (roster, status lines).
static int run_attr(const char *style, int base)
{
  int color, attr;

  if (!style)
    return base;
  color = internal_style(style);
  if (color >= 0)
    return get_color(color);
  if (custom_style_attr(style, &attr))
    return attr;
  return base;
}

//  draw_sstr(win, s, base, plain, max)
// Draw a styled string at the cursor position (with the base attributes
// only if plain is TRUE), on at most max cells.
static void draw_sstr(WINDOW *win, const sstr_t *s, int base, gboolean plain,
                      int max)
{
  guint r;
  int used = 0;

  for (r = 0; r < s->runs->len; r++) {
    srun_t *run = &g_array_index(s->runs, srun_t, r);
    const char *p = s->text->str + run->start;
    const char *end = p + run->len;
    wattrset(win, plain ? base : run_attr(run->style, base));
    while (p < end) {
      const char *next = next_char((char *)p);
      int w = get_char_width(p);
      if (w < 0)
        w = 1;
      if (used + w > max)
        goto done;
      waddnstr(win, p, next - p);
      used += w;
      p = next;
    }
  }
done:
  wattrset(win, base);
}

//  status_icon(status, def)
// Icon of a status (mc.format.icons), or the given character.
static gchar *status_icon(const char *status, char def)
{
  gchar *icon = msgfmt_icon(status);
  return icon ? icon : g_strdup_printf("%c", def);
}

//  scr_update_window()
// (Re-)Display the given chat window.
static void scr_update_window(winbuf_t *win_entry)
{
  int n, mark_offset = 0;
  hbb_line **lines, *line;
  GList *hbuf_head;
  int color = COLOR_GENERAL;
  bool readmark = FALSE;
  bool skipline = FALSE;
  int autolock;

  autolock = settings_opt_get_int("buffer_smart_scrolling");

  place_chat_window(win_entry);

  // Should the window be empty?
  if (win_entry->bd->cleared) {
    werase(win_entry->win);
    if (autolock && win_entry->bd->lock)
      scr_buffer_scroll_lock(0);
    return;
  }

  // win_entry->bd->top is the top message of the screen.  If it set to NULL,
  // we are displaying the last messages.

  // We will show the last CHAT_WIN_HEIGHT lines.
  // Let's find out where it begins.
  if (!win_entry->bd->top || (g_list_position(g_list_first(win_entry->bd->hbuf),
                                              win_entry->bd->top) == -1)) {
    // Move up CHAT_WIN_HEIGHT lines
    win_entry->bd->hbuf = g_list_last(win_entry->bd->hbuf);
    hbuf_head = win_entry->bd->hbuf;
    win_entry->bd->top = NULL; // (Just to make sure)
    n = 0;
    while (hbuf_head && (n < CHAT_WIN_HEIGHT-1) && g_list_previous(hbuf_head)) {
      hbuf_head = g_list_previous(hbuf_head);
      n++;
    }
    // If the buffer is locked, remember current "top" line for the next time.
    if (win_entry->bd->lock)
      win_entry->bd->top = hbuf_head;
  } else
    hbuf_head = win_entry->bd->top;

  // Get the last CHAT_WIN_HEIGHT lines, and one more to detect scroll.
  lines = hbuf_get_lines(hbuf_head, CHAT_WIN_HEIGHT+1);

  if (CHAT_WIN_HEIGHT > 1) {
    // Do we have a read mark?
    for (n = 0; n < CHAT_WIN_HEIGHT; n++) {
      line = *(lines+n);
      if (line) {
        if (line->flags & HBB_PREFIX_READMARK) {
          // If this is not the last line, we'll display a mark
          if (n+1 < CHAT_WIN_HEIGHT && *(lines+n+1)) {
            readmark = TRUE;
            skipline = TRUE;
            mark_offset = -1;
          }
        }
      } else if (readmark) {
        // There will be empty lines, so we don't need to skip the first line
        skipline = FALSE;
        mark_offset = 0;
      }
    }
  }

  // Display the lines
  for (n = 0 ; n < CHAT_WIN_HEIGHT; n++) {
    int winy = n + mark_offset;
    wmove(win_entry->win, winy, 0);
    line = *(lines+n);
    if (line) {
      if (skipline)
        goto scr_update_window_skipline;

      if (line->flags & HBB_PREFIX_HLIGHT_OUT)
        color = COLOR_MSGOUT;
      else if (line->flags & HBB_PREFIX_HLIGHT)
        color = COLOR_MSGHL;
      else if (line->flags & HBB_PREFIX_INFO)
        color = COLOR_INFO;
      else if (line->flags & HBB_PREFIX_IN)
        color = COLOR_MSGIN;
      else
        color = COLOR_GENERAL;

      if (color != COLOR_GENERAL)
        wbkgdset(win_entry->win, get_color(color));

      // Display the head of the message (time, nickname...)
      if (line->flags & HBB_PREFIX_CONT) {
        wclrtoeol(win_entry->win);
      } else {
        sstr_t *head = sstr_new();
        guint r;
        msgfmt_head(line->timestamp, line->flags, line->nick, head);
        for (r = 0; r < head->runs->len; r++) {
          srun_t *run = &g_array_index(head->runs, srun_t, r);
          wbkgdset(win_entry->win,
                   style_attr(run->style, color, line->nick, line->flags));
          waddnstr(win_entry->win, head->text->str + run->start, run->len);
        }
        wbkgdset(win_entry->win, get_color(color));
        sstr_free(head);
        // The head may be narrower than when the line was wrapped (e.g. the
        // receipt mark has been removed)
        while (getcurx(win_entry->win) < (int)line->headw)
          waddch(win_entry->win, ' ');
      }

      // Display text line
      wmove(win_entry->win, winy,
            (line->flags & HBB_PREFIX_CONT) ? line->contw : line->headw);
      wprintw(win_entry->win, "%s", line->text);
      wclrtoeol(win_entry->win);

      // Restore default ("general") color
      if (color != COLOR_GENERAL)
        wbkgdset(win_entry->win, get_color(COLOR_GENERAL));

scr_update_window_skipline:
      skipline = FALSE;
      if (readmark && line->flags & HBB_PREFIX_READMARK) {
        int i, w;
        mark_offset++;

        // Display the mark
        winy = n + mark_offset;
        wmove(win_entry->win, winy, 0);
        wbkgdset(win_entry->win, get_color(COLOR_READMARK));
        wprintw(win_entry->win, "             == ");
        w = ((int)scr_gettextwidth() - 17) / 3;
        for (i=0; i<w; i++)
          wprintw(win_entry->win, "== ");
        wclrtoeol(win_entry->win);
        wbkgdset(win_entry->win, get_color(COLOR_GENERAL));
      }
      g_free(line->text);
      g_free(line);
    } else {
      wclrtobot(win_entry->win);
      break;
    }
  }
  line = *(lines+CHAT_WIN_HEIGHT); //line is scrolled out and never written
  if (line) {
    if (autolock && !win_entry->bd->lock) {
      if (!hbuf_jump_readmark(hbuf_head))
        scr_buffer_readmark(TRUE);
      scr_buffer_scroll_lock(1);
    }
    g_free(line->text);
    g_free(line);
  } else if (autolock && win_entry->bd->lock) {
    scr_buffer_scroll_lock(0);
  }

  g_free(lines);
}

static winbuf_t *scr_create_window(const char *winId, int special, int dont_show)
{
  if (special) {
    if (!statusWindow) {
      statusWindow = scr_new_buddy(NULL, dont_show);
      statusWindow->bd->hbuf = statushbuf;
    }
    return statusWindow;
  } else {
    return scr_new_buddy(winId, dont_show);
  }
}

//  scr_show_window()
// Display the chat window with the given identifier.
// "special" must be true if this is a special buffer window.
static void scr_show_window(const char *winId, int special)
{
  winbuf_t *win_entry;

  win_entry = scr_search_window(winId, special);

  if (!win_entry) {
    win_entry = scr_create_window(winId, special, FALSE);
  }

  console_shown = FALSE;
  top_panel(win_entry->panel);
  // The splash window is wider than a room window: hide it
  // (top_panel() shows it again)
  hide_panel(chatPanel);
  currentWindow = win_entry;
  chatmode = TRUE;
  if (!win_entry->bd->lock)
    roster_msg_setflag(winId, special, FALSE);
  if (!special)
    roster_setflags(winId, ROSTER_FLAG_LOCK, TRUE);
  scr_update_roster();

  // Refresh the window
  scr_update_window(win_entry);
  draw_members();

  // Finished :)
  update_panels();

  top_panel(inputPanel);
}

//  scr_show_buddy_window()
// Display the chat window buffer for the current buddy.
void scr_show_buddy_window(void)
{
  const gchar *bjid;

  buddylist_build();
  // The XMPP console stays displayed until another buddy is selected
  if (console_shown && current_buddy == console_buddy) {
    console_show();
    return;
  }
  if (!current_buddy) {
    bjid = NULL;
  } else {
    bjid = CURRENT_JID;
    if (buddy_gettype(BUDDATA(current_buddy)) & ROSTER_TYPE_SPECIAL) {
      scr_show_window(buddy_getname(BUDDATA(current_buddy)), TRUE);
      return;
    }
  }

  if (!bjid) {
    top_panel(chatPanel);
    top_panel(inputPanel);
    currentWindow = NULL;
    return;
  }

  roster_msg_update_unread(bjid, FALSE);
  scr_show_window(bjid, FALSE);
#ifdef HAVE_OMEMO
  if (buddy_gettype(BUDDATA(current_buddy)) & ROSTER_TYPE_USER)
    omemo_prefetch(bjid);
#endif
}

//  scr_update_buddy_window()
// (Re)Display the current window.
// If chatmode is enabled, call scr_show_buddy_window(),
// else display the chat window.
void scr_update_buddy_window(void)
{
  if (chatmode) {
    scr_show_buddy_window();
    return;
  }

  top_panel(chatPanel);
  top_panel(inputPanel);
}

//  scr_write_in_window()
// Write some text in the winId window (this usually is a jid).
// Use winId == NULL for the special status buffer.
// Lines are splitted when they are too long to fit in the chat window.
// If this window doesn't exist, it is created.
static void scr_write_in_window(const char *winId, const char *text,
                                const char *nick, time_t timestamp,
                                unsigned int prefix_flags, int force_show,
                                gpointer xep184)
{
  winbuf_t *win_entry;
  char *text_locale, *nick_locale;
  int dont_show = FALSE;
  int special;
  guint num_history_blocks;
  bool setmsgflg = FALSE;
  bool clearmsgflg = FALSE;

  // Look for the window entry.
  special = (winId == NULL);
  win_entry = scr_search_window(winId, special);

  // Do we have to really show the window?
  if (!chatmode)
    dont_show = TRUE;
  else if ((!force_show) && ((!currentWindow || (currentWindow != win_entry))))
    dont_show = TRUE;

  // If the window entry doesn't exist yet, let's create it.
  if (!win_entry) {
    win_entry = scr_create_window(winId, special, dont_show);
  }

  // The message must be displayed -> update top pointer
  if (win_entry->bd->cleared)
    win_entry->bd->top = g_list_last(win_entry->bd->hbuf);

  // Make sure we do not free the buffer while it's locked or when
  // top is set.
  if (win_entry->bd->lock || win_entry->bd->top)
    num_history_blocks = 0U;
  else
    num_history_blocks = get_max_history_blocks();

  text_locale = from_utf8(text);
  nick_locale = from_utf8(nick);
  hbuf_add_line(&win_entry->bd->hbuf, text_locale, nick_locale, timestamp,
                prefix_flags, sync_buffer_width(win_entry),
                num_history_blocks, xep184);
  g_free(text_locale);
  g_free(nick_locale);

  if (win_entry->bd->cleared) {
    win_entry->bd->cleared = FALSE;
    if (g_list_next(win_entry->bd->top))
      win_entry->bd->top = g_list_next(win_entry->bd->top);
  }

  // Make sure the last line appears in the window; update top if necessary
  if (!win_entry->bd->lock && win_entry->bd->top) {
    int dist;
    GList *first = g_list_first(win_entry->bd->hbuf);
    dist = g_list_position(first, g_list_last(win_entry->bd->hbuf)) -
           g_list_position(first, win_entry->bd->top);
    if (dist >= CHAT_WIN_HEIGHT)
      win_entry->bd->top = NULL;
  }

  if (!dont_show) {
    if (win_entry->bd->lock)
      setmsgflg = TRUE;
    else
      // If this is an outgoing message, remove the readmark
      if (!special && (prefix_flags & (HBB_PREFIX_OUT|HBB_PREFIX_HLIGHT_OUT)))
        hbuf_set_readmark(win_entry->bd->hbuf, FALSE);
    // Show and refresh the window
    top_panel(win_entry->panel);
    scr_update_window(win_entry);
    top_panel(inputPanel);
    update_panels();
  } else if (settings_opt_get_int("clear_unread_on_carbon") &&
             prefix_flags & HBB_PREFIX_OUT &&
             prefix_flags & HBB_PREFIX_CARBON) {
    clearmsgflg = TRUE;
  } else if (!(prefix_flags & HBB_PREFIX_NOFLAG)) {
    setmsgflg = TRUE;
  }
  if (!special) {
    if (clearmsgflg) {
      roster_msg_update_unread(winId, FALSE);
      roster_msg_setflag(winId, FALSE, FALSE);
      scr_update_roster();
    } else if (setmsgflg) {
      roster_msg_update_unread(winId, TRUE);
      roster_msg_setflag(winId, FALSE, TRUE);
      scr_update_roster();
    }
  }
}

static char *attention_sign_guard(const gchar *key, const gchar *new_value)
{
  scr_update_roster();
  if (g_strcmp0(settings_opt_get(key), new_value)) {
    guint sign;
    char *c;
    if (!new_value || !*new_value)
      return NULL;
    sign = get_char(new_value);
    c = next_char((char*)new_value);
    if (get_char_width(new_value) != 1 || !iswprint(sign) || *c) {
      scr_log_print(LPRINT_NORMAL, "attention_char value is invalid.");
      return NULL;
    }
    // The new value looks good (1-char  wide and printable)
    return g_strdup(new_value);
  }
  return g_strdup(new_value);
}

//  scr_init_settings()
// Create guards for UI settings
void scr_init_settings(void)
{
  settings_set_guard("attention_char", attention_sign_guard);
}

static unsigned int attention_sign(void)
{
  const char *as = settings_opt_get("attention_char");
  if (!as)
      return DEFAULT_ATTENTION_CHAR;
  return get_char(as);
}

//  scr_update_main_status(forceupdate)
// Redraw the main (bottom) status line.
// You can set forceupdate to FALSE in order to optimize screen refresh
// if you call top_panel()/update_panels() later.
typedef struct {
  guint unread;         // Unread/attention character (or ' ')
  const char *info;
  const char *msg;
} main_status_t;

static const char *const main_status_fields[] = {
  "unread", "status", "status_char", "icon", "info", "msg", "jid", NULL
};

static gchar *main_status_field(const char *name, const char *arg,
                                gpointer data)
{
  main_status_t *ms = data;
  enum imstatus st = xmpp_getstatus();

  if (!strcmp(name, "unread")) {
    gchar buf[8] = { 0 };
    if (ms->unread == ' ')
      return NULL;
    g_unichar_to_utf8(ms->unread, buf);
    return g_strdup(buf);
  }
  if (!strcmp(name, "status"))
    return g_strdup(mlua_status_name(st));
  if (!strcmp(name, "status_char"))
    return g_strdup_printf("%c", imstatus2char[st]);
  if (!strcmp(name, "icon"))
    return status_icon(mlua_status_name(st), imstatus2char[st]);
  if (!strcmp(name, "info"))
    return g_strdup(ms->info);
  if (!strcmp(name, "msg"))
    return (ms->msg && *ms->msg) ? g_strdup(ms->msg) : NULL;
  if (!strcmp(name, "jid"))
    return g_strdup(settings_opt_get("jid"));
  return NULL;
}

void scr_update_main_status(int forceupdate)
{
  char *sm = from_utf8(xmpp_getstatusmsg());
  const char *info = settings_opt_get("info");
  guint prio = 0;
  gpointer unread_ptr;
  guint unreadchar;

  unread_ptr = unread_msg(NULL);
  if (unread_ptr) {
    prio = buddy_getuiprio(unread_ptr);
    // If there's an unerad buffer but no priority set, let's consider the
    // priority is 1.
    if (!prio && buddy_getflags(unread_ptr) & ROSTER_FLAG_MSG)
      prio = 1;
  }

  // Status bar unread message flag
  if (prio >= ROSTER_UI_PRIO_MUC_HL_MESSAGE)
    unreadchar = attention_sign();
  else if (prio > 0)
    unreadchar = '#';
  else
    unreadchar = ' ';

  werase(mainstatusWnd);
  if (msgfmt_ui_has("main_status")) {
    main_status_t ms;
    sstr_t *line = sstr_new();
    ms.unread = unreadchar;
    ms.info = info;
    ms.msg = sm;
    if (msgfmt_ui("main_status", NULL, main_status_fields, main_status_field,
                  &ms, getmaxx(mainstatusWnd), line)) {
      wmove(mainstatusWnd, 0, 0);
      draw_sstr(mainstatusWnd, line, getbkgd(mainstatusWnd) & A_ATTRIBUTES,
                FALSE, getmaxx(mainstatusWnd));
      sstr_free(line);
      goto main_status_done;
    }
    sstr_free(line);
  }
  if (info) {
    char *info_locale = from_utf8(info);
    mvwprintw(mainstatusWnd, 0, 0, "%lc[%c] %s %s", unreadchar,
              imstatus2char[xmpp_getstatus()],
              info_locale, (sm ? sm : ""));
    g_free(info_locale);
  } else
    mvwprintw(mainstatusWnd, 0, 0, "%lc[%c] %s", unreadchar,
              imstatus2char[xmpp_getstatus()], (sm ? sm : ""));
main_status_done:
  if (forceupdate) {
    top_panel(inputPanel);
    update_panels();
  }
  g_free(sm);
}

/* -- Members of the current room -- */

enum { MEMBERS_NONE, MEMBERS_COLUMN, MEMBERS_OVERLAY };

//  members_mode()
// muc_members option: "column" (default), "overlay" or "none".
static int members_mode(void)
{
  const char *m = settings_opt_get("muc_members");
  if (m && (!strcmp(m, "none") || !strcmp(m, "0") || !strcmp(m, "off")))
    return MEMBERS_NONE;
  if (m && !strcmp(m, "overlay"))
    return MEMBERS_OVERLAY;
  return MEMBERS_COLUMN;
}

static int members_width(void)
{
  int w = settings_opt_get_int("muc_members_width");
  return w > 2 ? w : 20;
}

/* -- Frames -- */

typedef struct {
  const char *name;
  const char *h, *v, *tl, *tr, *bl, *br, *lt, *rt, *tt, *bt;
} frame_chars_t;

static const frame_chars_t frame_sets[] = {
  { "ascii",   "-", "|", "+", "+", "+", "+", "+", "+", "+", "+" },
  { "single",  "\u2500", "\u2502", "\u250c", "\u2510", "\u2514", "\u2518",
               "\u251c", "\u2524", "\u252c", "\u2534" },
  { "rounded", "\u2500", "\u2502", "\u256d", "\u256e", "\u2570", "\u256f",
               "\u251c", "\u2524", "\u252c", "\u2534" },
  { "double",  "\u2550", "\u2551", "\u2554", "\u2557", "\u255a", "\u255d",
               "\u2560", "\u2563", "\u2566", "\u2569" },
  { "heavy",   "\u2501", "\u2503", "\u250f", "\u2513", "\u2517", "\u251b",
               "\u2523", "\u252b", "\u2533", "\u253b" },
  { NULL }
};

//  frame_chars()
// Characters of the frames (ui_borders option), or NULL if there is none.
static const frame_chars_t *frame_chars(void)
{
  const char *opt = settings_opt_get("ui_borders");
  int i;

  if (!opt || !*opt || !strcmp(opt, "none") || !strcmp(opt, "0"))
    return NULL;
  if (!utf8_mode)
    return &frame_sets[0];
  for (i = 0; frame_sets[i].name; i++)
    if (!strcmp(opt, frame_sets[i].name))
      return &frame_sets[i];
  return &frame_sets[2];  // rounded
}

static int named_style_attr(const char *name, int fallback)
{
  int attr;
  if (custom_style_attr(name, &attr))
    return attr;
  return get_color(fallback);
}

//  fill_hline(win, y, x, n)
// Draw a horizontal frame line.
static void fill_hline(WINDOW *win, int y, int x, int n)
{
  const frame_chars_t *fc = frame_chars();
  int i;
  if (!fc)
    return;
  wmove(win, y, x);
  for (i = 0; i < n; i++)
    waddstr(win, fc->h);
}

static void draw_title(int y, int x, int maxw, const char *option,
                       const char *def)
{
  const char *title = settings_opt_get(option);
  gchar *t, *tl;

  if (!title)
    title = def;
  if (!*title || maxw < 5)
    return;
  t = g_strdup_printf(" %s ", title);
  tl = from_utf8(t);
  wattrset(frameWnd, named_style_attr("title", COLOR_GENERAL));
  mvwaddnstr(frameWnd, y, x, tl ? tl : t, maxw);
  g_free(t);
  g_free(tl);
}

//  draw_frames()
// Draw the frames around the windows (see layout_borders()).
static void draw_frames(void)
{
  const frame_chars_t *fc = frame_chars();
  const ui_layout_t *l = &Layout;
  int y, x;
  // Rows of the line between the roster and the chat column
  int vtop = l->roster.y, vbot = l->roster.y + l->roster.h - 1;
  // Rows of the line between the chat window and the members column
  gboolean msep = members_shown && members_mode() == MEMBERS_COLUMN &&
                  l->frame_msep >= 0;
  int mtop = l->chat.y, mbot = l->chat.y + l->chat.h - 1;

  werase(frameWnd);
  if (!fc || !l->borders)
    return;

  wattrset(frameWnd, named_style_attr("border", COLOR_GENERAL));

  for (y = l->frame_top; y <= l->frame_bottom; y++) {
    const char *left, *right;
    if (y == l->frame_top) {
      left = fc->tl; right = fc->tr;
    } else if (y == l->frame_bottom) {
      left = fc->bl; right = fc->br;
    } else if (y == l->frame_mid) {
      left = fc->lt; right = fc->rt;
    } else {
      // Vertical lines
      mvwaddstr(frameWnd, y, l->frame_left, fc->v);
      mvwaddstr(frameWnd, y, l->frame_right, fc->v);
      if (l->frame_vsep >= 0 && y >= vtop && y <= vbot)
        mvwaddstr(frameWnd, y, l->frame_vsep, fc->v);
      if (msep && y >= mtop && y <= mbot)
        mvwaddstr(frameWnd, y, l->frame_msep, fc->v);
      if (y == l->frame_isep) {
        // Line between the chat window and the input line
        int x0 = l->input.x - 1, x1 = l->input.x + l->input.w;
        mvwaddstr(frameWnd, y, x0, fc->lt);
        for (x = x0 + 1; x < x1; x++)
          waddstr(frameWnd, fc->h);
        waddstr(frameWnd, fc->rt);
        if (msep)
          mvwaddstr(frameWnd, y, l->frame_msep, fc->bt);
      }
      continue;
    }
    mvwaddstr(frameWnd, y, l->frame_left, left);
    for (x = l->frame_left + 1; x < l->frame_right; x++)
      waddstr(frameWnd, fc->h);
    waddstr(frameWnd, right);
    // Junctions with the vertical lines
    if (l->frame_vsep >= 0) {
      if (y == vtop - 1)
        mvwaddstr(frameWnd, y, l->frame_vsep, fc->tt);
      else if (y == vbot + 1)
        mvwaddstr(frameWnd, y, l->frame_vsep, fc->bt);
    }
    if (msep) {
      if (y == mtop - 1)
        mvwaddstr(frameWnd, y, l->frame_msep, fc->tt);
      else if (y == mbot + 1)
        mvwaddstr(frameWnd, y, l->frame_msep, fc->bt);
    }
  }

  // Titles
  if (l->roster.w > 6)
    draw_title(l->roster.y - 1, l->roster.x + 1, l->roster.w - 2,
               "ui_title_roster", "Roster");
  if (msep && l->members.w > 6)
    draw_title(l->members.y - 1, l->members.x + 1, l->members.w - 2,
               "ui_title_members", "Members");
  if (l->log.h > 0)
    draw_title(l->log.y - 1, l->log.x + 1, l->log.w - 2,
               "ui_title_log", "Log");
}

//  scr_draw_main_window()
// Set fullinit to TRUE to also create panels.  Set it to FALSE for a resize.
//
// I think it could be improved a _lot_ but I'm really not an ncurses
// expert... :-\   Mikael.
//
void scr_draw_main_window(unsigned int fullinit)
{
  int requested_size;
  gchar *ver, *message;
  layout_opts_t lopts;

  if (maxY < 4)
    maxY = 4;

  if (NULL == settings_opt_get("log_win_height"))
    requested_size = DEFAULT_LOG_WIN_HEIGHT;
  else
    requested_size = settings_opt_get_int("log_win_height");
  lopts.log_height = MAX(requested_size, 0);

  if (roster_hidden) {
    lopts.roster_width = 0;
  } else {
    requested_size = settings_opt_get_int("roster_width");
    if (requested_size > 1)
      lopts.roster_width = requested_size;
    else if (requested_size == 1)
      lopts.roster_width = 2;
    else
      lopts.roster_width = DEFAULT_ROSTER_WIDTH;
  }

  log_win_on_top = (settings_opt_get_int("log_win_on_top") == 1);
  roster_win_on_right = (settings_opt_get_int("roster_win_on_right") == 1);

  lopts.lines = maxY;
  lopts.cols = maxX;
  lopts.log_on_top = log_win_on_top;
  lopts.roster_on_right = roster_win_on_right;
  lopts.input_height = Input_Win_Height;
  lopts.borders = (frame_chars() != NULL);
  {
    const char *pos = settings_opt_get("input_position");
    lopts.input_in_chat = !(pos && !strcmp(pos, "bottom"));
  }
  lopts.members_width = (members_mode() == MEMBERS_COLUMN && !members_hidden) ?
                        members_width() : 0;
  lopts.members_on_left = settings_opt_get_int("muc_members_left") == 1;
  layout_compute(&lopts, &Layout);
  Log_Win_Height = Layout.log.h;
  Roster_Width = Layout.roster.w;

#define NEWWIN(r) newwin((r).h, (r).w, (r).y, (r).x)
#define MOVEWIN(win, r) do { wresize((win), MAX((r).h, 1), MAX((r).w, 1)); \
                             mvwin((win), (r).y, (r).x); } while (0)

  if (fullinit) {
    if (!winbufhash)
      winbufhash = g_hash_table_new_full(g_str_hash, g_str_equal,
                                         g_free, winbuf_free);
    /* Create windows */
    frameWnd  = newwin(maxY, maxX, 0, 0);
    membersWnd = newwin(1, 1, 0, 0);
    rosterWnd = NEWWIN(Layout.roster);
    chatWnd   = NEWWIN(chat_rect(FALSE));
    activechatWnd = NEWWIN(Layout.chat);
    logWnd    = NEWWIN(Layout.log);
    chatstatusWnd = NEWWIN(Layout.chatstatus);
    mainstatusWnd = NEWWIN(Layout.mainstatus);
    inputWnd  = NEWWIN(Layout.input);
    if (!rosterWnd || !chatWnd || !logWnd || !inputWnd) {
      scr_terminate_curses();
      fprintf(stderr, "Cannot create windows!\n");
      exit(EXIT_FAILURE);
    }
    wbkgd(rosterWnd,      get_color(COLOR_GENERAL));
    wbkgd(chatWnd,        get_color(COLOR_GENERAL));
    wbkgd(activechatWnd,  get_color(COLOR_GENERAL));
    wbkgd(logWnd,         get_color(COLOR_LOG));
    wbkgd(chatstatusWnd,  get_color(COLOR_STATUS));
    wbkgd(mainstatusWnd,  get_color(COLOR_STATUS));
  } else {
    /* Resize/move windows */
    MOVEWIN(rosterWnd, Layout.roster);
    MOVEWIN(chatWnd, chat_rect(FALSE));
    MOVEWIN(logWnd, Layout.log);
    MOVEWIN(chatstatusWnd, Layout.chatstatus);
    MOVEWIN(mainstatusWnd, Layout.mainstatus);
    MOVEWIN(inputWnd, Layout.input);
    wresize(frameWnd, maxY, maxX);

    werase(chatWnd);
  }

  /* Draw/init windows */

  ver = mcabber_version();
  message = g_strdup_printf("mcabber-ng version %s.\n", ver);
  mvwprintw(chatWnd, 0, 0, "%s", message);
  mvwprintw(chatWnd, 1, 0, "https://github.com/h5vx/mcabber-ng");
  g_free(ver);
  g_free(message);

  // Auto-scrolling in log window
  scrollok(logWnd, TRUE);


  if (fullinit) {
    // Enable keypad (+ special keys)
    keypad(inputWnd, TRUE);
#ifdef __MirBSD__
    wtimeout(inputWnd, 50 /* ms */);
#else
    nodelay(inputWnd, TRUE);
#endif

    // Create panels
    framePanel  = new_panel(frameWnd);
    membersPanel = new_panel(membersWnd);
    hide_panel(membersPanel);
    rosterPanel = new_panel(rosterWnd);
    chatPanel   = new_panel(chatWnd);
    activechatPanel = new_panel(activechatWnd);
    logPanel    = new_panel(logWnd);
    chatstatusPanel = new_panel(chatstatusWnd);
    mainstatusPanel = new_panel(mainstatusWnd);
    inputPanel  = new_panel(inputWnd);

    // Build the buddylist at least once, to make sure the special buffer
    // is added
    buddylist_defer_build();

    // Init prev_chatwidth; this variable will be used to prevent us
    // from rewrapping buffers when the width doesn't change.
    prev_chatwidth = Layout.chat.w;
    // Wrap existing status buffer lines
    hbuf_rebuild(&statushbuf, prev_chatwidth);

#ifndef UNICODE
    if (utf8_mode)
      scr_LogPrint(LPRINT_NORMAL,
                   "WARNING: Compiled without full UTF-8 support!");
#endif
  } else {
    // Update panels
    replace_panel(framePanel, frameWnd);
    replace_panel(rosterPanel, rosterWnd);
    replace_panel(chatPanel, chatWnd);
    replace_panel(logPanel, logWnd);
    replace_panel(chatstatusPanel, chatstatusWnd);
    replace_panel(mainstatusPanel, mainstatusWnd);
    replace_panel(inputPanel, inputWnd);
  }

  if (0 == Log_Win_Height) {
    hide_panel(logPanel);
  } else {
    show_panel(logPanel);
  }

  // Frames are behind everything
  place_chatstatus();
  if (Layout.borders) {
    draw_frames();
    show_panel(framePanel);
    bottom_panel(framePanel);
    wbkgd(chatstatusWnd, named_style_attr("title_active", COLOR_STATUS));
  } else {
    hide_panel(framePanel);
    wbkgd(chatstatusWnd, get_color(COLOR_STATUS));
  }

  // We'll need to redraw the roster
  scr_update_roster();
  return;
}

static void resize_win_buffer(gpointer key, gpointer value, gpointer data)
{
  winbuf_t *wbp = value;
  struct dimensions *dim = data;
  int new_chatwidth;

  if (!(wbp && wbp->win))
    return;

  // The window is placed when the buffer is displayed (it is shared by
  // all buffers); the lines are wrapped again if needed.
  (void)dim;
  (void)new_chatwidth;
  sync_buffer_width(wbp);
}

//  scr_resize()
// Function called when the window is resized.
// - Resize windows
// - Rewrap lines in each buddy buffer
void scr_resize(void)
{
  struct dimensions dim;

  // First, update the global variables
  getmaxyx(stdscr, maxY, maxX);
  // scr_draw_main_window() will take care of maxY and Log_Win_Height

  // Make sure the cursor stays inside the window

  // Resize windows and update panels
  scr_draw_main_window(FALSE);

  // Resize all buddy windows
  dim.l = MAX(Layout.chat.h, 1);
  dim.c = MAX(Layout.chat.w, 1);

  // Resize all buffers
  g_hash_table_foreach(winbufhash, resize_win_buffer, &dim);

  // Resize/move special status buffer
  if (statusWindow)
    resize_win_buffer(NULL, statusWindow, &dim);

  // Update prev_chatwidth, now that all buffers have been resized
  prev_chatwidth = Layout.chat.w;

  // Refresh current buddy window
  if (chatmode)
    scr_show_buddy_window();
}

#ifdef USE_SIGWINCH
void sigwinch_resize(void)
{
  struct winsize size;
  if (ioctl(STDIN_FILENO, TIOCGWINSZ, &size) != -1)
    resizeterm(size.ws_row, size.ws_col);
  scr_resize();
}
#endif

//  scr_update_chat_status(forceupdate)
// Redraw the buddy status bar.
// Set forceupdate to TRUE if update_panels() must be called.
/* -- Status lines formats (mc.format.chat_status, mc.format.main_status) -- */

typedef struct {
  gpointer bud;
  guint btype;
  char status;
  const char *status_name;
  const char *resource;
  gchar *msg;
  const char *chatstate;
  gboolean locked;
} chat_status_t;

static const char *const chat_status_fields[] = {
  "name", "jid", "resource", "type", "type_name", "status", "status_char",
  "icon", "msg", "chatstate", "chatmode", "locked", "unread", "encryption",
  NULL
};

static gchar *chat_status_field(const char *name, const char *arg,
                                gpointer data)
{
  chat_status_t *cs = data;
  guint t = cs->btype;

  if (!strcmp(name, "name"))
    return g_strdup(buddy_getname(cs->bud));
  if (!strcmp(name, "jid"))
    return g_strdup(buddy_getjid(cs->bud));
  if (!strcmp(name, "resource"))
    return g_strdup(cs->resource);
  if (!strcmp(name, "type"))
    return g_strdup((t & ROSTER_TYPE_USER) ? "user" :
                    (t & ROSTER_TYPE_ROOM) ? "room" :
                    (t & ROSTER_TYPE_AGENT) ? "agent" :
                    (t & ROSTER_TYPE_GROUP) ? "group" : "special");
  if (!strcmp(name, "type_name"))
    return g_strdup((t & ROSTER_TYPE_USER) ? "Buddy" :
                    (t & ROSTER_TYPE_ROOM) ? "Room" :
                    (t & ROSTER_TYPE_AGENT) ? "Agent" :
                    (t & ROSTER_TYPE_GROUP) ? "Group" : "Special buffer");
  if (!strcmp(name, "status"))
    return g_strdup(cs->status_name);
  if (!strcmp(name, "status_char"))
    return cs->status ? g_strdup_printf("%c", cs->status) : NULL;
  if (!strcmp(name, "icon"))
    return cs->status_name ? status_icon(cs->status_name, cs->status) : NULL;
  if (!strcmp(name, "msg"))
    return g_strdup(cs->msg);
  if (!strcmp(name, "chatstate"))
    return g_strdup(cs->chatstate);
  if (!strcmp(name, "chatmode"))
    return chatmode ? g_strdup("1") : NULL;
  if (!strcmp(name, "locked"))
    return cs->locked ? g_strdup("1") : NULL;
  if (!strcmp(name, "unread"))
    return (buddy_getflags(cs->bud) & ROSTER_FLAG_MSG) ? g_strdup("1") : NULL;
#ifdef HAVE_OMEMO
  // Encryption of the next message sent
  if (!strcmp(name, "encryption"))
    return (t & ROSTER_TYPE_USER) ?
           g_strdup(omemo_status(buddy_getjid(cs->bud))) : NULL;
#endif
  return NULL;
}

//  chat_status_format()
// Draw the chat status line with mc.format.chat_status.  Returns FALSE if
// it is not set.
static gboolean chat_status_format(void)
{
  chat_status_t cs;
  sstr_t *line;
  gboolean done;
  guint ev;

  if (!msgfmt_ui_has("chat_status"))
    return FALSE;

  memset(&cs, 0, sizeof(cs));
  cs.bud = BUDDATA(current_buddy);
  cs.btype = buddy_gettype(cs.bud);
  if (cs.btype & (ROSTER_TYPE_USER|ROSTER_TYPE_AGENT)) {
    enum imstatus st;
    cs.resource = buddy_getactiveresource(cs.bud);
    st = buddy_getstatus(cs.bud, cs.resource);
    cs.status = (xmpp_getstatus() != offline && st < imstatus_size) ?
                imstatus2char[st] : '?';
    cs.status_name = (xmpp_getstatus() != offline) ? mlua_status_name(st) :
                     "unknown";
    cs.msg = g_strdup(buddy_getstatusmsg(cs.bud, cs.resource));
    ev = buddy_resource_getevents(cs.bud, cs.resource);
    cs.chatstate = (ev == ROSTER_EVENT_ACTIVE) ? "active" :
                   (ev == ROSTER_EVENT_COMPOSING) ? "composing" :
                   (ev == ROSTER_EVENT_PAUSED) ? "paused" :
                   (ev == ROSTER_EVENT_INACTIVE) ? "inactive" :
                   (ev == ROSTER_EVENT_GONE) ? "gone" : NULL;
  } else if (cs.btype & ROSTER_TYPE_ROOM) {
    gboolean inside = buddy_getinsideroom(cs.bud) != 0;
    cs.status = inside ? 'C' : 'x';
    cs.status_name = inside ? "room" : "room_out";
    cs.msg = g_strdup(buddy_gettopic(cs.bud));
  }
  if (cs.msg)
    replace_nl_with_dots(cs.msg);
  if (chatmode && !(cs.btype & ROSTER_TYPE_GROUP)) {
    winbuf_t *win_entry = scr_search_window(buddy_getjid(cs.bud),
                                            cs.btype & ROSTER_TYPE_SPECIAL);
    cs.locked = win_entry && win_entry->bd->lock;
  }

  line = sstr_new();
  done = msgfmt_ui("chat_status", NULL, chat_status_fields,
                   chat_status_field, &cs, getmaxx(chatstatusWnd), line);
  if (done) {
    wmove(chatstatusWnd, 0, 0);
    draw_sstr(chatstatusWnd, line, getbkgd(chatstatusWnd) & A_ATTRIBUTES,
              FALSE, getmaxx(chatstatusWnd));
  }
  sstr_free(line);
  g_free(cs.msg);
  return done;
}

void scr_update_chat_status(int forceupdate)
{
  unsigned short btype, isgrp, ismuc, isspe;
  const char *btypetext = "Unknown";
  const char *fullname;
  char *fullnameres = NULL;
  const char *activeres;
  const char *msg = NULL;
  char status;
  char *buf, *buf_locale;

  // Usually we need to update the bottom status line too,
  // at least to refresh the pending message flag.
  scr_update_main_status(FALSE);

  // Clear the line
  werase(chatstatusWnd);
  if (Layout.borders) {
    wattrset(chatstatusWnd, named_style_attr("border", COLOR_GENERAL));
    fill_hline(chatstatusWnd, 0, 0, getmaxx(chatstatusWnd));
    wattrset(chatstatusWnd, A_NORMAL);
    wmove(chatstatusWnd, 0, 0);
  }

  if (console_shown) {
    wattrset(chatstatusWnd, A_BOLD);
    mvwprintw(chatstatusWnd, 0, 1, " XMPP console ");
    wattrset(chatstatusWnd, A_NORMAL);
    if (forceupdate)
      update_panels();
    return;
  }

  if (!current_buddy) {
    if (forceupdate) {
      update_panels();
    }
    return;
  }

  if (chat_status_format()) {
    if (forceupdate)
      update_panels();
    return;
  }

  fullname = buddy_getname(BUDDATA(current_buddy));
  btype = buddy_gettype(BUDDATA(current_buddy));

  isgrp = ismuc = isspe = 0;
  if (btype & ROSTER_TYPE_USER) {
    btypetext = "Buddy";
  } else if (btype & ROSTER_TYPE_GROUP) {
    btypetext = "Group";
    isgrp = 1;
  } else if (btype & ROSTER_TYPE_AGENT) {
    btypetext = "Agent";
  } else if (btype & ROSTER_TYPE_ROOM) {
    btypetext = "Room";
    ismuc = 1;
  } else if (btype & ROSTER_TYPE_SPECIAL) {
    btypetext = "Special buffer";
    isspe = 1;
  }

  if (chatmode) {
    wprintw(chatstatusWnd, "~");
  } else {
    unsigned short bflags = buddy_getflags(BUDDATA(current_buddy));
    if (bflags & ROSTER_FLAG_MSG) {
      // There is an unread message from the current buddy
      wprintw(chatstatusWnd, "#");
    }
  }

  if (chatmode && !isgrp) {
    winbuf_t *win_entry;
    win_entry = scr_search_window(buddy_getjid(BUDDATA(current_buddy)), isspe);
    if (win_entry && win_entry->bd->lock)
      mvwprintw(chatstatusWnd, 0, 0, "*");
  }

  if (isgrp || isspe) {
    buf_locale = from_utf8(fullname);
    mvwprintw(chatstatusWnd, 0, 5, "%s: %s", btypetext, buf_locale);
    g_free(buf_locale);
    if (forceupdate) {
      update_panels();
    }
    return;
  }

  status = '?';

  activeres = buddy_getactiveresource(BUDDATA(current_buddy));

  if (ismuc) {
    if (buddy_getinsideroom(BUDDATA(current_buddy)))
      status = 'C';
    else
      status = 'x';
  } else if (xmpp_getstatus() != offline) {
    enum imstatus budstate;
    budstate = buddy_getstatus(BUDDATA(current_buddy), activeres);
    if (budstate < imstatus_size)
      status = imstatus2char[budstate];
  }

  // No status message for MUC rooms
  if (!ismuc) {
    if (activeres) {
      fullnameres = g_strdup_printf("%s/%s", fullname, activeres);
      fullname = fullnameres;
      msg = buddy_getstatusmsg(BUDDATA(current_buddy), activeres);
    } else {
      GSList *resources, *p_res, *p_next_res;
      resources = buddy_getresources(BUDDATA(current_buddy));

      for (p_res = resources ; p_res ; p_res = p_next_res) {
        p_next_res = g_slist_next(p_res);
        // Store the status message of the latest resource (highest priority)
        if (!p_next_res)
          msg = buddy_getstatusmsg(BUDDATA(current_buddy), p_res->data);
        g_free(p_res->data);
      }
      g_slist_free(resources);
    }
  } else {
    msg = buddy_gettopic(BUDDATA(current_buddy));
  }

  if (msg)
    buf = g_strdup_printf("[%c] %s: %s -- %s", status, btypetext, fullname, msg);
  else
    buf = g_strdup_printf("[%c] %s: %s", status, btypetext, fullname);
  replace_nl_with_dots(buf);
  buf_locale = from_utf8(buf);
  mvwprintw(chatstatusWnd, 0, 1, "%s", buf_locale);
  g_free(fullnameres);
  g_free(buf_locale);
  g_free(buf);

  // Display chatstates of the contact, if available.
  if (btype & ROSTER_TYPE_USER) {
    char eventchar = 0;
    guint event;

    // We specify active resource here, so when there is none then the resource
    // with the highest priority will be used.
    event = buddy_resource_getevents(BUDDATA(current_buddy), activeres);

    if (event == ROSTER_EVENT_ACTIVE)
      eventchar = 'A';
    else if (event == ROSTER_EVENT_COMPOSING)
      eventchar = 'C';
    else if (event == ROSTER_EVENT_PAUSED)
      eventchar = 'P';
    else if (event == ROSTER_EVENT_INACTIVE)
      eventchar = 'I';
    else if (event == ROSTER_EVENT_GONE)
      eventchar = 'G';

    if (eventchar)
      mvwprintw(chatstatusWnd, 0, getmaxx(chatstatusWnd)-3, "[%c]", eventchar);
  }


  if (forceupdate) {
    update_panels();
  }
}

void increment_if_buddy_not_filtered(gpointer rosterdata, void *param)
{
  int *p = param;
  if (buddylist_is_status_filtered(buddy_getstatus(rosterdata, NULL)))
    *p=*p+1;
}

/* -- Members panel (muc_members option, mc.format.members) -- */

typedef struct {
  gchar *name;
  enum imrole role;
  enum imaffiliation affil;
  enum imstatus status;
  gboolean self;
} member_t;

static int role_rank(enum imrole r)
{
  switch (r) {
    case role_moderator:   return 0;
    case role_participant: return 1;
    case role_visitor:     return 2;
    default:               return 3;
  }
}

static gint member_cmp(gconstpointer a, gconstpointer b)
{
  const member_t *x = *(member_t * const *)a, *y = *(member_t * const *)b;
  gchar *kx, *ky;
  gint r = role_rank(x->role) - role_rank(y->role);

  if (r)
    return r;
  kx = g_utf8_casefold(x->name, -1);
  ky = g_utf8_casefold(y->name, -1);
  r = g_utf8_collate(kx, ky);
  g_free(kx);
  g_free(ky);
  return r;
}

static void member_free(gpointer data)
{
  member_t *m = data;
  g_free(m->name);
  g_free(m);
}

static char role_char(enum imrole r)
{
  return r == role_moderator ? '@' : r == role_participant ? '+' : ' ';
}

static const char *const member_fields[] = {
  "name", "role", "role_char", "affil", "status", "icon", "self", NULL
};

static gchar *member_field(const char *name, const char *arg, gpointer data)
{
  member_t *m = data;

  if (!strcmp(name, "name"))
    return g_strdup(m->name);
  if (!strcmp(name, "role"))
    return m->role != role_none ? g_strdup(strrole[m->role]) : NULL;
  if (!strcmp(name, "role_char"))
    return g_strdup_printf("%c", role_char(m->role));
  if (!strcmp(name, "affil"))
    return m->affil != affil_none ? g_strdup(straffil[m->affil]) : NULL;
  if (!strcmp(name, "status"))
    return g_strdup(mlua_status_name(m->status));
  if (!strcmp(name, "icon"))
    return status_icon(mlua_status_name(m->status), imstatus2char[m->status]);
  if (!strcmp(name, "self"))
    return m->self ? g_strdup("1") : NULL;
  return NULL;
}

//  place_chatstatus()
// With frames, the chat status line is in the frame above the chat window,
// whose width depends on the members column.
static void place_chatstatus(void)
{
  if (Layout.borders) {
    rect_t c = chat_rect(members_shown && members_mode() == MEMBERS_COLUMN);
    rect_t r = { Layout.chatstatus.y, c.x + 1, 1, MAX(c.w - 2, 1) };
    MOVEWIN(chatstatusWnd, r);
    replace_panel(chatstatusPanel, chatstatusWnd);
  }
}

//  draw_members()
// Display the members of the current room (if this is a room).
static void draw_members(void)
{
  gboolean show = chatmode && currentWindow && winbuf_is_room(currentWindow) &&
                  members_mode() != MEMBERS_NONE && !members_hidden;
  const frame_chars_t *fc = frame_chars();
  gboolean has_format = msgfmt_ui_has("members");
  GSList *elt, *res, *p;
  GPtrArray *list;
  gpointer bud;
  const char *mynick;
  rect_t r;
  int x0, cw, rows;
  guint i;

  if (show != members_shown) {
    members_shown = show;
    place_chatstatus();
    if (Layout.borders)
      draw_frames();
    scr_update_chat_status(FALSE);
  }
  if (!show) {
    hide_panel(membersPanel);
    return;
  }

  elt = roster_find(currentWindow->jid, jidsearch, ROSTER_TYPE_ROOM);
  bud = elt->data;
  mynick = buddy_getnickname(bud);
  list = g_ptr_array_new_with_free_func(member_free);
  res = buddy_getresources(bud);
  for (p = res; p; p = g_slist_next(p)) {
    member_t *m = g_new0(member_t, 1);
    m->name = p->data;
    m->role = buddy_getrole(bud, m->name);
    m->affil = buddy_getaffil(bud, m->name);
    m->status = buddy_getstatus(bud, m->name);
    m->self = mynick && !strcmp(mynick, m->name);
    g_ptr_array_add(list, m);
  }
  g_slist_free(res);
  g_ptr_array_sort(list, member_cmp);

  if (members_mode() == MEMBERS_COLUMN) {
    r = Layout.members;
  } else {
    rect_t c = chat_rect(TRUE);
    r.w = MIN(members_width(), MAX(c.w / 2, 1));
    r.h = MIN((int)list->len + 1, c.h);
    r.y = c.y;
    r.x = c.x + c.w - r.w;
  }
  MOVEWIN(membersWnd, r);
  replace_panel(membersPanel, membersWnd);
  werase(membersWnd);
  wbkgd(membersWnd, get_color(COLOR_ROSTER));

  // Separators
  x0 = 0;
  cw = r.w;
  rows = r.h;
  wattrset(membersWnd, named_style_attr("border", COLOR_GENERAL));
  if (members_mode() == MEMBERS_COLUMN) {
    if (!Layout.borders) {
      int sx = (Layout.members.x < Layout.chat.x) ? r.w - 1 : 0;
      for (i = 0; (int)i < r.h; i++)
        mvwaddch(membersWnd, i, sx, ACS_VLINE);
      x0 = sx ? 0 : 1;
      cw--;
    }
  } else {
    // Overlay: line on the left and at the bottom
    for (i = 0; (int)i < r.h - 1; i++) {
      if (fc)
        mvwaddstr(membersWnd, i, 0, fc->v);
      else
        mvwaddch(membersWnd, i, 0, ACS_VLINE);
    }
    wmove(membersWnd, r.h - 1, 0);
    if (fc) {
      waddstr(membersWnd, fc->bl);
      for (i = 1; (int)i < r.w; i++)
        waddstr(membersWnd, fc->h);
    } else {
      waddch(membersWnd, ACS_LLCORNER);
      for (i = 1; (int)i < r.w; i++)
        waddch(membersWnd, ACS_HLINE);
    }
    x0 = 1;
    cw--;
    rows--;
  }
  wattrset(membersWnd, get_color(COLOR_ROSTER));

  for (i = 0; i < list->len && (int)i < rows; i++) {
    member_t *m = g_ptr_array_index(list, i);
    sstr_t *line = sstr_new();
    int base = get_color(COLOR_ROSTER);

    wmove(membersWnd, i, x0);
    if ((int)i == rows - 1 && list->len > (guint)rows) {
      // Not enough room
      gchar *more = g_strdup_printf(" … +%u", list->len - rows + 1);
      wattrset(membersWnd, named_style_attr("dim", COLOR_ROSTER));
      waddnstr(membersWnd, more, -1);
      g_free(more);
      sstr_free(line);
      break;
    }
    if (has_format &&
        msgfmt_ui("members", NULL, member_fields, member_field, m, cw, line)) {
      draw_sstr(membersWnd, line, base, FALSE, cw);
    } else {
      // " @nick", the nickname colored like in the chat window
      int attr = base;
      gchar *t = g_strdup_printf(" %c", role_char(m->role));
      sstr_append(line, t, -1, NULL);
      g_free(t);
      draw_sstr(membersWnd, line, base, TRUE, cw);
      sstr_clear(line);
      if (m->self)
        attr = named_style_attr("nick_self", COLOR_ROSTER);
      else
        nick_color(m->name, HBB_PREFIX_IN, &attr);
      sstr_append(line, m->name, -1, NULL);
      draw_sstr(membersWnd, line, attr, TRUE, cw - 2);
    }
    sstr_free(line);
  }
  wattrset(membersWnd, A_NORMAL);
  g_ptr_array_free(list, TRUE);

  show_panel(membersPanel);
  top_panel(membersPanel);
}

//  scr_members(arg)
// /members show|hide|toggle
void scr_members(const char *arg)
{
  gboolean hidden = members_hidden;

  if (!arg || !*arg || !strcmp(arg, "toggle"))
    hidden = !hidden;
  else if (!strcmp(arg, "show"))
    hidden = FALSE;
  else if (!strcmp(arg, "hide"))
    hidden = TRUE;
  else {
    scr_LogPrint(LPRINT_NORMAL, "Usage: /members [show|hide|toggle]");
    return;
  }
  if (hidden == members_hidden)
    return;
  members_hidden = hidden;
  scr_resize();
  draw_members();
  update_panels();
}

/* -- XMPP console -- */

static void console_create(void)
{
  if (consoleWindow)
    return;
  consoleWindow = g_new0(winbuf_t, 1);
  consoleWindow->win = activechatWnd;
  consoleWindow->panel = activechatPanel;
  consoleWindow->bd = g_new0(buffdata_t, 1);
}

//  scr_console_add(outgoing, data)
// Add XML data sent to or received from the server to the console.
void scr_console_add(gboolean outgoing, const char *data)
{
  gchar **parts, **p;
  buffdata_t *bd;
  guint flags = outgoing ? (HBB_PREFIX_OUT | HBB_PREFIX_HLIGHT_OUT)
                         : HBB_PREFIX_IN;
  time_t now = time(NULL);

  if (!Curses || !activechatWnd)
    return;
  console_create();
  bd = consoleWindow->bd;
  parts = xml_split_stanzas(data);
  for (p = parts; *p; p++) {
    gchar *masked = xml_mask_secrets(*p);
    gchar *locale = from_utf8(masked);
    // At most 64 blocks of history (unless the buffer is scrolled)
    hbuf_add_line(&bd->hbuf, locale ? locale : masked, NULL, now, flags,
                  sync_buffer_width(consoleWindow),
                  (bd->lock || bd->top) ? 0 : 64, NULL);
    g_free(locale);
    g_free(masked);
  }
  g_strfreev(parts);
  if (console_shown)
    scr_update_window(consoleWindow);
}

//  scr_console_shown()
// TRUE if the XMPP console is displayed: text typed in the input line is
// sent to the server.
gboolean scr_console_shown(void)
{
  return console_shown;
}

static void console_show(void)
{
  console_create();
  console_shown = TRUE;
  console_buddy = current_buddy;
  chatmode = TRUE;
  currentWindow = consoleWindow;
  top_panel(consoleWindow->panel);
  hide_panel(chatPanel);
  scr_update_window(consoleWindow);
  draw_members();
  scr_update_chat_status(FALSE);
  top_panel(inputPanel);
  update_panels();
}

//  scr_console(arg)
// /console [show|hide|toggle|clear]
void scr_console(const char *arg)
{
  if (!Curses)
    return;
  if (!arg || !*arg || !strcmp(arg, "toggle")) {
    arg = console_shown ? "hide" : "show";
  }
  if (!strcmp(arg, "show")) {
    console_show();
  } else if (!strcmp(arg, "hide")) {
    if (!console_shown)
      return;
    console_shown = FALSE;
    if (current_buddy)
      scr_show_buddy_window();
    else
      readline_disable_chat_mode(FALSE);
  } else if (!strcmp(arg, "clear")) {
    if (consoleWindow) {
      hbuf_free(&consoleWindow->bd->hbuf);
      consoleWindow->bd->top = NULL;
      if (console_shown)
        scr_update_window(consoleWindow);
    }
  } else {
    scr_LogPrint(LPRINT_NORMAL, "Usage: /console [show|hide|toggle|clear]");
  }
}

/* -- Roster format (mc.format.roster) -- */

typedef struct {
  gpointer bud;
  guint btype;
  char status;              // Status character
  const char *status_name;
  gunichar pending;
  guint unread;
  int count;                // Members of a folded group
  gboolean selected, folded, msg, composing, paused;
} roster_item_t;

static const char *const roster_fields[] = {
  "name", "jid", "status", "status_char", "icon", "pending", "unread",
  "count", "type", "selected", "folded", "msg", "composing", "paused",
  "sub_from", "sub_to", NULL
};

static gchar *roster_field(const char *name, const char *arg, gpointer data)
{
  roster_item_t *it = data;

  if (!strcmp(name, "name"))
    return g_strdup(buddy_getname(it->bud));
  if (!strcmp(name, "jid"))
    return g_strdup(buddy_getjid(it->bud));
  if (!strcmp(name, "status"))
    return g_strdup(it->status_name);
  if (!strcmp(name, "status_char"))
    return g_strdup_printf("%c", it->status);
  if (!strcmp(name, "icon"))
    return it->status_name ? status_icon(it->status_name, it->status) : NULL;
  if (!strcmp(name, "pending")) {
    gchar buf[8] = { 0 };
    if (it->pending == ' ')
      return NULL;
    g_unichar_to_utf8(it->pending, buf);
    return g_strdup(buf);
  }
  if (!strcmp(name, "unread"))
    return it->unread ? g_strdup_printf("%u", it->unread) : NULL;
  if (!strcmp(name, "count"))
    return it->folded ? g_strdup_printf("%d", it->count) : NULL;
  if (!strcmp(name, "type")) {
    if (it->btype & ROSTER_TYPE_GROUP)   return g_strdup("group");
    if (it->btype & ROSTER_TYPE_ROOM)    return g_strdup("room");
    if (it->btype & ROSTER_TYPE_AGENT)   return g_strdup("agent");
    if (it->btype & ROSTER_TYPE_SPECIAL) return g_strdup("special");
    return g_strdup("user");
  }
  if (!strcmp(name, "sub_from") || !strcmp(name, "sub_to")) {
    guint sub;
    if (!(it->btype & ROSTER_TYPE_USER))
      return NULL;
    sub = buddy_getsubscription(it->bud);
    return (sub & (name[4] == 'f' ? sub_from : sub_to)) ? g_strdup("1") : NULL;
  }
  if (!strcmp(name, "selected"))  return it->selected ? g_strdup("1") : NULL;
  if (!strcmp(name, "folded"))    return it->folded ? g_strdup("1") : NULL;
  if (!strcmp(name, "msg"))       return it->msg ? g_strdup("1") : NULL;
  if (!strcmp(name, "composing")) return it->composing ? g_strdup("1") : NULL;
  if (!strcmp(name, "paused"))    return it->paused ? g_strdup("1") : NULL;
  return NULL;
}

//  scr_draw_roster()
// Display the buddylist (not really the roster) on the screen
void scr_draw_roster(void)
{
  static int offset = 0;
  char *name, *rline, *unread;
  int maxx, maxy;
  GList *buddy;
  int i, n;
  int rOffset;
  int cursor_backup;
  guint status, pending;
  enum imstatus currentstatus = xmpp_getstatus();
  int x_pos;
  int prefix_length;
  char space[2] = " ";
  gboolean roster_format = msgfmt_ui_has("roster");

  // We can reset update_roster
  if (_update_roster == FALSE)
    return;
  _update_roster = FALSE;

  buddylist_build();

  getmaxyx(rosterWnd, maxy, maxx);
  if (!Layout.borders)
    maxx--;  // Last char is for vertical border

  cursor_backup = curs_set(0);

  if (!buddylist)
    offset = 0;
  else
    scr_update_chat_status(FALSE);

  // Cleanup of roster window
  wbkgdset(rosterWnd, get_color(COLOR_GENERAL)); // clear background color
  werase(rosterWnd);

  if (Roster_Width && !Layout.borders) {
    int line_x_pos = roster_win_on_right ? 0 : Roster_Width-1;
    // Redraw the vertical line (not very good...)
    for (i=0 ; i < Layout.roster.h ; i++)
      mvwaddch(rosterWnd, i, line_x_pos, ACS_VLINE);
  }

  // Leave now if buddylist is empty or the roster is hidden
  if (!buddylist || !Roster_Width) {
    draw_members();
    update_panels();
    curs_set(cursor_backup);
    return;
  }

  // Update offset if necessary
  // a) Try to show as many buddylist items as possible
  i = g_list_length(buddylist) - maxy;
  if (i < 0)
    i = 0;
  if (i < offset)
    offset = i;
  // b) Make sure the current_buddy is visible
  i = g_list_position(buddylist, current_buddy);
  if (i == -1) { // This is bad
    scr_LogPrint(LPRINT_NORMAL, "Doh! Can't find current selected buddy!!");
    curs_set(cursor_backup);
    return;
  } else if (i < offset) {
    offset = i;
  } else if (i+1 > offset + maxy) {
    offset = i + 1 - maxy;
  }

  if (roster_win_on_right && !Layout.borders)
    x_pos = 1; // 1 char offset (vertical line)
  else
    x_pos = 0;

  if (settings_opt_get_int("roster_no_leading_space") == 1) {
    space[0] = '\0';
    prefix_length = 6;
  } else {
    prefix_length = 7;
  }

  name = g_new0(char, 4*Roster_Width);
  unread = g_new0(char, Roster_Width+1);;
  rline = g_new0(char, 4*Roster_Width+1);

  buddy = buddylist;
  rOffset = offset;

  for (i=0; i<maxy && buddy; buddy = g_list_next(buddy)) {
    unsigned short bflags, btype;
    unsigned short ismsg, isgrp, ismuc, ishid, isspe;
    guint isurg;
    gchar *rline_locale;

    bflags = buddy_getflags(BUDDATA(buddy));
    btype = buddy_gettype(BUDDATA(buddy));

    ismsg = bflags & ROSTER_FLAG_MSG;
    ishid = bflags & ROSTER_FLAG_HIDE;
    isgrp = btype  & ROSTER_TYPE_GROUP;
    ismuc = btype  & ROSTER_TYPE_ROOM;
    isspe = btype  & ROSTER_TYPE_SPECIAL;
    isurg = buddy_getuiprio(BUDDATA(buddy));

    if (rOffset > 0) {
      rOffset--;
      continue;
    }

    status = '?';
    pending = ' ';

    if (!ismuc) {
      // There is currently no chat state support for MUC
      GSList *resources = buddy_getresources(BUDDATA(buddy));
      GSList *p_res;

      for (p_res = resources ; p_res ; p_res = g_slist_next(p_res)) {
        guint events = buddy_resource_getevents(BUDDATA(buddy),
                                                p_res ? p_res->data : "");
        if ((events & ROSTER_EVENT_PAUSED) && pending != '+')
          pending = '.';
        if (events & ROSTER_EVENT_COMPOSING)
          pending = '+';
        g_free(p_res->data);
      }
      g_slist_free(resources);
    }

    // Display message notice if there is a message flag, but not
    // for unfolded groups.
    if (ismsg && (!isgrp || ishid)) {
      pending = '#';
    }

    if (ismuc) {
      if (buddy_getinsideroom(BUDDATA(buddy)))
        status = 'C';
      else
        status = 'x';
    } else if (currentstatus != offline) {
      enum imstatus budstate;
      budstate = buddy_getstatus(BUDDATA(buddy), NULL);
      if (budstate < imstatus_size)
        status = imstatus2char[budstate];
    }
    if (buddy == current_buddy) {
      if (pending == '#')
        wbkgdset(rosterWnd, get_color(COLOR_ROSTERSELNMSG));
      else
        wbkgdset(rosterWnd, get_color(COLOR_ROSTERSEL));
      // The 3 following lines aim at coloring the whole line
      wmove(rosterWnd, i, x_pos);
      for (n = 0; n < maxx; n++)
        waddch(rosterWnd, ' ');
    } else {
      if (pending == '#')
        wbkgdset(rosterWnd, get_color(COLOR_ROSTERNMSG));
      else {
        int color = get_color(COLOR_ROSTER);
        if ((!isspe) && (!isgrp)) { // Look for color rules
          GSList *head;
          const char *bjid = buddy_getjid(BUDDATA(buddy));
          for (head = rostercolrules; head; head = g_slist_next(head)) {
            rostercolor_t *rc = head->data;
            if (g_pattern_match_string(rc->compiled, bjid) &&
                (!strcmp("*", rc->status) || strchr(rc->status, status))) {
              color = compose_color(rc->color);
              break;
            }
          }
        }
        wbkgdset(rosterWnd, color);
      }
    }

    name[0] = 0;
    unread[0] = 0;
    if (Roster_Width > prefix_length) {
      g_utf8_strncpy(name, buddy_getname(BUDDATA(buddy)), Roster_Width-prefix_length);
      if (settings_opt_get_int("roster_show_unread_count")) {
        guint unread_count = buddy_getunread(BUDDATA(buddy));
        glong name_length = g_utf8_strlen(name, 4*Roster_Width);
        if (unread_count > 0 && Roster_Width > prefix_length + name_length) {
          snprintf(unread, Roster_Width-(prefix_length+name_length)+1, " (%u)", unread_count);
        }
      }
    }

    if (pending == '#') {
      // Attention sign?
      if ((ismuc && isurg >= ui_attn_sign_prio_level_muc) ||
          (!ismuc && isurg >= ui_attn_sign_prio_level))
        pending = attention_sign();
    }

    if (roster_format) {
      roster_item_t it;
      sstr_t *line = sstr_new();
      const char *kind = isgrp ? "group" : isspe ? "special" :
                         ismuc ? "room" : "contact";
      gboolean done;

      memset(&it, 0, sizeof(it));
      it.bud = BUDDATA(buddy);
      it.btype = btype;
      it.status = status;
      if (ismuc) {
        it.status_name = (status == 'C') ? "room" : "room_out";
      } else if (!isgrp && !isspe) {
        enum imstatus st = buddy_getstatus(BUDDATA(buddy), NULL);
        it.status_name = (currentstatus == offline) ? "unknown" :
                         mlua_status_name(st);
      }
      it.pending = pending;
      it.unread = buddy_getunread(BUDDATA(buddy));
      it.selected = (buddy == current_buddy);
      it.folded = isgrp && ishid;
      it.msg = ismsg;
      it.composing = (pending == '+');
      it.paused = (pending == '.');
      if (it.folded)
        foreach_group_member(BUDDATA(buddy), increment_if_buddy_not_filtered,
                             &it.count);

      done = msgfmt_ui("roster", kind, roster_fields, roster_field, &it,
                       maxx, line);
      if (done) {
        int base = getbkgd(rosterWnd) & A_ATTRIBUTES;
        wmove(rosterWnd, i, x_pos);
        draw_sstr(rosterWnd, line, base, it.selected, maxx);
        sstr_free(line);
        i++;
        continue;
      }
      sstr_free(line);
    }

    if (isgrp) {
      if (ishid) {
        int group_count = 0;
        foreach_group_member(BUDDATA(buddy), increment_if_buddy_not_filtered,
                             &group_count);
        snprintf(rline, 4*Roster_Width, "%s%lc+++ %s (%i)", space, pending,
                 name, group_count);
        /* Do not display the item count if there isn't enough space */
        if (g_utf8_strlen(rline, 4*Roster_Width) >= Roster_Width)
          snprintf(rline, 4*Roster_Width, "%s%lc+++ %s", space, pending, name);
      }
      else
        snprintf(rline, 4*Roster_Width, "%s%lc--- %s", space, pending, name);
    } else if (isspe) {
      snprintf(rline, 4*Roster_Width, "%s%lc%s", space, pending, name);
    } else {
      char sepleft  = '[';
      char sepright = ']';
      if (btype & ROSTER_TYPE_USER) {
        guint subtype = buddy_getsubscription(BUDDATA(buddy));
        if (status == '_' && !(subtype & sub_to))
          status = '?';
        if (!(subtype & sub_from)) {
          sepleft  = '{';
          sepright = '}';
        }
      }
      snprintf(rline, 4*Roster_Width, "%s%lc%c%c%c %s%s",
               space, pending, sepleft, status, sepright, name, unread);
    }

    rline_locale = from_utf8(rline);
    mvwprintw(rosterWnd, i, x_pos, "%s", rline_locale);
    g_free(rline_locale);
    i++;
  }

  g_free(rline);
  g_free(unread);
  g_free(name);
  draw_members();
  top_panel(inputPanel);
  update_panels();
  curs_set(cursor_backup);
}

void scr_update_roster(void)
{
  _update_roster = TRUE;
}


//  scr_roster_visibility(status)
// Set the roster visibility:
// status=1   Show roster
// status=0   Hide roster
// status=-1  Toggle roster status
void scr_roster_visibility(int status)
{
  int old_roster_status = roster_hidden;

  if (status > 0)
    roster_hidden = FALSE;
  else if (status == 0)
    roster_hidden = TRUE;
  else
    roster_hidden = !roster_hidden;

  if (roster_hidden != old_roster_status) {
    // Recalculate windows size and redraw
    scr_resize();
    redrawwin(stdscr);
  }
}

static void scr_write_message(const char *bjid, const char *text,
                              const char *nick, time_t timestamp,
                              guint prefix_flags, gpointer xep184)
{
  char *xtext;

  if (!timestamp)
    timestamp = time(NULL);
  else
    prefix_flags |= HBB_PREFIX_DELAYED;

  xtext = ut_expand_tabs(text); // Expand tabs and filter out some chars

  scr_write_in_window(bjid, xtext, nick, timestamp, prefix_flags, FALSE,
                      xep184);

  if (xtext != (char*)text)
    g_free(xtext);
}

//  scr_write_incoming_message_nick(jidfrom, text, nick, timestamp, prefix)
// Write an incoming message.  nick is the nickname of the sender, for room
// messages (HBB_PREFIX_MUC) and actions (HBB_PREFIX_ME).
// If prefix is NULL, HBB_PREFIX_IN is supposed.
void scr_write_incoming_message_nick(const char *jidfrom, const char *text,
                                     const char *nick, time_t timestamp,
                                     guint prefix)
{
  if (!(prefix &
        ~HBB_PREFIX_NOFLAG & ~HBB_PREFIX_HLIGHT & ~HBB_PREFIX_HLIGHT_OUT &
        ~HBB_PREFIX_PGPCRYPT & ~HBB_PREFIX_OTRCRYPT &
        ~HBB_PREFIX_OMEMOCRYPT & ~HBB_PREFIX_CARBON &
        ~HBB_PREFIX_ME & ~HBB_PREFIX_MUC & ~HBB_PREFIX_PRIVATE))
    prefix |= HBB_PREFIX_IN;

  scr_write_message(jidfrom, text, nick, timestamp, prefix, NULL);
}

// For compatibility: if mucnicklen is not 0, the text begins with
// "<nick> " (or "*nick " for an action).
void scr_write_incoming_message(const char *jidfrom, const char *text,
                                time_t timestamp,
                                guint prefix, unsigned mucnicklen)
{
  gchar *nick = NULL;

  if (mucnicklen > 2 && strlen(text) >= mucnicklen) {
    if (text[0] == '<' && text[mucnicklen-1] == '>') {
      nick = g_strndup(text + 1, mucnicklen - 2);
      prefix |= HBB_PREFIX_MUC;
    } else if (text[0] == '*' && text[mucnicklen-1] == ' ') {
      nick = g_strndup(text + 1, mucnicklen - 2);
      prefix |= HBB_PREFIX_MUC | HBB_PREFIX_ME;
    }
    if (nick) {
      text += mucnicklen;
      if (*text == ' ')
        text++;
    }
  }
  scr_write_incoming_message_nick(jidfrom, text, nick, timestamp, prefix);
  g_free(nick);
}

void scr_write_outgoing_message(const char *jidto, const char *text,
                                guint prefix, gpointer xep184)
{
  scr_write_outgoing_message_nick(jidto, text, NULL, prefix, xep184);
}

//  scr_write_outgoing_message_nick(jidto, text, nick, prefix, xep184)
// nick is our nickname for actions (HBB_PREFIX_ME), or the recipient for
// private messages in a room (HBB_PREFIX_PRIVATE).
void scr_write_outgoing_message_nick(const char *jidto, const char *text,
                                     const char *nick, guint prefix,
                                     gpointer xep184)
{
  GSList *roster_elt;
  roster_elt = roster_find(jidto, jidsearch,
                           ROSTER_TYPE_USER|ROSTER_TYPE_AGENT|ROSTER_TYPE_ROOM);

  scr_write_message(jidto, text, nick,
                    0, prefix|HBB_PREFIX_OUT|HBB_PREFIX_HLIGHT_OUT, xep184);

  // Show jidto's buffer unless the buddy is not in the buddylist
  if (roster_elt && g_list_position(buddylist, roster_elt->data) != -1)
    scr_show_window(jidto, FALSE);
}

void scr_remove_receipt_flag(const char *bjid, gconstpointer xep184)
{
  winbuf_t *win_entry = scr_search_window(bjid, FALSE);
  if (win_entry && xep184) {
    hbuf_remove_receipt(win_entry->bd->hbuf, xep184);
    if (chatmode && (buddy_search_jid(bjid) == current_buddy))
      scr_update_buddy_window();
  }
}

static inline void set_autoaway(bool setaway)
{
  static enum imstatus oldstatus;
  static char *oldmsg;
  Autoaway = setaway;

  if (setaway) {
    const char *msg, *prevmsg;
    oldstatus = xmpp_getstatus();
    if (oldmsg) {
      g_free(oldmsg);
      oldmsg = NULL;
    }
    prevmsg = xmpp_getstatusmsg();
    msg = settings_opt_get("message_autoaway");
    if (!msg)
      msg = prevmsg;
    if (prevmsg)
      oldmsg = g_strdup(prevmsg);
    xmpp_setstatus(away, NULL, msg, FALSE);
  } else {
    // Back
    xmpp_setstatus(oldstatus, NULL, (oldmsg ? oldmsg : ""), FALSE);
    if (oldmsg) {
      g_free(oldmsg);
      oldmsg = NULL;
    }
  }
}

//  set_chatstate(state)
// Set the current chat state (0=active, 1=composing, 2=paused)
// If the chat state has changed, call xmpp_send_chatstate()
static void set_chatstate(int state)
{
#ifdef XEP0085
  if (chatstates_disabled)
    return;
  if (!chatmode)
    state = 0;
  if (state != chatstate) {
    chatstate = state;
    if (current_buddy &&
        buddy_gettype(BUDDATA(current_buddy)) == ROSTER_TYPE_USER) {
      guint jep_state;
      if (chatstate == 1) {
        if (chatstate_timeout_id == 0)
          chatstate_timeout_id = g_timeout_add_seconds(1,
                                                       scr_chatstates_timeout,
                                                       NULL);
        jep_state = ROSTER_EVENT_COMPOSING;
      }
      else if (chatstate == 2)
        jep_state = ROSTER_EVENT_PAUSED;
      else
        jep_state = ROSTER_EVENT_ACTIVE;
      xmpp_send_chatstate(BUDDATA(current_buddy), jep_state);
    }
    if (!chatstate)
      chatstate_timestamp = 0;
  }
#endif
}

#ifdef XEP0085
static gboolean scr_chatstates_timeout(gpointer data)
{
  time_t now;
  time(&now);
  // Check if we're currently composing...
  if (chatstate != 1 || !chatstate_timestamp) {
    chatstate_timeout_id = 0;
    return FALSE;
  }

  // If the timeout is reached, let's change the state right now.
  if (now >= chatstate_timestamp + COMPOSING_TIMEOUT) {
    chatstate_timestamp = now;
    set_chatstate(2);
    chatstate_timeout_id = 0;
    return FALSE;
  }
  return TRUE;
}
#endif

static gboolean scr_autoaway_timeout_callback(gpointer data)
{
  enum imstatus cur_st = xmpp_getstatus();
  if (cur_st != available && cur_st != freeforchat)
    // Some non-user-originated status changes, let's wait more.
    // Maybe the proper fix for that will be set global variable
    // "autoaway_delayed" and check that variable in postconnect
    // hook (afaik, only source for such status changes are
    // error disconnects).
    return TRUE;
  set_autoaway(TRUE);
  // source will be destroyed after return
  autoaway_source = 0;
  return FALSE;
}

static void scr_reinstall_autoaway_timeout(void)
{
  unsigned int autoaway_timeout = settings_opt_get_int("autoaway");
  enum imstatus cur_st = xmpp_getstatus();
  if (autoaway_source) {
    g_source_remove(autoaway_source);
    autoaway_source = 0;
  }
  if (autoaway_timeout && (cur_st == available || cur_st == freeforchat))
    autoaway_source = g_timeout_add_seconds(autoaway_timeout,
                                            scr_autoaway_timeout_callback,
                                            NULL);
}

// Check if we should reset autoaway timeout source
void scr_check_auto_away(int activity)
{
  if (Autoaway && activity) {
    scr_reinstall_autoaway_timeout();
    set_autoaway(FALSE);
  } else if (activity || !autoaway_source)
    scr_reinstall_autoaway_timeout();
}

//  set_current_buddy(newbuddy)
// Set the current_buddy to newbuddy (if not NULL)
// Lock the newbuddy, and unlock the previous current_buddy
static void set_current_buddy(GList *newbuddy)
{
  enum imstatus prev_st = imstatus_size;
  /* prev_st initialized to imstatus_size, which is used as "undef" value.
   * We are sure prev_st will get a different status value after the
   * buddy_getstatus() call.
   */

  if (!current_buddy || !newbuddy)  return;
  if (newbuddy == current_buddy)    return;

  // We're moving to another buddy.  We're thus inactive wrt current_buddy.
  set_chatstate(0);
  // We don't want the chatstate to be changed again right now.
  lock_chatstate = TRUE;

  prev_st = buddy_getstatus(BUDDATA(current_buddy), NULL);
  buddy_setflags(BUDDATA(current_buddy), ROSTER_FLAG_LOCK, FALSE);
  if (chatmode) {
    scr_buffer_readmark(TRUE);
    alternate_buddy = current_buddy;
  }
  current_buddy = newbuddy;
  // Lock the buddy in the buddylist if we're in chat mode
  if (chatmode) {
    buddy_setflags(BUDDATA(current_buddy), ROSTER_FLAG_LOCK, TRUE);
    // Remove the readmark if it is at the end of the buffer
    scr_buffer_readmark(-1);
  }
  // We should rebuild the buddylist when the last selected buddy isn't
  // displayed anymore
  if (!(buddylist_get_filter() & 1<<prev_st))
    buddylist_defer_build();
  scr_update_roster();
}

//  scr_roster_top()
// Go to the first buddy in the buddylist
void scr_roster_top(void)
{
  set_current_buddy(buddylist);
  if (chatmode) {
    last_activity_buddy = current_buddy;
    scr_show_buddy_window();
  }
}

//  scr_roster_bottom()
// Go to the last buddy in the buddylist
void scr_roster_bottom(void)
{
  set_current_buddy(g_list_last(buddylist));
  if (chatmode) {
    last_activity_buddy = current_buddy;
    scr_show_buddy_window();
  }
}

//  scr_roster_up_down(updown, n)
// Go to the nth next buddy in the buddylist
// (up if updown == -1, down if updown == 1)
void scr_roster_up_down(int updown, unsigned int n)
{
  unsigned int i;
  GList *new_buddy = current_buddy;
  GList *tmp_buddy;

  if (!current_buddy)
    return;

  for (i = 0; i < n; i++) {
    if (updown < 0)
      tmp_buddy = g_list_previous(new_buddy);
    else
      tmp_buddy = g_list_next(new_buddy);
    if (tmp_buddy)
      new_buddy = tmp_buddy;
  }
  if (new_buddy == current_buddy)
    return;

  set_current_buddy(new_buddy);
  if (chatmode) {
    last_activity_buddy = current_buddy;
    scr_show_buddy_window();
  }
}

//  scr_roster_prev_group()
// Go to the previous group in the buddylist
void scr_roster_prev_group(void)
{
  GList *bud;

  for (bud = current_buddy ; bud ; ) {
    bud = g_list_previous(bud);
    if (!bud)
      break;
    if (buddy_gettype(BUDDATA(bud)) & ROSTER_TYPE_GROUP) {
      set_current_buddy(bud);
      if (chatmode) {
        last_activity_buddy = current_buddy;
        scr_show_buddy_window();
      }
      break;
    }
  }
}

//  scr_roster_next_group()
// Go to the next group in the buddylist
void scr_roster_next_group(void)
{
  GList *bud;

  for (bud = current_buddy ; bud ; ) {
    bud = g_list_next(bud);
    if (!bud)
      break;
    if (buddy_gettype(BUDDATA(bud)) & ROSTER_TYPE_GROUP) {
      set_current_buddy(bud);
      if (chatmode) {
        last_activity_buddy = current_buddy;
        scr_show_buddy_window();
      }
      break;
    }
  }
}

//  scr_roster_search(str)
// Look forward for a buddy with jid/name containing str.
void scr_roster_search(char *str)
{
  set_current_buddy(buddy_search(str));
  if (chatmode) {
    last_activity_buddy = current_buddy;
    scr_show_buddy_window();
  }
}

//  scr_roster_jump_jid(bjid)
// Jump to buddy bjid.
// NOTE: With this function, the buddy is added to the roster if doesn't exist.
void scr_roster_jump_jid(char *barejid)
{
  GSList *roster_elt;
  // Look for an existing buddy
  roster_elt = roster_find(barejid, jidsearch,
                 ROSTER_TYPE_USER|ROSTER_TYPE_AGENT|ROSTER_TYPE_ROOM);
  // Create it if necessary
  if (!roster_elt)
    roster_elt = roster_add_user(barejid, NULL, NULL, ROSTER_TYPE_USER,
                                 sub_none, -1);
  // Set a lock to see it in the buddylist
  buddy_setflags(BUDDATA(roster_elt), ROSTER_FLAG_LOCK, TRUE);
  buddylist_defer_build();
  // Jump to the buddy
  set_current_buddy(buddy_search_jid(barejid));
  if (chatmode) {
    last_activity_buddy = current_buddy;
    scr_show_buddy_window();
  }
}

//  scr_roster_unread_message(next)
// Go to a new message.  If next is not null, try to go to the next new
// message.  If it is not possible or if next is NULL, go to the first new
// message from unread_list.
void scr_roster_unread_message(int next)
{
  gpointer unread_ptr;
  gpointer refbuddata;
  GList *nbuddy;

  if (!current_buddy) return;

  if (next) refbuddata = BUDDATA(current_buddy);
  else      refbuddata = NULL;

  unread_ptr = unread_msg(refbuddata);
  if (!unread_ptr) {
    if (!last_activity_buddy || g_list_position(buddylist, last_activity_buddy) == -1)
      return;
    unread_ptr = BUDDATA(last_activity_buddy);
  }

  if (!(buddy_gettype(unread_ptr) & ROSTER_TYPE_SPECIAL)) {
    gpointer ngroup;
    // If buddy is in a folded group, we need to expand it
    ngroup = buddy_getgroup(unread_ptr);
    if (buddy_getflags(ngroup) & ROSTER_FLAG_HIDE) {
      buddy_setflags(ngroup, ROSTER_FLAG_HIDE, FALSE);
      buddylist_defer_build();
    }
  }

  buddylist_build();
  nbuddy = g_list_find(buddylist, unread_ptr);
  if (nbuddy) {
    set_current_buddy(nbuddy);
    if (chatmode) scr_show_buddy_window();
  } else
    scr_LogPrint(LPRINT_LOGNORM, "Error: nbuddy == NULL"); // should not happen
}

//  scr_roster_next_open_buffer()
// Jump to the next open buffer (experimental XXX)
// This implementation ignores the hidden entries (folded groups).
void scr_roster_next_open_buffer(void)
{
  GList *bud = current_buddy;

  if (!current_buddy) return;

  for (;;) {
    guint budtype;
    bud = g_list_next(bud);
    // End of list: jump to the first entry
    if (!bud)
      bud = buddylist;
    // Check if we're back to the initial position
    if (bud == current_buddy)
      break;
    // Ignore the special buffer(s), groups
    budtype = buddy_gettype(BUDDATA(bud));
    if (budtype & (ROSTER_TYPE_GROUP | ROSTER_TYPE_SPECIAL))
      continue;

    // Check if a buffer/window exists
    if (scr_search_window(buddy_getjid(BUDDATA(bud)), 0)) {
      set_current_buddy(bud);
      if (chatmode) {
        last_activity_buddy = current_buddy;
        scr_show_buddy_window();
      }
      break;
    }
  }
}

//  scr_roster_jump_alternate()
// Try to jump to alternate (== previous) buddy
void scr_roster_jump_alternate(void)
{
  if (!alternate_buddy || g_list_position(buddylist, alternate_buddy) == -1)
    return;
  set_current_buddy(alternate_buddy);
  if (chatmode) {
    last_activity_buddy = current_buddy;
    scr_show_buddy_window();
  }
}

//  scr_roster_display(filter)
// Set the roster filter mask.  If filter is null/empty, the current
// mask is displayed.
void scr_roster_display(const char *filter)
{
  guchar status;
  enum imstatus budstate;
  char strfilter[imstatus_size+1];
  char *psfilter;

  if (filter && *filter) {
    int show_all = (*filter == '*');
    status = 0;
    for (budstate = 0; budstate < imstatus_size-1; budstate++)
      if (strchr(filter, imstatus2char[budstate]) || show_all)
        status |= 1<<budstate;
    buddylist_set_filter(status);
    buddylist_defer_build();
    scr_update_roster();
    return;
  }

  // Display current filter
  psfilter = strfilter;
  status = buddylist_get_filter();
  for (budstate = 0; budstate < imstatus_size-1; budstate++)
    if (status & 1<<budstate)
      *psfilter++ = imstatus2char[budstate];
  *psfilter = '\0';
  scr_LogPrint(LPRINT_NORMAL, "Roster status filter: %s", strfilter);
}

//  buffer_lookup(isspe)
// The buffer of the current buddy, or the XMPP console if it is displayed.
static winbuf_t *buffer_lookup(guint isspe)
{
  if (console_shown && consoleWindow)
    return consoleWindow;
  return scr_search_window(CURRENT_JID, isspe);
}

//  scr_buffer_scroll_up_down()
// Scroll up/down the current buddy window,
// - half a screen if nblines is 0,
// - up if updown == -1, down if updown == 1
void scr_buffer_scroll_up_down(int updown, unsigned int nblines)
{
  winbuf_t *win_entry;
  int n, nbl;
  GList *hbuf_top;
  guint isspe;

  // Get win_entry
  if (!current_buddy) return;

  isspe = buddy_gettype(BUDDATA(current_buddy)) & ROSTER_TYPE_SPECIAL;
  win_entry = buffer_lookup(isspe);
  if (!win_entry) return;

  if (!nblines) {
    // Scroll half a screen (or less)
    nbl = CHAT_WIN_HEIGHT/2;
  } else {
    nbl = nblines;
  }
  hbuf_top = win_entry->bd->top;

  if (updown == -1) {   // UP
    n = 0;
    if (!hbuf_top) {
      hbuf_top = g_list_last(win_entry->bd->hbuf);
      if (!win_entry->bd->cleared) {
        if (!nblines) nbl = nbl*3 - 1;
        else nbl += CHAT_WIN_HEIGHT - 1;
      } else {
        win_entry->bd->cleared = FALSE;
        n++; // We'll scroll one line less
      }
    }
    for ( ; hbuf_top && n < nbl && g_list_previous(hbuf_top) ; n++)
      hbuf_top = g_list_previous(hbuf_top);
    win_entry->bd->top = hbuf_top;
  } else {              // DOWN
    for (n=0 ; hbuf_top && n < nbl ; n++)
      hbuf_top = g_list_next(hbuf_top);
    win_entry->bd->top = hbuf_top;
    // Check if we are at the bottom
    for (n=0 ; hbuf_top && n < CHAT_WIN_HEIGHT-1 ; n++)
      hbuf_top = g_list_next(hbuf_top);
    if (!hbuf_top)
      win_entry->bd->top = NULL; // End reached
  }

  // Refresh the window
  scr_update_window(win_entry);

  // Finished :)
  update_panels();
}

//  scr_buffer_clear()
// Clear the current buddy window (used for the /clear command)
void scr_buffer_clear(void)
{
  winbuf_t *win_entry;
  guint isspe;

  // Get win_entry
  if (!current_buddy) return;
  isspe = buddy_gettype(BUDDATA(current_buddy)) & ROSTER_TYPE_SPECIAL;
  win_entry = buffer_lookup(isspe);
  if (!win_entry) return;

  win_entry->bd->cleared = TRUE;
  win_entry->bd->top = NULL;

  // Refresh the window
  scr_update_window(win_entry);

  // Finished :)
  update_panels();
}

//  buffer_purge()
// key: winId/jid
// value: winbuf_t structure
// data: int, set to 1 if the buffer should be closed.
// NOTE: does not work for special buffers.
// Returns TRUE IFF the win_entry can be closed and freed.
static gboolean buffer_purge(gpointer key, gpointer value, gpointer data)
{
  int *p_closebuf = data;
  winbuf_t *win_entry = value;
  gboolean retval = FALSE;

  // Delete the current hbuf
  // unless we close the buffer *and* this is a shared bd structure
  if (!(*p_closebuf && win_entry->bd->refcount))
    hbuf_free(&win_entry->bd->hbuf);

  if (*p_closebuf) {
    GSList *roster_elt;
    retval = TRUE;
    roster_elt = roster_find(key, jidsearch,
        ROSTER_TYPE_USER|ROSTER_TYPE_AGENT);
    if (roster_elt)
      buddy_setactiveresource(roster_elt->data, NULL);
    if (win_entry->bd->refcount) {
      win_entry->bd->refcount--;
    } else {
      g_free(win_entry->bd);
      win_entry->bd = NULL;
    }
  } else {
    win_entry->bd->cleared = FALSE;
    win_entry->bd->top = NULL;
  }
  return retval;
}

//  scr_buffer_purge(closebuf, jid)
// Purge/Drop the current buddy buffer or jid's buffer if jid != NULL.
// If closebuf is 1, close the buffer.
void scr_buffer_purge(int closebuf, const char *jid)
{
  winbuf_t *win_entry;
  guint isspe;
  const char *cjid;
  char *ljid = NULL;
  guint hold_chatmode = FALSE;

  if (jid) {
    isspe = FALSE;
    ljid = g_strdup(jid);
    mc_strtolower(ljid);
    cjid = ljid;
    // If closebuf is TRUE, it's probably better not to leave chat mode
    // if the change isn't related to the current buffer.
    if (closebuf && current_buddy) {
      if (buddy_gettype(BUDDATA(current_buddy)) & ROSTER_TYPE_SPECIAL ||
          strcasecmp(jid, CURRENT_JID))
        hold_chatmode = TRUE;
    }
  } else {
    // Get win_entry
    if (!current_buddy) return;
    cjid = CURRENT_JID;
    isspe = buddy_gettype(BUDDATA(current_buddy)) & ROSTER_TYPE_SPECIAL;
  }
  win_entry = scr_search_window(cjid, isspe);
  if (!win_entry) {
    g_free(ljid);
    return;
  }

  if (!isspe) {
    if (buffer_purge((gpointer)cjid, win_entry, &closebuf))
      g_hash_table_remove(winbufhash, cjid);
    roster_msg_setflag(cjid, FALSE, FALSE);
    if (closebuf && !hold_chatmode) {
      scr_set_chatmode(FALSE);
      currentWindow = NULL;
    }
  } else {
    // (Special buffer)
    // Reset the current hbuf
    hbuf_free(&win_entry->bd->hbuf);
    // Currently it can only be the status buffer
    statushbuf = NULL;
    roster_msg_setflag(SPECIAL_BUFFER_STATUS_ID, TRUE, FALSE);

    win_entry->bd->cleared = FALSE;
    win_entry->bd->top = NULL;
  }

  scr_update_roster();

  // Refresh the window
  scr_update_buddy_window();

  // Finished :)
  update_panels();

  g_free(ljid);
}

//  scr_buffer_purge_all(closebuf)
// Purge all existing buffers.
// If closebuf is 1, the buffers are closed.
void scr_buffer_purge_all(int closebuf)
{
  g_hash_table_foreach_remove(winbufhash, buffer_purge, &closebuf);

  if (closebuf) {
    scr_set_chatmode(FALSE);
    currentWindow = NULL;
  }

  // Refresh the window
  scr_update_buddy_window();

  // Finished :)
  update_panels();
}

//  scr_buffer_scroll_lock(lock)
// Lock/unlock the current buddy buffer
// lock = 1 : lock
// lock = 0 : unlock
// lock = -1: toggle lock status
void scr_buffer_scroll_lock(int lock)
{
  winbuf_t *win_entry;
  guint isspe;

  // Get win_entry
  if (!current_buddy) return;
  isspe = buddy_gettype(BUDDATA(current_buddy)) & ROSTER_TYPE_SPECIAL;
  win_entry = buffer_lookup(isspe);
  if (!win_entry) return;

  if (lock == -1)
    lock = !win_entry->bd->lock;

  if (lock) {
    win_entry->bd->lock = TRUE;
  } else {
    win_entry->bd->lock = FALSE;
    if (isspe || (buddy_getflags(BUDDATA(current_buddy)) & ROSTER_FLAG_MSG))
      win_entry->bd->top = NULL;
  }

  // If chatmode is disabled and we're at the bottom of the buffer,
  // we need to set the "top" line, so we need to call scr_show_buddy_window()
  // at least once.  (Maybe it will cause a double refresh...)
  if (!chatmode && !win_entry->bd->top) {
    chatmode = TRUE;
    scr_show_buddy_window();
    chatmode = FALSE;
  }

  // Refresh the window
  scr_update_buddy_window();

  // Finished :)
  update_panels();
}

//  scr_buffer_readmark(action)
// Update the readmark flag for the current buffer
// If action = 1, set the readmark flag on the last message
// If action = 0, reset the readmark flag
// If action = -1, remove the readmark flag iff it is on the last line
void scr_buffer_readmark(gchar action)
{
  winbuf_t *win_entry;
  guint isspe;
  int autolock;

  // Get win_entry
  if (!current_buddy) return;
  isspe = buddy_gettype(BUDDATA(current_buddy)) & ROSTER_TYPE_SPECIAL;
  if (isspe) return; // Maybe not necessary
  win_entry = buffer_lookup(isspe);
  if (!win_entry) return;

  autolock = settings_opt_get_int("buffer_smart_scrolling");
  if (!win_entry->bd->lock || autolock) {
    if (action >= 0)
      hbuf_set_readmark(win_entry->bd->hbuf, action);
    else
      hbuf_remove_trailing_readmark(win_entry->bd->hbuf);
  }
}


//  scr_buffer_top_bottom()
// Jump to the head/tail of the current buddy window
// (top if topbottom == -1, bottom topbottom == 1)
void scr_buffer_top_bottom(int topbottom)
{
  winbuf_t *win_entry;
  guint isspe;

  // Get win_entry
  if (!current_buddy) return;
  isspe = buddy_gettype(BUDDATA(current_buddy)) & ROSTER_TYPE_SPECIAL;
  win_entry = buffer_lookup(isspe);
  if (!win_entry) return;

  win_entry->bd->cleared = FALSE;
  if (topbottom == 1)
    win_entry->bd->top = NULL;
  else
    win_entry->bd->top = g_list_first(win_entry->bd->hbuf);

  // Refresh the window
  scr_update_window(win_entry);

  // Finished :)
  update_panels();
}

//  scr_buffer_search(direction, text)
// Jump to the next line containing text
// (backward search if direction == -1, forward if topbottom == 1)
void scr_buffer_search(int direction, const char *text)
{
  winbuf_t *win_entry;
  GList *current_line, *search_res;
  guint isspe;

  // Get win_entry
  if (!current_buddy) return;
  isspe = buddy_gettype(BUDDATA(current_buddy)) & ROSTER_TYPE_SPECIAL;
  win_entry = buffer_lookup(isspe);
  if (!win_entry) return;

  if (win_entry->bd->top)
    current_line = win_entry->bd->top;
  else
    current_line = g_list_last(win_entry->bd->hbuf);

  search_res = hbuf_search(current_line, direction, text);

  if (search_res) {
    win_entry->bd->cleared = FALSE;
    win_entry->bd->top = search_res;

    // Refresh the window
    scr_update_window(win_entry);

    // Finished :)
    update_panels();
  } else
    scr_LogPrint(LPRINT_NORMAL, "Search string not found.");
}

//  scr_buffer_percent(n)
// Jump to the specified position in the buffer, in %
void scr_buffer_percent(int pc)
{
  winbuf_t *win_entry;
  GList *search_res;
  guint isspe;

  // Get win_entry
  if (!current_buddy) return;
  isspe = buddy_gettype(BUDDATA(current_buddy)) & ROSTER_TYPE_SPECIAL;
  win_entry = buffer_lookup(isspe);
  if (!win_entry) return;

  if (pc < 0 || pc > 100) {
    scr_LogPrint(LPRINT_NORMAL, "Bad %% value");
    return;
  }

  search_res = hbuf_jump_percent(win_entry->bd->hbuf, pc);

  win_entry->bd->cleared = FALSE;
  win_entry->bd->top = search_res;

  // Refresh the window
  scr_update_window(win_entry);

  // Finished :)
  update_panels();
}

//  scr_buffer_date(t)
// Jump to the first line after date t in the buffer
// t is a date in seconds since `00:00:00 1970-01-01 UTC'
void scr_buffer_date(time_t t)
{
  winbuf_t *win_entry;
  GList *search_res;
  guint isspe;

  // Get win_entry
  if (!current_buddy) return;
  isspe = buddy_gettype(BUDDATA(current_buddy)) & ROSTER_TYPE_SPECIAL;
  win_entry = buffer_lookup(isspe);
  if (!win_entry) return;

  search_res = hbuf_jump_date(win_entry->bd->hbuf, t);

  win_entry->bd->cleared = FALSE;
  win_entry->bd->top = search_res;

  if (!search_res)
    scr_log_print(LPRINT_NORMAL, "Date not found.");

  // Refresh the window
  scr_update_window(win_entry);

  // Finished :)
  update_panels();
}

//  scr_buffer_jump_readmark()
// Jump to the buffer readmark, if there's one
void scr_buffer_jump_readmark(void)
{
  winbuf_t *win_entry;
  GList *search_res;
  guint isspe;

  // Get win_entry
  if (!current_buddy) return;
  isspe = buddy_gettype(BUDDATA(current_buddy)) & ROSTER_TYPE_SPECIAL;
  if (isspe) return;
  win_entry = buffer_lookup(isspe);
  if (!win_entry) return;

  search_res = hbuf_jump_readmark(win_entry->bd->hbuf);

  if (!search_res) {
    scr_log_print(LPRINT_NORMAL, "Readmark not found.");
    return;
  }

  win_entry->bd->cleared = FALSE;
  win_entry->bd->top = search_res;

  // Refresh the window
  scr_update_window(win_entry);

  // Finished :)
  update_panels();
}

//  scr_buffer_dump(filename)
// Dump the current buffer content to the specified file.
void scr_buffer_dump(const char *file)
{
  char *extfname;

  if (!currentWindow) {
    scr_LogPrint(LPRINT_NORMAL, "No current buffer!");
    return;
  }

  if (!file || !*file) {
    scr_LogPrint(LPRINT_NORMAL, "Missing parameter (file name)!");
    return;
  }

  extfname = expand_filename(file);
  hbuf_dump_to_file(currentWindow->bd->hbuf, extfname);
  g_free(extfname);
}

//  buffer_list()
// key: winId/jid
// value: winbuf_t structure
// data: none.
static void buffer_list(gpointer key, gpointer value, gpointer data)
{
  GList *head;
  winbuf_t *win_entry = value;

  head = g_list_first(win_entry->bd->hbuf);

  scr_LogPrint(LPRINT_NORMAL, " %s  (%u/%u)", (const char *) key,
               g_list_length(head), hbuf_get_blocks_number(head));
}

void scr_buffer_list(void)
{
  scr_LogPrint(LPRINT_NORMAL, "Buffer list:");
  buffer_list("[status]", statusWindow, NULL);
  g_hash_table_foreach(winbufhash, buffer_list, NULL);
  scr_LogPrint(LPRINT_NORMAL, "End of buffer list.");
  scr_setmsgflag_if_needed(SPECIAL_BUFFER_STATUS_ID, TRUE);
  scr_setattentionflag_if_needed(SPECIAL_BUFFER_STATUS_ID, TRUE,
                                 ROSTER_UI_PRIO_STATUS_WIN_MESSAGE, prio_max);
}

//  scr_set_chatmode()
// Public function to (un)set chatmode...
void scr_set_chatmode(int enable)
{
  gboolean enter_chatmode = enable && chatmode == FALSE;
  chatmode = enable;
  scr_update_chat_status(TRUE);
  if (enter_chatmode)
    scr_buffer_readmark(-1);
}

//  scr_get_chatmode()
// Public function to get chatmode state.
int scr_get_chatmode(void)
{
  return chatmode;
}

//  scr_get_multimode()
// Public function to get multimode status...
int scr_get_multimode(void)
{
  return multimode;
}

//  scr_setmsgflag_if_needed(jid)
// Set the message flag unless we're already in the jid buffer window
void scr_setmsgflag_if_needed(const char *bjid, int special)
{
  const char *current_id;
  bool iscurrentlocked = FALSE;

  if (!bjid)
    return;

  if (current_buddy) {
    if (special)
      current_id = buddy_getname(BUDDATA(current_buddy));
    else
      current_id = buddy_getjid(BUDDATA(current_buddy));
    if (current_id) {
      winbuf_t *win_entry = scr_search_window(current_id, special);
      if (!win_entry) return;
      iscurrentlocked = win_entry->bd->lock;
    }
  } else {
    current_id = NULL;
  }
  if (!chatmode || !current_id || strcmp(bjid, current_id) || iscurrentlocked) {
    roster_msg_setflag(bjid, special, TRUE);
    scr_update_roster();
  }
}

//  scr_setattentionflag_if_needed(bare_jid, special, value, action)
// Set the attention flag unless we're already in the jid buffer window
// TODO: avoid code duplication with scr_setmsgflag_if_needed()
void scr_setattentionflag_if_needed(const char *bjid, int special,
                                    guint value, enum setuiprio_ops action)
{
  const char *current_id;
  winbuf_t *wb;
  bool iscurrentlocked = FALSE;

  if (!bjid)
    return;

  wb = scr_search_window(bjid, special);
  if (!wb)
    return;

  if (current_buddy) {
    if (special)
      current_id = buddy_getname(BUDDATA(current_buddy));
    else
      current_id = buddy_getjid(BUDDATA(current_buddy));
    if (current_id) {
      winbuf_t *win_entry = scr_search_window(current_id, special);
      if (!win_entry) return;
      iscurrentlocked = win_entry->bd->lock;
    }
  } else {
    current_id = NULL;
  }

  if (!chatmode || !current_id || strcmp(bjid, current_id) || iscurrentlocked) {
    roster_setuiprio(bjid, special, value, action);
    scr_update_roster();
  }
}

//  scr_set_multimode()
// Public function to (un)set multimode...
// Convention:
//  0 = disabled / 1 = multimode / 2 = multimode verbatim (commands disabled)
void scr_set_multimode(int enable, char *subject)
{
  g_free(multiline);
  multiline = NULL;

  g_free(multimode_subj);
  if (enable && subject)
    multimode_subj = g_strdup(subject);
  else
    multimode_subj = NULL;

  multimode = enable;
}

//  scr_get_multiline()
// Public function to get the current multi-line.
const char *scr_get_multiline(void)
{
  if (multimode && multiline)
    return multiline;
  return NULL;
}

//  scr_get_multimode_subj()
// Public function to get the multi-line subject, if any.
const char *scr_get_multimode_subj(void)
{
  if (multimode)
    return multimode_subj;
  return NULL;
}

//  scr_append_multiline(line)
// Public function to append a line to the current multi-line message.
// Skip empty leading lines.
void scr_append_multiline(const char *line)
{
  static int num;

  if (!multimode) {
    scr_LogPrint(LPRINT_NORMAL, "Error: Not in multi-line message mode!");
    return;
  }
  if (multiline) {
    int len = strlen(multiline)+strlen(line)+2;
    if (len >= HBB_BLOCKSIZE - 1) {
      // We don't handle single messages with size > HBB_BLOCKSIZE
      // (see hbuf)
      scr_LogPrint(LPRINT_NORMAL, "Your multi-line message is too big, "
                   "this line has not been added.");
      scr_LogPrint(LPRINT_NORMAL, "Please send this part now...");
      return;
    }
    if (num >= MULTILINE_MAX_LINE_NUMBER) {
      // We don't allow too many lines; however the maximum is arbitrary
      // (It should be < 1000 yet)
      scr_LogPrint(LPRINT_NORMAL, "Your message has too many lines, "
                   "this one has not been added.");
      scr_LogPrint(LPRINT_NORMAL, "Please send this part now...");
      return;
    }
    multiline = g_renew(char, multiline, len);
    strcat(multiline, "\n");
    strcat(multiline, line);
    num++;
  } else {
    // First message line (we skip leading empty lines)
    num = 0;
    if (line[0]) {
      multiline = g_strdup(line);
      num++;
    } else
      return;
  }
  scr_LogPrint(LPRINT_NORMAL|LPRINT_NOTUTF8,
               "Multi-line mode: line #%d added  [%.25s...", num, line);
}

//  scr_cmdhisto_addline()
// Add a line to the inputLine history
static void scr_cmdhisto_addline(char *line)
{
  int max_histo_lines;

  if (!line || !*line)
    return;

  max_histo_lines = settings_opt_get_int("cmdhistory_lines");

  if (max_histo_lines < 0)
    max_histo_lines = 1;

  if (max_histo_lines)
    while (cmdhisto_nblines >= (guint)max_histo_lines) {
      if (cmdhisto_cur && cmdhisto_cur == cmdhisto)
        break;
      g_free(cmdhisto->data);
      cmdhisto = g_list_delete_link(cmdhisto, cmdhisto);
      cmdhisto_nblines--;
    }

  cmdhisto = g_list_append(cmdhisto, g_strdup(line));
  cmdhisto_nblines++;
}

//  scr_cmdhisto_reset()
// Reset the inputLine history
static void scr_cmdhisto_reset(void)
{
  while (cmdhisto_nblines) {
    g_free(cmdhisto->data);
    cmdhisto = g_list_delete_link(cmdhisto, cmdhisto);
    cmdhisto_nblines--;
  }
  cmdhisto_backup[0] = 0;
  cmdhisto_cur = NULL;
}

//  scr_cmdhisto_prev()
// Look for previous line beginning w/ the given mask in the inputLine history
// Returns NULL if none found
static const char *scr_cmdhisto_prev(char *mask, guint len)
{
  GList *hl;
  if (!cmdhisto_cur) {
    hl = g_list_last(cmdhisto);
    if (hl) { // backup current line
      strncpy(cmdhisto_backup, mask, INPUTLINE_LENGTH);
    }
  } else {
    hl = g_list_previous(cmdhisto_cur);
  }
  while (hl) {
    if (!strncmp((char*)hl->data, mask, len)) {
      // Found a match
      cmdhisto_cur = hl;
      return (const char*)hl->data;
    }
    hl = g_list_previous(hl);
  }
  return NULL;
}

//  scr_cmdhisto_next()
// Look for next line beginning w/ the given mask in the inputLine history
// Returns NULL if none found
static const char *scr_cmdhisto_next(char *mask, guint len)
{
  GList *hl;
  if (!cmdhisto_cur) return NULL;
  hl = cmdhisto_cur;
  while ((hl = g_list_next(hl)) != NULL)
    if (!strncmp((char*)hl->data, mask, len)) {
      // Found a match
      cmdhisto_cur = hl;
      return (const char*)hl->data;
    }
  // If the "backuped" line matches, we'll use it
  if (strncmp(cmdhisto_backup, mask, len)) return NULL; // No match
  cmdhisto_cur = NULL;
  return cmdhisto_backup;
}

static char *_strmove(char *dst, const char *src)
{
  char *dest = dst;
  while ((*dest++ = *src++) != '\0')
    ;
  return dest;
}

//  readline_transpose_chars()
// Drag  the  character  before point forward over the character at
// point, moving point forward as well.  If point is at the end  of
// the  line, then this transposes the two characters before point.
void readline_transpose_chars(void)
{
  char *c1, *c2;
  unsigned a, b;

  if (ptr_inputline == inputLine) return;

  if (!*ptr_inputline) { // We're at EOL
    // If line is only 1 char long, nothing to do...
    if (ptr_inputline == prev_char(ptr_inputline, inputLine)) return;
    // Transpose the two previous characters
    c2 = prev_char(ptr_inputline, inputLine);
    c1 = prev_char(c2, inputLine);
    a = get_char(c1);
    b = get_char(c2);
    put_char(put_char(c1, b), a);
  } else {
    // Swap the two characters before the cursor and move right.
    c2 = ptr_inputline;
    c1 = prev_char(c2, inputLine);
    a = get_char(c1);
    b = get_char(c2);
    put_char(put_char(c1, b), a);
  }
}

void readline_forward_kill_word(void)
{
  char *c, *old = ptr_inputline;
  int spaceallowed = 1;

  if (! *ptr_inputline) return;

  for (c = ptr_inputline ; *c ; c = next_char(c)) {
    if (!iswalnum(get_char(c))) {
      if (iswblank(get_char(c))) {
        if (!spaceallowed) break;
      } else spaceallowed = 0;
    } else spaceallowed = 0;
  }

  // Modify the line
  for (;;) {
    *old = *c++;
    if (!*old++) break;
  }
}

//  readline_backward_kill_word()
// Kill the word before the cursor, in input line
void readline_backward_kill_word(void)
{
  char *c, *old = ptr_inputline;
  int spaceallowed = 1;

  if (ptr_inputline == inputLine) return;

  c = prev_char(ptr_inputline, inputLine);
  for ( ; c > inputLine ; c = prev_char(c, inputLine)) {
    if (!iswalnum(get_char(c))) {
      if (iswblank(get_char(c))) {
        if (!spaceallowed) break;
      } else spaceallowed = 0;
    } else spaceallowed = 0;
  }

  if (c == inputLine && *c == COMMAND_CHAR && old != c+1) {
      c = next_char(c);
  } else if (c != inputLine || (iswblank(get_char(c)) && !spaceallowed)) {
    if ((c < prev_char(ptr_inputline, inputLine)) && (!iswalnum(get_char(c))))
      c = next_char(c);
  }

  // Modify the line
  ptr_inputline = c;
  _strmove(ptr_inputline, old);
}

//  readline_backward_word()
// Move back to the start of the current or previous word
void readline_backward_word(void)
{
  int i = 0;

  if (ptr_inputline == inputLine) return;

  if (iswalnum(get_char(ptr_inputline)) &&
      !iswalnum(get_char(prev_char(ptr_inputline, inputLine))))
    i--;

  for ( ;
       ptr_inputline > inputLine;
       ptr_inputline = prev_char(ptr_inputline, inputLine)) {
    if (!iswalnum(get_char(ptr_inputline))) {
      if (i) {
        ptr_inputline = next_char(ptr_inputline);
        break;
      }
    } else i++;
  }

}

//  readline_forward_word()
// Move forward to the end of the next word
void readline_forward_word(void)
{
  int stopsymbol_allowed = 1;

  while (*ptr_inputline) {
    if (!iswalnum(get_char(ptr_inputline))) {
      if (!stopsymbol_allowed) break;
    } else stopsymbol_allowed = 0;
    ptr_inputline = next_char(ptr_inputline);
  }

}

void readline_updowncase_word(int upcase)
{
  int stopsymbol_allowed = 1;

  while (*ptr_inputline) {
    if (!iswalnum(get_char(ptr_inputline))) {
      if (!stopsymbol_allowed) break;
    } else {
      stopsymbol_allowed = 0;
      if (upcase)
        put_char(ptr_inputline, towupper(get_char(ptr_inputline)));
      else
        put_char(ptr_inputline, towlower(get_char(ptr_inputline)));
    }
    ptr_inputline = next_char(ptr_inputline);
  }

}

void readline_capitalize_word(void)
{
  int stopsymbol_allowed = 1;
  int upcased = 0;

  while (*ptr_inputline) {
    if (!iswalnum(get_char(ptr_inputline))) {
      if (!stopsymbol_allowed) break;
    } else {
      stopsymbol_allowed = 0;
      if (!upcased) {
        put_char(ptr_inputline, towupper(get_char(ptr_inputline)));
        upcased = 1;
      } else
        put_char(ptr_inputline, towlower(get_char(ptr_inputline)));
    }
    ptr_inputline = next_char(ptr_inputline);
  }

}

void readline_backward_char(void)
{
  if (ptr_inputline == (char*)&inputLine) return;

  ptr_inputline = prev_char(ptr_inputline, inputLine);
}

void readline_forward_char(void)
{
  if (!*ptr_inputline) return;

  ptr_inputline = next_char(ptr_inputline);
}

//  readline_accept_line(down_history)
// Validate current command line.
// If down_history is true, load the next history line.
void readline_accept_line(int down_history)
{
  char *end;

  scr_check_auto_away(TRUE);

  // Trailing newlines are not part of the message
  end = inputLine + strlen(inputLine);
  while (end > inputLine && (end[-1] == '\n' || end[-1] == '\r'))
    *--end = 0;
  if (ptr_inputline > end)
    ptr_inputline = end;
  if (strchr(inputLine, '\n') && !process_line_allows_newlines(inputLine)) {
    scr_LogPrint(LPRINT_NORMAL, "This command cannot span several lines.");
    return;
  }

  last_activity_buddy = current_buddy;
  if (console_shown && inputLine[0] && inputLine[0] != COMMAND_CHAR) {
    // XMPP console: send raw XML
    gchar *xml = g_locale_to_utf8(inputLine, -1, NULL, NULL, NULL);
    if (xml && !xmpp_send_raw(xml))
      scr_LogPrint(LPRINT_NORMAL, "You are not connected.");
    g_free(xml);
  } else {
    process_line(inputLine);
  }
  // Add line to history
  scr_cmdhisto_addline(inputLine);
  // Reset the line
  ptr_inputline = inputLine;
  *ptr_inputline = 0;

  if (down_history) {
    // Use next history line instead of a blank line
    const char *l = scr_cmdhisto_next("", 0);
    if (l) strcpy(inputLine, l);
    // Reset backup history line
    cmdhisto_backup[0] = 0;
  } else {
    // Reset history line pointer
    cmdhisto_cur = NULL;
  }
}

//  readline_clear_history()
// Clear command line history.
void readline_clear_history(void)
{
  scr_cmdhisto_reset();
}

void readline_cancel_completion(void)
{
  scr_cancel_current_completion();
  scr_end_current_completion();
}

void readline_do_completion(gboolean fwd)
{
  int i, n;

  if (multimode != 2) {
    // Not in verbatim multi-line mode
    scr_handle_tab(fwd);
  } else {
    // Verbatim multi-line mode: expand tab
    char tabstr[9];
    n = 8 - (ptr_inputline - inputLine) % 8;
    for (i = 0; i < n; i++)
      tabstr[i] = ' ';
    tabstr[i] = '\0';
    scr_insert_text(tabstr);
  }
}

void readline_refresh_screen(void)
{
  scr_check_auto_away(TRUE);
  keypad(inputWnd, TRUE);
  parse_colors();
  scr_resize();
  redrawwin(stdscr);
}

void readline_disable_chat_mode(guint show_roster)
{
  scr_check_auto_away(TRUE);
  if (chatmode) {
    scr_buffer_readmark(TRUE);
    if (vi_mode)
      clear_inputline();
  }
  currentWindow = NULL;
  chatmode = FALSE;
  console_shown = FALSE;
  if (current_buddy)
    buddy_setflags(BUDDATA(current_buddy), ROSTER_FLAG_LOCK, FALSE);
  if (show_roster)
    scr_roster_visibility(1);
  scr_update_chat_status(FALSE);
  top_panel(chatPanel);
  draw_members();
  top_panel(inputPanel);
  update_panels();
}

void readline_hist_beginning_search_bwd(void)
{
  const char *l = scr_cmdhisto_prev(inputLine, ptr_inputline-inputLine);
  if (l) strcpy(inputLine, l);
}

void readline_hist_beginning_search_fwd(void)
{
  const char *l = scr_cmdhisto_next(inputLine, ptr_inputline-inputLine);
  if (l) strcpy(inputLine, l);
}

void readline_hist_prev(void)
{
  const char *l = scr_cmdhisto_prev(inputLine, 0);
  if (l) {
    strcpy(inputLine, l);
    // Set the pointer at the end of the buffer
    readline_buffer_end();
  }
}

void readline_hist_next(void)
{
  const char *l = scr_cmdhisto_next(inputLine, 0);
  if (l) {
    strcpy(inputLine, l);
    // Set the pointer at the end of the buffer
    readline_buffer_end();
  }
}

void readline_backward_kill_char(void)
{
  char *src, *c;

  if (ptr_inputline == (char*)&inputLine)
    return;

  src = ptr_inputline;
  c = prev_char(ptr_inputline, inputLine);
  ptr_inputline = c;
  _strmove(ptr_inputline, src);
}

void readline_forward_kill_char(void)
{
  if (!*ptr_inputline)
    return;

  _strmove(ptr_inputline, next_char(ptr_inputline));
}

//  line_start(p), line_end(p)
// Beginning/end of the line (in a multi-line input buffer) containing p.
static char *line_start(char *p)
{
  while (p > inputLine && p[-1] != '\n')
    p--;
  return p;
}

static char *line_end(char *p)
{
  while (*p && *p != '\n')
    p++;
  return p;
}

void readline_iline_start(void)
{
  ptr_inputline = line_start(ptr_inputline);
}

void readline_iline_end(void)
{
  ptr_inputline = line_end(ptr_inputline);
}

void readline_buffer_start(void)
{
  ptr_inputline = inputLine;
}

void readline_buffer_end(void)
{
  ptr_inputline = inputLine + strlen(inputLine);
}

//  readline_backward_kill_iline()
// Kill from the beginning of the line to the cursor.  At the beginning of
// a line, join it with the previous one.
void readline_backward_kill_iline(void)
{
  char *dest = line_start(ptr_inputline);

  if (ptr_inputline == inputLine) return;

  if (dest == ptr_inputline) {
    // Remove the previous newline
    dest--;
  } else if (dest == inputLine && *dest == COMMAND_CHAR &&
             ptr_inputline != dest+1) {
    dest = next_char(dest);
  }

  _strmove(dest, ptr_inputline);
  ptr_inputline = dest;
}

//  readline_forward_kill_iline()
// Kill from the cursor to the end of the line.  At the end of a line, join
// it with the next one.
void readline_forward_kill_iline(void)
{
  char *end = line_end(ptr_inputline);

  if (end == ptr_inputline && *end == '\n')
    end++;
  _strmove(ptr_inputline, end);
}

//  readline_insert_newline()
// Insert a line break (multi-line message).
void readline_insert_newline(void)
{
  scr_insert_text("\n");
}

//  readline_line_up_or_hist(), readline_line_down_or_hist()
// Move the cursor to the previous/next row of the input line, or in the
// history when the cursor is on the first/last row.
void readline_line_up_or_hist(void)
{
  il_layout(inputLine, ptr_inputline - inputLine, input_text_width(),
            &InputLayout);
  if (InputLayout.cursor_row == 0) {
    readline_hist_beginning_search_bwd();
    return;
  }
  ptr_inputline = inputLine + il_offset_at(inputLine, &InputLayout,
                                           InputLayout.cursor_row - 1,
                                           InputLayout.cursor_col);
}

void readline_line_down_or_hist(void)
{
  il_layout(inputLine, ptr_inputline - inputLine, input_text_width(),
            &InputLayout);
  if (InputLayout.cursor_row + 1 >= InputLayout.rows) {
    readline_hist_beginning_search_fwd();
    return;
  }
  ptr_inputline = inputLine + il_offset_at(inputLine, &InputLayout,
                                           InputLayout.cursor_row + 1,
                                           InputLayout.cursor_col);
}

void readline_send_multiline(void)
{
  // Validate current multi-line
  if (multimode)
    process_command(mkcmdstr("msay send"), TRUE);
}

void readline_insert(const char *toinsert)
{
  if (!toinsert || !*toinsert) return;

  scr_insert_text(toinsert);
}

//  which_row()
// Tells which row our cursor is in, in the command line.
// -2 -> normal text
// -1 -> room: nickname completion
//  0 -> command
//  1 -> parameter 1 (etc.)
//  If > 0, then *p_row is set to the beginning of the row
static int which_row(const char **p_row)
{
  int row = -1;
  char *p;
  int quote = FALSE;

  // Not a command?
  if ((ptr_inputline == inputLine) || (inputLine[0] != COMMAND_CHAR)) {
    if (!current_buddy) return -2;
    if (buddy_gettype(BUDDATA(current_buddy)) == ROSTER_TYPE_ROOM) {
      *p_row = line_start(ptr_inputline);
      return -1;
    }
    return -2;
  }

  // This is a command
  row = 0;
  for (p = inputLine ; p < ptr_inputline ; p = next_char(p)) {
    if (quote) {
      if (*p == '"' && *(p-1) != '\\')
        quote = FALSE;
      continue;
    }
    if (*p == '"' && *(p-1) != '\\') {
      quote = TRUE;
    } else if (*p == ' ') {
      if (*(p-1) != ' ')
        row++;
      *p_row = p+1;
    }
  }
  return row;
}

//  scr_insert_text()
// Insert the given text at the current cursor position.
// The cursor is moved.  We don't check if the cursor still is in the screen
// after, the caller should do that.
static void scr_insert_text(const char *text)
{
  char tmpLine[INPUTLINE_LENGTH+1];
  int len = strlen(text);
  // Check the line isn't too long
  if (strlen(inputLine) + len >= INPUTLINE_LENGTH) {
    scr_LogPrint(LPRINT_LOGNORM, "Cannot insert text, line too long.");
    return;
  }

  strcpy(tmpLine, ptr_inputline);
  strcpy(ptr_inputline, text);
  ptr_inputline += len;
  strcpy(ptr_inputline, tmpLine);
}

static void scr_cancel_current_completion(void);

//  scr_handle_tab()
// Function called when tab is pressed.
// Initiate or continue a completion...
// If fwd is false, a backward-completion is requested.
static void scr_handle_tab(gboolean fwd)
{
  int nrow;
  const char *row;
  const char *cchar;
  guint compl_categ;

  row = inputLine; // (Kills a GCC warning)
  nrow = which_row(&row);

  // a) No completion if no leading slash ('cause not a command),
  //    unless this is a room (then, it is a nickname completion)
  // b) We can't have more than 2 parameters (we use 2 flags)
  if ((nrow == -2) || (nrow == 3 && !completion_started) || nrow > 3)
    return;

  if (nrow == 0) {          // Command completion
    row = next_char(inputLine);
    compl_categ = COMPL_CMD;
  } else if (nrow == -1) {  // Nickname completion
    compl_categ = COMPL_RESOURCE;
  } else {                  // Other completion, depending on the command
    int alias = FALSE;
    cmd *com;
    char *xpline = expandalias(inputLine);
    com = cmd_get(xpline);
    if (xpline != inputLine) {
      // This is an alias, so we can't complete rows > 0
      alias = TRUE;
      g_free(xpline);
    }
    if ((!com && (!alias || !completion_started)) || !row) {
      scr_LogPrint(LPRINT_NORMAL, "I cannot complete that...");
      return;
    }
    if (!alias)
      compl_categ = com->completion_flags[nrow-1];
    else
      compl_categ = 0;
  }

  if (!completion_started) {
    guint dynlist;
    GSList *list;

    if (!compl_categ)
      return; // Nothing to complete

    list = compl_get_category_list(compl_categ, &dynlist);
    if (list) {
      guint n;
      char *prefix = g_strndup(row, ptr_inputline-row);
      // Init completion
      n = new_completion(prefix, list,
                         (compl_categ == COMPL_RESOURCE ?
                          settings_opt_get("muc_completion_suffix") : NULL));
      g_free(prefix);
      if (n == 0 && nrow == -1) {
        // This is a MUC room and we can't complete from the beginning of the
        // line.  Let's try a bit harder and complete the current word.
        row = prev_char(ptr_inputline, inputLine);
        while (row >= inputLine) {
          if (iswspace(get_char(row)) || get_char(row) == '(') {
              row = next_char((char*)row);
              break;
          }
          if (row == inputLine)
            break;
          row = prev_char((char*)row, inputLine);
        }
        // There's no need to try again if row == inputLine
        if (row > inputLine) {
          prefix = g_strndup(row, ptr_inputline-row);
          new_completion(prefix, list, NULL);
          g_free(prefix);
        }
      }
      // Free the list if it's a dynamic one
      if (dynlist) {
        GSList *slp;
        for (slp = list; slp; slp = g_slist_next(slp))
          g_free(slp->data);
        g_slist_free(list);
      }
      // Now complete
      cchar = complete(fwd);
      if (cchar)
        scr_insert_text(cchar);
      completion_started = TRUE;
    }
  } else {      // Completion already initialized
    scr_cancel_current_completion();
    // Now complete again
    cchar = complete(fwd);
    if (cchar)
      scr_insert_text(cchar);
  }
}

static void scr_cancel_current_completion(void)
{
  char *c;
  char *src = ptr_inputline;
  guint back = cancel_completion();
  guint i;
  // Remove $back chars
  for (i = 0; i < back; i++)
    ptr_inputline = prev_char(ptr_inputline, inputLine);
  c = ptr_inputline;
  for ( ; *src ; )
    *c++ = *src++;
  *c = 0;
}

static void scr_end_current_completion(void)
{
  done_completion();
  completion_started = FALSE;
}

/* -- Input prompt (mc.format.input_prompt) -- */

static const char *const prompt_fields[] = {
  "name", "jid", "type", "chatmode", "command", "multiline", NULL
};

static gchar *prompt_field(const char *name, const char *arg, gpointer data)
{
  gpointer bud = current_buddy ? BUDDATA(current_buddy) : NULL;

  if (!strcmp(name, "name"))
    return bud ? g_strdup(buddy_getname(bud)) : NULL;
  if (!strcmp(name, "jid"))
    return bud ? g_strdup(buddy_getjid(bud)) : NULL;
  if (!strcmp(name, "type")) {
    guint t = bud ? buddy_gettype(bud) : 0;
    return g_strdup((t & ROSTER_TYPE_USER) ? "user" :
                    (t & ROSTER_TYPE_ROOM) ? "room" :
                    (t & ROSTER_TYPE_AGENT) ? "agent" :
                    (t & ROSTER_TYPE_GROUP) ? "group" : "special");
  }
  if (!strcmp(name, "chatmode"))
    return chatmode ? g_strdup("1") : NULL;
  if (!strcmp(name, "command"))
    return inputLine[0] == COMMAND_CHAR ? g_strdup("1") : NULL;
  if (!strcmp(name, "multiline"))
    return multimode ? g_strdup("1") : NULL;
  return NULL;
}

//  input_prompt(out)
// Render the input prompt; returns its width (0 if there is none).  The
// prompt uses at most a third of the screen.
static guint input_prompt(sstr_t *out)
{
  if (!msgfmt_ui_has("input_prompt"))
    return 0;
  int w = Layout.input.w ? Layout.input.w : maxX;
  if (!msgfmt_ui("input_prompt", NULL, prompt_fields, prompt_field, NULL,
                 w / 3, out))
    return 0;
  return MIN(out->width, (guint)w / 3);
}

//  input_text_width()
// Width available for the text of the input line.
static guint input_text_width(void)
{
  sstr_t *p = sstr_new();
  guint w = input_prompt(p);
  int width = Layout.input.w ? Layout.input.w : maxX;
  sstr_free(p);
  return width > (int)w + 1 ? width - w : 1;
}

//  input_max_rows()
// Maximum height of the input line window.
static guint input_max_rows(void)
{
  const char *v = settings_opt_get("input_max_rows");
  int n = v ? atoi(v) : 6;
  return n > 0 ? n : 1;
}

//  refresh_inputline()
// Draw the input line.  The window grows (up to input_max_rows lines)
// when the text needs more rows.
static void refresh_inputline(void)
{
  guint r, visible, height, pw;
  gboolean spell = FALSE;
  int width = getmaxx(inputWnd);
  sstr_t *prompt = sstr_new();

  pw = input_prompt(prompt);
  il_layout(inputLine, ptr_inputline - inputLine,
            width > (int)pw + 1 ? width - pw : 1, &InputLayout);

  height = MIN(InputLayout.rows, input_max_rows());
  if (height != (guint)Input_Win_Height) {
    Input_Win_Height = height;
    scr_resize();
    width = getmaxx(inputWnd);
  }
  visible = Layout.input.h;

  // Keep the cursor visible
  if (InputLayout.cursor_row < input_scroll)
    input_scroll = InputLayout.cursor_row;
  else if (InputLayout.cursor_row >= input_scroll + visible)
    input_scroll = InputLayout.cursor_row - visible + 1;
  if (input_scroll + visible > InputLayout.rows)
    input_scroll = InputLayout.rows > visible ? InputLayout.rows - visible : 0;

#if defined(WITH_ENCHANT) || defined(WITH_ASPELL)
  if (settings_opt_get_int("spell_enable") && (chatmode || !vi_mode)) {
    memset(maskLine, 0, INPUTLINE_LENGTH+1);
    spellcheck(inputLine, maskLine);
    spell = TRUE;
  }
#endif

  werase(inputWnd);
  if (pw && input_scroll == 0) {
    wmove(inputWnd, 0, 0);
    draw_sstr(inputWnd, prompt, A_NORMAL, FALSE, pw);
  }
  sstr_free(prompt);
  for (r = 0; r < visible && input_scroll + r < InputLayout.rows; r++) {
    char *p = inputLine + il_row_start(&InputLayout, input_scroll + r);
    char *end = inputLine + il_row_end(&InputLayout, input_scroll + r);

    wmove(inputWnd, r, pw);
    while (p < end) {
      char *next = next_char(p);
#if defined(WITH_ENCHANT) || defined(WITH_ASPELL)
      if (spell && maskLine[p - inputLine])
        wattrset(inputWnd, A_UNDERLINE);
#endif
      if ((unsigned char)*p < ' ')
        waddch(inputWnd, '?' | A_REVERSE);  // Control character
      else
        waddnstr(inputWnd, p, next - p);
      if (spell)
        wattrset(inputWnd, A_NORMAL);
      p = next;
    }
  }
  wmove(inputWnd, InputLayout.cursor_row - input_scroll,
        MIN((int)(InputLayout.cursor_col + pw), width - 1));
}

//  scr_get_inputline(), scr_set_inputline(text), scr_insert_inputline(text)
// Access to the input line (strings in the locale charset).
const char *scr_get_inputline(void)
{
  return inputLine;
}

void scr_set_inputline(const char *text)
{
  if (strlen(text) > INPUTLINE_LENGTH) {
    scr_LogPrint(LPRINT_NORMAL, "Text too long for the input line.");
    return;
  }
  strcpy(inputLine, text);
  ptr_inputline = inputLine + strlen(inputLine);
  if (Curses)
    refresh_inputline();
}

void scr_insert_inputline(const char *text)
{
  scr_insert_text(text);
  if (Curses)
    refresh_inputline();
}

void scr_handle_CtrlC(void)
{
  if (!Curses) return;
  // Leave multi-line mode
  process_command(mkcmdstr("msay abort"), TRUE);
  // Same as Ctrl-g, now
  scr_cancel_current_completion();
  scr_end_current_completion();
  refresh_inputline();
}

static void add_keyseq(char *seqstr, guint mods, gint key)
{
  keyseq_t *ks;

  // Let's make sure the length is correct
  if (strlen(seqstr) > MAX_KEYSEQ_LENGTH) {
    scr_LogPrint(LPRINT_LOGNORM, "add_keyseq(): key sequence is too long!");
    return;
  }

  ks = g_new0(keyseq_t, 1);
  ks->seqstr = g_strdup(seqstr);
  ks->spec.mods = mods;
  ks->spec.key = key;
  keyseqlist = g_slist_append(keyseqlist, ks);
}

//  match_keyseq(iseq, &ret)
// Check if "iseq" is a known key escape sequence.
// Return value:
// -1  if "seq" matches no known sequence
//  0  if "seq" could match 1 or more known sequences
//  1  if "seq" matches a key sequence; *ret is set to the matching
//     keyseq structure.
static inline gint match_keyseq(int *iseq, keyseq_t **ret)
{
  GSList *ksl;
  keyseq_t *ksp;
  char *p, c;
  int *i;
  int needmore = FALSE;

  for (ksl = keyseqlist; ksl; ksl = g_slist_next(ksl)) {
    ksp = ksl->data;
    p = ksp->seqstr;
    i = iseq;
    while (1) {
      c = (unsigned char)*i;
      if (!*p && !c) { // Match
        (*ret) = ksp;
        return 1;
      }
      if (!c) {
        // iseq is too short
        needmore = TRUE;
        break;
      } else if (!*p || c != *p) {
        // This isn't a match
        break;
      }
      p++; i++;
    }
  }

  if (needmore)
    return 0;
  return -1;
}

//  match_csi(iseq, len, spec)
// Generic decoding of "[" params final.
// Return value: -1 if this isn't a known CSI sequence, 0 if more
// characters are needed, 1 if spec has been set.
static gint match_csi(const int *iseq, int len, keyspec_t *spec)
{
  char params[MAX_KEYSEQ_LENGTH+1];
  int i, last;

  if (len < 2 || iseq[0] != '[')
    return -1;
  for (i = 1; i < len - 1; i++) {
    int c = iseq[i];
    if (!(g_ascii_isdigit(c) || c == ';' || c == ':'))
      return -1;
    params[i-1] = c;
  }
  params[len-2] = '\0';
  last = iseq[len-1];
  if (g_ascii_isdigit(last) || last == ';' || last == ':')
    return 0;
  if (last < 0x40 || last > 0x7e)
    return -1;
  return key_from_csi(params, last, spec) ? 1 : -1;
}

static inline int match_utf8_keyseq(int *iseq)
{
  int *strp = iseq;
  unsigned c = *strp++;
  unsigned mask = 0x80;
  int len = -1;
  while (c & mask) {
    mask >>= 1;
    len++;
  }
  if (len <= 0 || len > 4)
    return -1;
  c &= mask - 1;
  while ((*strp & 0xc0) == 0x80) {
    if (len-- <= 0) // can't happen
      return -1;
    c = (c << 6) | (*strp++ & 0x3f);
  }
  if (len)
    return 0;
  return c;
}

void scr_getch(keycode_t *kcode)
{
  keyseq_t *mks = NULL;
  int  ks[MAX_KEYSEQ_LENGTH+1];
  int i;

  memset(kcode, 0, sizeof(keycode_t));
  memset(ks,  0, sizeof(ks));

  kcode->value = wgetch(inputWnd);
  if (utf8_mode) {
    bool ismeta = (kcode->value == 27);
#ifdef NCURSES_MOUSE_VERSION
    bool ismouse = (kcode->value == KEY_MOUSE);

    if (ismouse) {
      MEVENT mouse;
      getmouse(&mouse);
      kcode->value = mouse.bstate;
      kcode->mcode = MKEY_MOUSE;
      return;
    } else if (ismeta)
#else
    if (ismeta)
#endif
      ks[0] = wgetch(inputWnd);
    else
      ks[0] = kcode->value;

    for (i = 0; i < MAX_KEYSEQ_LENGTH - 1; i++) {
      int match = match_utf8_keyseq(ks);
      if (match == -1)
        break;
      if (match > 0) {
        kcode->value = match;
        kcode->utf8 = 1;
        if (ismeta)
          kcode->mcode = MKEY_META;
        return;
      }
      ks[i + 1] = wgetch(inputWnd);
      if (ks[i + 1] == ERR)
        break;
    }
    while (i > 0)
      ungetch(ks[i--]);
    if (ismeta && ks[0] != ERR)
      ungetch(ks[0]);
    memset(ks,  0, sizeof(ks));
  }
  if (kcode->value != 27)
    return;

  // Check for escape key sequence
  for (i=0; i < MAX_KEYSEQ_LENGTH; i++) {
    int match, csi;
    keyspec_t spec;
    ks[i] = wgetch(inputWnd);
    if (ks[i] == ERR) break;
    match = match_keyseq(ks, &mks);
    if (match > 0) {
      // We have a matching sequence
      kcode->mcode = MKEY_SPEC;
      kcode->spec = mks->spec;
      return;
    }
    csi = match_csi(ks, i+1, &spec);
    if (csi > 0) {
      kcode->mcode = MKEY_SPEC;
      kcode->spec = spec;
      return;
    }
    if (match == -1 && csi == -1) {
      // No such key sequence.  Let's increment i as it is a valid key.
      i++;
      break;
    }
  }

  // No match.  Let's return a meta-key.
  if (i > 0) {
    kcode->mcode = MKEY_META;
    kcode->value = ks[0];
  }
  if (i > 1) {
    // We need to push some keys back to the keyboard buffer
    while (i-- > 1)
      ungetch(ks[i]);
  }
  return;
}

static gboolean format_changed;

//  scr_format_changed()
// The message formats have changed: the buffers will be rebuilt.
void scr_format_changed(void)
{
  format_changed = TRUE;
}

static void rebuild_win_buffer(gpointer key, gpointer value, gpointer data)
{
  winbuf_t *wbp = value;
  if (!(wbp && wbp->bd))
    return;
  wbp->bd->width = 0;   // Force the rewrapping
  sync_buffer_width(wbp);
}

void scr_do_update(void)
{
  if (format_changed && Curses) {
    format_changed = FALSE;
    g_hash_table_foreach(winbufhash, rebuild_win_buffer, NULL);
    if (statusWindow)
      rebuild_win_buffer(NULL, statusWindow, NULL);
    if (chatmode)
      scr_show_buddy_window();
    else
      update_panels();
    scr_update_roster();
    if (current_buddy)
      scr_update_chat_status(TRUE);
    else
      scr_update_main_status(TRUE);
  }
  if (colors_stalled && Curses) {
    parse_colors();
    if (Layout.borders)
      draw_frames();
    scr_update_roster();
    scr_update_main_status(TRUE);
  }
  doupdate();
}

//  keycode_to_spec(kcode, spec)
// Get the key combination for a key read by scr_getch().
static void keycode_to_spec(const keycode_t *kcode, keyspec_t *spec)
{
  memset(spec, 0, sizeof(*spec));

  switch (kcode->mcode) {
    case MKEY_SPEC:
        *spec = kcode->spec;
        return;
    case MKEY_MOUSE:
#ifdef NCURSES_MOUSE_VERSION
        if (kcode->value & BUTTON4_PRESSED)
          spec->key = KN_WHEELUP;
# ifdef BUTTON5_PRESSED
        else if (kcode->value & BUTTON5_PRESSED)
          spec->key = KN_WHEELDOWN;
# endif
        else
#endif
        {
          spec->key = KN_MOUSE;
          spec->raw = kcode->value;
        }
        return;
    default:
        break;
  }

  if (kcode->utf8)
    spec->key = kcode->value;
  else if (kcode->value > KEY_MAX)
    // Extended keys are defined at run time by ncurses
    (void)(key_from_terminfo_name(keyname(kcode->value), spec) ||
           key_from_ncurses(kcode->value, spec));
  else
    key_from_ncurses(kcode->value, spec);

  if (kcode->mcode == MKEY_META)
    spec->mods |= KMOD_ALT;
  key_normalize(spec);
}

//  scr_keycode_name(kcode)
// Return the name of the key combination (to be freed by the caller).
gchar *scr_keycode_name(const keycode_t *kcode)
{
  keyspec_t spec;
  keycode_to_spec(kcode, &spec);
  return key_format(&spec);
}

static void bindcommand(keycode_t kcode)
{
  gchar *keyname;
  const gchar *boundcmd;

  keyname = scr_keycode_name(&kcode);
  if (mlua_call_binding(keyname)) {
    scr_check_auto_away(TRUE);
    g_free(keyname);
    return;
  }
  boundcmd = settings_get(SETTINGS_TYPE_BINDING, keyname);

  if (boundcmd) {
    gchar *cmdline = from_utf8(boundcmd);
    scr_check_auto_away(TRUE);
    process_command(cmdline, TRUE);
    g_free(cmdline);
    g_free(keyname);
    return;
  }

  scr_LogPrint(LPRINT_NORMAL, "Unknown key: %s", keyname);
  g_free(keyname);
#ifndef UNICODE
  if (utf8_mode)
    scr_LogPrint(LPRINT_NORMAL,
                 "WARNING: Compiled without full UTF-8 support!");
#endif
}

static void scr_process_vi_arrow_key(int key)
{
  const char *l;
  char mask[INPUTLINE_LENGTH+1];
  size_t cmd_len = strlen(mask);
  size_t str_len = strlen(inputLine) - 1;

  strncpy(mask, mkcmdstr("roster search "), INPUTLINE_LENGTH);

  if  (inputLine[0] == COMMAND_CHAR) {
    if (!chatmode) { // Command mode
      if (key == KEY_UP)
        l = scr_cmdhisto_prev(inputLine, ptr_inputline - inputLine);
      else
        l = scr_cmdhisto_next(inputLine, ptr_inputline - inputLine);
      if (l)
        strcpy(inputLine, l);

      return;
    }

    // Chat mode

    if (cmd_len + str_len > INPUTLINE_LENGTH)
      return;

    memcpy(mask + cmd_len, inputLine + 1, str_len + 1);
    if (key == KEY_UP)
      l = scr_cmdhisto_prev(mask, ptr_inputline - inputLine + cmd_len - 1);
    else
      l = scr_cmdhisto_next(mask, ptr_inputline - inputLine + cmd_len - 1);
    if (l)
      strcpy(inputLine + 1, l + cmd_len);

    return;
  }

  if  (inputLine[0] == VI_SEARCH_COMMAND_CHAR)
    return;

  if (key == KEY_UP)
    process_command(mkcmdstr("roster up"), TRUE);
  else if (key == KEY_DOWN)
    process_command(mkcmdstr("roster down"), TRUE);
}


/* -- Pasted text -- */

// Maximum size of pasted text we accept
#define PASTE_MAX_SIZE  (256*1024)

static struct {
  gboolean active;      // Collecting pasted text
  gboolean bracketed;   // ... between \e[200~ and \e[201~
  GString *buf;
  gint64 last_key;      // Time of the last pasted key (monotonic, us)
  guint timer;
  gchar *pending;       // Text waiting for a confirmation ("confirm" mode)
} paste;

static void paste_commit(void);

static guint paste_option_int(const char *name, guint def)
{
  const char *v = settings_opt_get(name);
  return v ? (guint)MAX(atoi(v), 0) : def;
}

//  paste_timer_cb()
// End of a detected paste (no more input), or bracketed paste without end.
static gboolean paste_timer_cb(gpointer data)
{
  gint64 quiet = paste.bracketed ? 500000 :
                 (gint64)paste_option_int("paste_timeout_ms", 30) * 1000;

  if (!paste.active) {
    paste.timer = 0;
    return FALSE;
  }
  if (g_get_monotonic_time() - paste.last_key < quiet)
    return TRUE;    // Wait more
  paste.timer = 0;
  paste_commit();
  scr_do_update();
  return FALSE;
}

static void paste_start(gboolean bracketed)
{
  if (!paste.buf)
    paste.buf = g_string_new(NULL);
  g_string_truncate(paste.buf, 0);
  paste.active = TRUE;
  paste.bracketed = bracketed;
  paste.last_key = g_get_monotonic_time();
  if (!paste.timer)
    paste.timer = g_timeout_add(bracketed ? 250 : 10, paste_timer_cb, NULL);
}

//  paste_add_key(kcode)
// Add the key to the pasted text.
static void paste_add_key(const keycode_t *k)
{
  paste.last_key = g_get_monotonic_time();
  if (paste.buf->len >= PASTE_MAX_SIZE)
    return;
  if (k->mcode == MKEY_META) {
    // An Escape character in the text: keep the next character only
  } else if (k->mcode != MKEY_NONE) {
    return;   // Escape sequences are dropped
  }
  if (k->utf8)
    g_string_append_unichar(paste.buf, k->value);
  else if (k->value == 13)
    g_string_append_c(paste.buf, '\r');
  else if (k->value == 10 || k->value == 9 ||
           (k->value >= ' ' && k->value < 256 && k->value != 127))
    g_string_append_c(paste.buf, k->value);
}

//  paste_send_lines(text)
// Old behaviour: each line is sent (or run) like a typed line.
static void paste_send_lines(const char *text)
{
  gchar **lines = g_strsplit(text, "\n", -1);
  gchar **l;

  for (l = lines; *l; l++) {
    scr_insert_text(*l);
    if (*(l+1))
      readline_accept_line(FALSE);
  }
  g_strfreev(lines);
}

static void paste_insert(const char *text)
{
  if (strlen(inputLine) + strlen(text) >= INPUTLINE_LENGTH) {
    scr_LogPrint(LPRINT_NORMAL, "Pasted text is too long (%u bytes), "
                 "it has not been inserted.", (unsigned)strlen(text));
    return;
  }
  scr_insert_text(text);
}

//  paste_commit()
// End of the pasted text: insert it according to "paste_mode".
static void paste_commit(void)
{
  gchar *text;
  const char *mode;

  if (!paste.active)
    return;
  paste.active = FALSE;
  if (paste.timer) {
    g_source_remove(paste.timer);
    paste.timer = 0;
  }

  text = paste_normalize(paste.buf->str);
  g_string_truncate(paste.buf, 0);
  if (!*text) {
    g_free(text);
    return;
  }

  if (vi_mode && !chatmode && inputLine[0] != COMMAND_CHAR &&
      inputLine[0] != VI_SEARCH_COMMAND_CHAR) {
    // Don't run the text as vi commands
    scr_LogPrint(LPRINT_NORMAL, "Enter chat mode to paste text.");
    g_free(text);
    return;
  }

  mode = settings_opt_get("paste_mode");
  if (!strchr(text, '\n') || !mode || !strcmp(mode, "insert")) {
    paste_insert(text);
  } else if (!strcmp(mode, "send_lines")) {
    paste_send_lines(text);
  } else {  // "confirm"
    if (!strcmp(mode, "confirm")) {
      scr_LogPrint(LPRINT_NORMAL, "Pasted %u lines (%u bytes): "
                   "Enter = insert, s = send line by line, Esc = cancel",
                   paste_count_lines(text), (unsigned)strlen(text));
      g_free(paste.pending);
      paste.pending = text;
      text = NULL;
    } else {
      scr_LogPrint(LPRINT_NORMAL, "Invalid paste_mode value: %s", mode);
      paste_insert(text);
    }
  }
  g_free(text);
  refresh_inputline();
}

//  paste_confirm_key(kcode)
// Answer to the "confirm" question.
static void paste_confirm_key(const keycode_t *k)
{
  gchar *text = paste.pending;

  paste.pending = NULL;
  if (k->mcode == MKEY_NONE && (k->value == 13 || k->value == 'i' ||
                                k->value == 'y')) {
    paste_insert(text);
  } else if (k->mcode == MKEY_NONE && k->value == 's') {
    paste_send_lines(text);
  } else {
    scr_LogPrint(LPRINT_NORMAL, "Pasted text discarded.");
  }
  g_free(text);
  refresh_inputline();
}

//  paste_key(kcode)
// Handle bracketed paste.  Returns TRUE if the key has been consumed.
static gboolean paste_key(const keycode_t *k)
{
  gboolean special = (k->mcode == MKEY_SPEC && !k->spec.mods);

  if (special && k->spec.key == KN_PASTE_START) {
    if (!paste.active)
      paste_start(TRUE);
    return TRUE;
  }
  if (special && k->spec.key == KN_PASTE_END) {
    paste_commit();
    return TRUE;
  }
  if (paste.active) {
    paste_add_key(k);
    return TRUE;
  }
  if (paste.pending) {
    paste_confirm_key(k);
    return TRUE;
  }
  return FALSE;
}

//  burst_is_paste(keys, n)
// Without bracketed paste, a paste is detected when many keys, including
// Enter, are read at once: nobody types that fast.
static gboolean burst_is_paste(const keycode_t *keys, guint n)
{
  guint i;
  const char *opt = settings_opt_get("paste_detect");

  if (opt && !atoi(opt))
    return FALSE;
  if (n < paste_option_int("paste_burst_min", 8))
    return FALSE;
  for (i = 0; i < n; i++) {
    if (keys[i].mcode == MKEY_SPEC && keys[i].spec.key == KN_PASTE_START)
      return FALSE;   // Bracketed paste
    if (keys[i].mcode == MKEY_NONE && !keys[i].utf8 &&
        (keys[i].value == 13 || keys[i].value == 10))
      return TRUE;
  }
  return FALSE;
}

//  scr_process_input()
// Read and process all the pending keys.
void scr_process_input(void)
{
  static GArray *burst;
  keycode_t kcode;
  guint i;

  if (!burst)
    burst = g_array_new(FALSE, FALSE, sizeof(keycode_t));
  g_array_set_size(burst, 0);

  for (scr_getch(&kcode); kcode.value != ERR; scr_getch(&kcode)) {
    g_array_append_val(burst, kcode);
    if (burst->len >= 65536)
      break;
  }
  if (!burst->len)
    return;

  // A detected paste goes on as long as keys keep coming quickly
  if (paste.active && !paste.bracketed) {
    for (i = 0; i < burst->len; i++)
      paste_add_key(&g_array_index(burst, keycode_t, i));
    return;
  }
  if (!paste.active && !paste.pending &&
      burst_is_paste((keycode_t *)burst->data, burst->len)) {
    paste_start(FALSE);
    for (i = 0; i < burst->len; i++)
      paste_add_key(&g_array_index(burst, keycode_t, i));
    return;
  }

  for (i = 0; i < burst->len; i++)
    scr_process_key(g_array_index(burst, keycode_t, i));
}

//  scr_process_key(key)
// Handle the pressed key, in the command line (bottom).
void scr_process_key(keycode_t kcode)
{
  int key = kcode.value;
  int display_char = FALSE;
  int vi_search = FALSE;
  static int ex_or_search_mode = FALSE;

  if (paste_key(&kcode))
    return;

  lock_chatstate = FALSE;

  if (kcode.mcode) {
    bindcommand(kcode);
    key = ERR; // Do not process any further
  }

  if (vi_mode && !chatmode) {
    int got_cmd_prefix = FALSE;
    int unrecognized = FALSE;
    static char search_cmd[INPUTLINE_LENGTH+1] = "";

    if (key == KEY_UP || key == KEY_DOWN) {
      scr_process_vi_arrow_key(key);
      key = ERR;    // Do not process any further
    } else if (ex_or_search_mode) {
      switch (key) {
        case 27:    // Escape
            clear_inputline();
            ex_or_search_mode = FALSE;
            break;
        case 9:     // Tab
        case 353:   // Shift-Tab
            if (inputLine[0] == VI_SEARCH_COMMAND_CHAR)
              vi_search = TRUE;
            break;
        case 13:    // Enter
        case 343:   // Enter on Maemo
            if (inputLine[0] == COMMAND_CHAR) {
              {
                char *p = strchr(inputLine, 0);

                while (*--p == ' ' && p > inputLine)
                  *p = 0;
              }
              if (!strcmp(inputLine, mkcmdstr("x")) ||
                  !strcmp(inputLine, mkcmdstr("q")) ||
                  !strcmp(inputLine, mkcmdstr("wq")))
                strcpy(inputLine, mkcmdstr("quit"));
              if (isdigit((int)(unsigned char)inputLine[1]) &&
                  strlen(inputLine) <= 9) {
                process_command(mkcmdstr("roster top"), TRUE);
                memcpy(inputLine + 13, inputLine + 1, 10);
                memcpy(inputLine + 1, "roster down ", 12);
              }
              inputLine[0] = COMMAND_CHAR;
              process_command(inputLine, TRUE);
              scr_cmdhisto_addline(inputLine);
            } else if (inputLine[0] == VI_SEARCH_COMMAND_CHAR) {
              size_t cmd_len;
              size_t str_len = strlen(inputLine) - 1;

              strncpy(search_cmd, mkcmdstr("roster search "), INPUTLINE_LENGTH);
              cmd_len = strlen(search_cmd);

              if (cmd_len + str_len > INPUTLINE_LENGTH)
                return;

              memcpy(search_cmd + cmd_len, inputLine + 1, str_len + 1);

              process_command(search_cmd, TRUE);
              scr_cmdhisto_addline(search_cmd);
            } else if (inputLine[0] == 0) {
              if (buddy_gettype(BUDDATA(current_buddy)) ==
                  ROSTER_TYPE_GROUP)
                process_command(mkcmdstr("group toggle"), TRUE);
              else
                open_chat_window();
            }
            ex_or_search_mode = FALSE;
      }
    } else if (key >= '0' && key <= '9') {
      got_cmd_prefix = TRUE;
    } else if (key == COMMAND_CHAR || key == VI_SEARCH_COMMAND_CHAR) {
      ex_or_search_mode = TRUE;
      cmdhisto_cur = NULL;
    } else {
      switch (key) {
        case ' ':
            process_command(mkcmdstr("group toggle"), TRUE);
            break;
        case '!':
            {
              const char *bjid = buddy_getjid(BUDDATA(current_buddy));

              if (bjid) {
                guint type = buddy_gettype(BUDDATA(current_buddy));
                guint prio = buddy_getuiprio(BUDDATA(current_buddy));

                if (type & ROSTER_TYPE_ROOM &&
                    prio < ROSTER_UI_PRIO_MUC_HL_MESSAGE) {
                  roster_setuiprio(bjid, FALSE,
                                   ROSTER_UI_PRIO_MUC_HL_MESSAGE, prio_set);
                  roster_msg_setflag(bjid, FALSE, TRUE);
                } else if (type & ROSTER_TYPE_USER &&
                           prio < ROSTER_UI_PRIO_ATTENTION_MESSAGE) {
                  roster_setuiprio(bjid, FALSE,
                                   ROSTER_UI_PRIO_ATTENTION_MESSAGE, prio_set);
                  roster_msg_setflag(bjid, FALSE, TRUE);
                } else {
                  roster_msg_setflag(bjid, FALSE, FALSE);
                }
                scr_update_roster();
              }
            }
            break;
        case '#':
            {
              const char *bjid = buddy_getjid(BUDDATA(current_buddy));

              if (bjid) {
                unsigned short bflags = buddy_getflags(BUDDATA(current_buddy));

                if (bflags & ROSTER_FLAG_MSG)
                  roster_msg_setflag(bjid, FALSE, FALSE);
                else
                  roster_msg_setflag(bjid, FALSE, TRUE);

                scr_update_roster();
              }
            }
            break;
        case '\'':
            if (inputLine[0] == '\'')
              process_command(mkcmdstr("roster alternate"), TRUE);
            else
              got_cmd_prefix = TRUE;
            break;
        case 'A':
            process_command(mkcmdstr("roster unread_first"), TRUE);
            break;
        case 'a':
            process_command(mkcmdstr("roster unread_next"), TRUE);
            break;
        case 'F':
            process_command(mkcmdstr("roster group_prev"), TRUE);
            break;
        case 'f':
            process_command(mkcmdstr("roster group_next"), TRUE);
            break;
        case 'G':
            process_command(mkcmdstr("roster bottom"), TRUE);
            break;
        case 'g':
            if (inputLine[0] == 'g')
              process_command(mkcmdstr("roster top"), TRUE);
            else {
              clear_inputline();
              got_cmd_prefix = TRUE;
            }
            break;
        case 'i':
            open_chat_window();
            break;
        case 'j':
            if (isdigit((int)(unsigned char)inputLine[0]) &&
                strlen(inputLine) <= 9) {
              char down_cmd[24];
              snprintf (down_cmd, sizeof(down_cmd), "%.13s%.9s",
                        mkcmdstr("roster down "), inputLine);
              process_command(down_cmd, TRUE);
            } else
              process_command(mkcmdstr("roster down"), TRUE);
            break;
        case 'k':
            if (isdigit((int)(unsigned char)inputLine[0]) &&
                strlen(inputLine) <= 9) {
              char up_cmd[24];
              snprintf (up_cmd, sizeof(up_cmd), "%.11s%.9s",
                        mkcmdstr("roster up "), inputLine);
              process_command(up_cmd, TRUE);
            } else
              process_command(mkcmdstr("roster up "), TRUE);
            break;
        case 'M':
            if (inputLine[0] == 'z') {
              GSList *groups = compl_list(ROSTER_TYPE_GROUP);
              GSList *g;

              for (g = groups; g; g = g_slist_next(g)) {
                char fold_cmd[256];
                size_t cmd_len, grp_len;

                strncpy(fold_cmd, mkcmdstr("group fold "), 32);
                cmd_len = strlen(fold_cmd);
                grp_len = strlen(g->data);

                if (cmd_len + grp_len + 1 > sizeof(fold_cmd))
                  continue;
                memcpy(fold_cmd + cmd_len, g->data, grp_len + 1);
                process_command(fold_cmd, TRUE);
                g_free(g->data);
              }
              g_slist_free(groups);
            } else
              unrecognized = TRUE;
            break;
        case 'n':
            process_command(search_cmd, TRUE);
            break;
        case 'O':
            process_command(mkcmdstr("roster unread_first"), TRUE);
            open_chat_window();
            break;
        case 'o':
            process_command(mkcmdstr("roster unread_next"), TRUE);
            open_chat_window();
            break;
        case 'R':
            if (inputLine[0] == 'z') {
              GSList *groups = compl_list(ROSTER_TYPE_GROUP);
              GSList *g;

              for (g = groups; g; g = g_slist_next(g)) {
                char fold_cmd[256];
                size_t cmd_len, grp_len;

                strncpy(fold_cmd, mkcmdstr("group unfold "), 32);
                cmd_len = strlen(fold_cmd);
                grp_len = strlen(g->data);

                if (cmd_len + grp_len + 1 > sizeof(fold_cmd))
                  continue;
                memcpy(fold_cmd + cmd_len, g->data, grp_len + 1);
                process_command(fold_cmd, TRUE);
                g_free(g->data);
              }
              g_slist_free(groups);
            } else
              unrecognized = TRUE;
            break;
        case 'Z':
            if (inputLine[0] == 'Z')
              process_command(mkcmdstr("quit"), TRUE);
            else {
              clear_inputline();
              got_cmd_prefix = TRUE;
            }
            break;
        case 'z':
            clear_inputline();
            got_cmd_prefix = TRUE;
            break;
        case 13:    // Enter
        case 343:   // Enter on Maemo
            if (inputLine[0] == 0) {
              if (buddy_gettype(BUDDATA(current_buddy)) == ROSTER_TYPE_GROUP)
                process_command(mkcmdstr("group toggle"), TRUE);
              else
                open_chat_window();
            }
            break;
        default:
            unrecognized = TRUE;
            break;
      }
      cmdhisto_cur = NULL;
    }
    if (!ex_or_search_mode && !got_cmd_prefix) {
      clear_inputline();
      if (!unrecognized)
        key = ERR;  // Do not process any further
    }
    lock_chatstate = TRUE;
  }

  if (kcode.utf8) {
    if (key != ERR && !kcode.mcode)
      display_char = TRUE;
    goto display;
  }

  switch (key) {
    case 0:
    case ERR:
        break;
    case 9:     // Tab
        if (!vi_search)
          readline_do_completion(TRUE);   // Forward-completion
        break;
    case 353:   // Shift-Tab
        if (!vi_search)
          readline_do_completion(FALSE);  // Backward-completion
        break;
    case 13:    // Enter
    case 343:   // Enter on Maemo
        readline_accept_line(FALSE);
        break;
    case 3:     // Ctrl-C
        scr_handle_CtrlC();
        break;
    case KEY_RESIZE:
        scr_resize();
        break;
    default:
        display_char = TRUE;
  } // switch

display:
  if (display_char) {
    guint printable;

    if (kcode.utf8) {
      printable = iswprint(key);
    } else {
#ifdef __CYGWIN__
      printable = (isprint(key) || (key >= 161 && key <= 255))
                  && !is_speckey(key);
#else
      printable = isprint(key) && !is_speckey(key);
#endif
    }
    if (printable) {
      char tmpLine[INPUTLINE_LENGTH+1];

      // Check the line isn't too long
      if (strlen(inputLine) + 4 > INPUTLINE_LENGTH)
        return;

      // Insert char
      strcpy(tmpLine, ptr_inputline);
      ptr_inputline = put_char(ptr_inputline, key);
      strcpy(ptr_inputline, tmpLine);
    } else {
      // Look for a key binding.
      if (!kcode.utf8)
        bindcommand(kcode);
    }
  }

  if (completion_started && key != 9 && key != 353 && key != KEY_RESIZE)
    scr_end_current_completion();
  refresh_inputline();

  if (ex_or_search_mode &&
      inputLine[0] != COMMAND_CHAR && inputLine[0] != VI_SEARCH_COMMAND_CHAR)
    ex_or_search_mode = FALSE;

  if (!lock_chatstate) {
    // Set chat state to composing (1) if the user is currently composing,
    // i.e. not an empty line and not a command line.
    if (inputLine[0] == 0 || inputLine[0] == COMMAND_CHAR)
      set_chatstate(0);
    else
      set_chatstate(1);
    if (chatstate)
      time(&chatstate_timestamp);
  }
  return;
}

#if defined(WITH_ENCHANT) || defined(WITH_ASPELL)
static void spell_checker_free(gpointer data)
{
  spell_checker_t *sc = data;
#ifdef WITH_ENCHANT
  enchant_broker_free_dict(sc->broker, sc->checker);
  enchant_broker_free(sc->broker);
#endif
#ifdef WITH_ASPELL
  delete_aspell_speller(sc->checker);
  delete_aspell_config(sc->config);
#endif
  g_free(sc);
}

static spell_checker_t* new_spell_checker(const char* spell_lang)
{
  spell_checker_t *sc = g_new(spell_checker_t, 1);
#ifdef WITH_ASPELL
  const char *spell_encoding = settings_opt_get("spell_encoding");
  AspellCanHaveError *possible_err;
  sc->config = new_aspell_config();
  if (spell_encoding)
    aspell_config_replace(sc->config, "encoding", spell_encoding);
  aspell_config_replace(sc->config, "lang", spell_lang);
  possible_err = new_aspell_speller(sc->config);

  if (aspell_error_number(possible_err) != 0) {
    delete_aspell_config(sc->config);
    g_free(sc);
    sc = NULL;
  } else {
    sc->checker = to_aspell_speller(possible_err);
  }
#endif
#ifdef WITH_ENCHANT
  sc->broker = enchant_broker_init();
  sc->checker = enchant_broker_request_dict(sc->broker, spell_lang);
  if (!sc->checker) {
    enchant_broker_free(sc->broker);
    g_free(sc);
    sc = NULL;
  }
#endif
  return sc;
}

// initialization
void spellcheck_init(void)
{
  int spell_enable            = settings_opt_get_int("spell_enable");
  const char *spell_lang     = settings_opt_get("spell_lang");
  gchar **langs;
  gchar **lang_iter;
  spell_checker_t *sc;

  if (!spell_enable)
    return;

  spellcheck_deinit();

  if (!spell_lang) { // Cannot initialize: language not specified
    scr_LogPrint(LPRINT_LOGNORM, "Error: Cannot initialize spell checker, language not specified.");
    scr_LogPrint(LPRINT_LOGNORM, "Please set the 'spell_lang' variable.");
    return;
  }

  langs = g_strsplit(spell_lang, " ", -1);
  for (lang_iter = langs; *lang_iter; ++lang_iter) {
    if (**lang_iter) { // Skip empty strings
      sc = new_spell_checker(*lang_iter);
      if (sc) {
        spell_checkers = g_slist_append(spell_checkers, sc);
      } else {
        scr_LogPrint(LPRINT_LOGNORM,
                     "Warning: Could not load spell checker language '%s'.",
                     *lang_iter);
      }
    }
  }
  g_strfreev(langs);
}

// Deinitialization of spellchecker
void spellcheck_deinit(void)
{
  g_slist_foreach (spell_checkers, (GFunc) spell_checker_free, NULL);
  g_slist_free (spell_checkers);
  spell_checkers = NULL;
}

typedef struct {
  const char* str;
  int len;
} spell_substring_t;

static int spellcheckword(gconstpointer sc_ptr, gconstpointer substr_ptr)
{
  spell_checker_t *sc = (spell_checker_t*) sc_ptr;
  spell_substring_t *substr = (spell_substring_t*) substr_ptr;
#ifdef WITH_ENCHANT
  // enchant_dict_check will return 0 on good word
  return enchant_dict_check(sc->checker, substr->str, substr->len);
#endif
#ifdef WITH_ASPELL
  // aspell_speller_check will return 1 on good word, so we need to make it 0
  return aspell_speller_check(sc->checker, substr->str, substr->len) - 1;
#endif
  return 0; // Keep compiler happy
}

#define spell_isalpha(c) (utf8_mode ? iswalpha(get_char(c)) : isalpha(*c))

// Spell checking function
static void spellcheck(char *line, char *checked)
{
  const char *start, *line_start;
  spell_substring_t substr;

  if (inputLine[0] == 0 || inputLine[0] == COMMAND_CHAR)
    return;

  // Give up early if not languages are loaded
  if (!spell_checkers)
    return;

  line_start = line;

  while (*line) {

    if (!spell_isalpha(line)) {
      line = next_char(line);
      continue;
    }

    if (!strncmp(line, "http://", 7)) {
      line += 7; // : and / characters are 1 byte long in utf8, right?

      while (!strchr(" \t\r\n", *line))
        line = next_char(line); // i think line++ would be fine here?

      continue;
    }

    if (!strncmp(line, "ftp://", 6)) {
      line += 6;

      while (!strchr(" \t\r\n", *line))
        line = next_char(line);

      continue;
    }

    start = line;

    while (spell_isalpha(line))
      line = next_char(line);

    substr.str = start;
    substr.len = line - start;
    if (!g_slist_find_custom(spell_checkers, &substr, spellcheckword))
      memset(&checked[start - line_start], SPELLBADCHAR, line - start);
  }
}
#endif

static void open_chat_window(void)
{
  last_activity_buddy = current_buddy;
  scr_check_auto_away(TRUE);
  scr_set_chatmode(TRUE);
  scr_show_buddy_window();
}

static void clear_inputline(void)
{
  ptr_inputline = inputLine;
  *ptr_inputline = 0;
}

/* vim: set et cindent cinoptions=>2\:2(0 ts=2 sw=2:  For Vim users... */
