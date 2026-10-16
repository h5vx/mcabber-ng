/*
 * style.c      -- Colors and attributes
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

#include <stdlib.h>
#include <string.h>

#include "style.h"

GQuark style_error_quark(void)
{
  return g_quark_from_static_string("mcabber-style");
}

static const char *color_names[] = {
  "black", "red", "green", "yellow", "blue", "magenta", "cyan", "white", NULL
};

static const struct {
  const char *name;
  guint attr;
} attr_names[] = {
  { "bold", STYLE_BOLD },           { "dim", STYLE_DIM },
  { "italic", STYLE_ITALIC },       { "underline", STYLE_UNDERLINE },
  { "reverse", STYLE_REVERSE },     { "blink", STYLE_BLINK },
  { "standout", STYLE_STANDOUT },   { "normal", 0 },
  { NULL, 0 }
};

gboolean style_parse_color(const char *s, style_color_t *c, GError **err)
{
  int i;
  char *end;

  if (!s || !*s)
    goto invalid;
  if (!g_ascii_strcasecmp(s, "default") || !g_ascii_strcasecmp(s, "none")) {
    c->kind = STYLE_COLOR_DEFAULT;
    c->value = 0;
    return TRUE;
  }
  if (*s == '#') {
    if (strlen(s) != 7)
      goto invalid;
    c->value = strtoul(s + 1, &end, 16);
    if (*end)
      goto invalid;
    c->kind = STYLE_COLOR_RGB;
    return TRUE;
  }
  if (g_ascii_isdigit(*s)) {
    long n = strtol(s, &end, 10);
    if (*end || n < 0 || n > 255)
      goto invalid;
    c->kind = STYLE_COLOR_INDEX;
    c->value = n;
    return TRUE;
  }
  {
    gboolean bright = FALSE;
    if (!g_ascii_strncasecmp(s, "bright", 6)) {
      bright = TRUE;
      s += 6;
    }
    for (i = 0; color_names[i]; i++) {
      if (!g_ascii_strcasecmp(s, color_names[i])) {
        c->kind = STYLE_COLOR_INDEX;
        c->value = i + (bright ? 8 : 0);
        return TRUE;
      }
    }
  }
invalid:
  g_set_error(err, STYLE_ERROR, 0, "invalid color \"%s\"", s ? s : "");
  return FALSE;
}

gboolean style_parse(const char *spec, style_spec_t *out, GError **err)
{
  gchar **tokens, **t;
  style_spec_t st;
  gboolean on = FALSE, ok = TRUE;

  memset(&st, 0, sizeof(st));
  tokens = g_strsplit_set(spec ? spec : "", " \t,", -1);
  for (t = tokens; *t && ok; t++) {
    const char *tok = *t;
    int i;
    gboolean found = FALSE;

    if (!*tok)
      continue;
    if (!g_ascii_strcasecmp(tok, "on")) {
      on = TRUE;
      continue;
    }
    for (i = 0; attr_names[i].name; i++) {
      if (!g_ascii_strcasecmp(tok, attr_names[i].name)) {
        st.attrs |= attr_names[i].attr;
        found = TRUE;
        break;
      }
    }
    if (found)
      continue;
    if (!g_ascii_strncasecmp(tok, "fg=", 3)) {
      ok = style_parse_color(tok + 3, &st.fg, err);
    } else if (!g_ascii_strncasecmp(tok, "bg=", 3)) {
      ok = style_parse_color(tok + 3, &st.bg, err);
    } else if (on) {
      ok = style_parse_color(tok, &st.bg, err);
      on = FALSE;
    } else if (st.fg.kind == STYLE_COLOR_UNSET) {
      ok = style_parse_color(tok, &st.fg, err);
    } else {
      g_set_error(err, STYLE_ERROR, 0, "unexpected \"%s\" in style", tok);
      ok = FALSE;
    }
  }
  g_strfreev(tokens);
  if (ok)
    *out = st;
  return ok;
}

/* -- Palette -- */

static const guint32 ansi16[16] = {
  0x000000, 0xcd0000, 0x00cd00, 0xcdcd00, 0x0000ee, 0xcd00cd, 0x00cdcd,
  0xe5e5e5, 0x7f7f7f, 0xff0000, 0x00ff00, 0xffff00, 0x5c5cff, 0xff00ff,
  0x00ffff, 0xffffff
};

static const guint8 cube_levels[6] = { 0, 95, 135, 175, 215, 255 };

guint32 style_palette_rgb(guint index)
{
  if (index < 16)
    return ansi16[index];
  if (index < 232) {
    guint i = index - 16;
    return (cube_levels[i / 36] << 16) | (cube_levels[(i / 6) % 6] << 8) |
           cube_levels[i % 6];
  }
  if (index < 256) {
    guint v = 8 + (index - 232) * 10;
    return (v << 16) | (v << 8) | v;
  }
  return 0;
}

static guint32 rgb_dist(guint32 a, guint32 b)
{
  int dr = (int)((a >> 16) & 0xff) - (int)((b >> 16) & 0xff);
  int dg = (int)((a >> 8) & 0xff) - (int)((b >> 8) & 0xff);
  int db = (int)(a & 0xff) - (int)(b & 0xff);
  // Weighted distance (the eye is more sensitive to green)
  return 2*dr*dr + 4*dg*dg + 3*db*db;
}

static guint nearest(guint32 rgb, guint first, guint last)
{
  guint i, best = first;
  guint32 bestd = G_MAXUINT32;
  for (i = first; i <= last; i++) {
    guint32 d = rgb_dist(rgb, style_palette_rgb(i));
    if (d < bestd) {
      bestd = d;
      best = i;
    }
  }
  return best;
}

guint style_rgb_to_256(guint32 rgb)
{
  // The 16 first colors are often redefined by the terminal theme
  return nearest(rgb, 16, 255);
}

guint style_rgb_to_16(guint32 rgb)
{
  return nearest(rgb, 0, 15);
}

/* vim: set et cindent cinoptions=>2\:2(0 ts=2 sw=2:  For Vim users... */
