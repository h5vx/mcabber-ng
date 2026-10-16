/*
 * fmt.c        -- Templates with styles
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

#include "fmt.h"
#include "utf8.h"

GQuark fmt_error_quark(void)
{
  return g_quark_from_static_string("mcabber-fmt");
}

/* -- Styled strings -- */

sstr_t *sstr_new(void)
{
  sstr_t *s = g_new0(sstr_t, 1);
  s->text = g_string_new(NULL);
  s->runs = g_array_new(FALSE, FALSE, sizeof(srun_t));
  return s;
}

void sstr_free(sstr_t *s)
{
  if (!s)
    return;
  g_string_free(s->text, TRUE);
  g_array_free(s->runs, TRUE);
  g_free(s);
}

void sstr_clear(sstr_t *s)
{
  g_string_truncate(s->text, 0);
  g_array_set_size(s->runs, 0);
  s->width = 0;
}

// Screen width of a character (control characters use one cell)
static guint char_cells(const char *p)
{
  int w = get_char_width(p);
  return w < 0 ? 1 : (guint)w;
}

guint fmt_text_width(const char *text, gssize len)
{
  const char *p = text, *end = len < 0 ? text + strlen(text) : text + len;
  guint w = 0;

  while (p < end && *p) {
    w += char_cells(p);
    p = next_char((char *)p);
  }
  return w;
}

void sstr_append(sstr_t *s, const char *text, gssize len, const char *style)
{
  srun_t run;

  if (len < 0)
    len = strlen(text);
  if (!len)
    return;
  style = style ? g_intern_string(style) : NULL;
  // Merge with the previous run if the style is the same
  if (s->runs->len) {
    srun_t *last = &g_array_index(s->runs, srun_t, s->runs->len - 1);
    if (last->style == style && last->start + last->len == s->text->len) {
      last->len += len;
      g_string_append_len(s->text, text, len);
      s->width += fmt_text_width(text, len);
      return;
    }
  }
  run.start = s->text->len;
  run.len = len;
  run.style = style;
  g_array_append_val(s->runs, run);
  g_string_append_len(s->text, text, len);
  s->width += fmt_text_width(text, len);
}

/* -- Templates -- */

typedef enum {
  OP_TEXT,
  OP_FIELD,
  OP_IF,
  OP_IFNOT,
  OP_END,
  OP_STYLE,
  OP_RIGHT
} optype_t;

typedef struct {
  optype_t type;
  gchar *str;       // Text, field name or style name
  gchar *arg;       // Field argument
  char align;       // '<', '>', '^' or 0
  int width, max;   // -1 when not set
  gboolean flex;
  guint jump;       // OP_IF/OP_IFNOT: index of the matching OP_END
} op_t;

struct fmt {
  GArray *ops;
};

static void free_ops(GArray *ops)
{
  guint i;
  for (i = 0; i < ops->len; i++) {
    op_t *op = &g_array_index(ops, op_t, i);
    g_free(op->str);
    g_free(op->arg);
  }
  g_array_free(ops, TRUE);
}

void fmt_free(fmt_t *f)
{
  if (!f)
    return;
  free_ops(f->ops);
  g_free(f);
}

static void add_text(GArray *ops, GString *text)
{
  op_t op = { OP_TEXT, NULL, NULL, 0, -1, -1, FALSE, 0 };
  if (!text->len)
    return;
  op.str = g_strndup(text->str, text->len);
  g_array_append_val(ops, op);
  g_string_truncate(text, 0);
}

//  parse_spec(spec, op)
// "[<>^][width][.max]" or "*"; anything else is an argument.
static void parse_spec(const char *spec, op_t *op)
{
  const char *p = spec;
  char *e;

  if (!strcmp(spec, "*")) {
    op->flex = TRUE;
    return;
  }
  if (*p == '<' || *p == '>' || *p == '^')
    p++;
  while (g_ascii_isdigit(*p))
    p++;
  if (*p == '.' && g_ascii_isdigit(p[1])) {
    p++;
    while (g_ascii_isdigit(*p))
      p++;
  }
  if (*p || p == spec) {
    op->arg = g_strdup(spec);
    return;
  }
  p = spec;
  if (*p == '<' || *p == '>' || *p == '^')
    op->align = *p++;
  if (g_ascii_isdigit(*p)) {
    op->width = strtol(p, &e, 10);
    p = e;
  }
  if (*p == '.')
    op->max = strtol(p + 1, NULL, 10);
}

fmt_t *fmt_compile(const char *tmpl, GError **err)
{
  GArray *ops = g_array_new(FALSE, FALSE, sizeof(op_t));
  GArray *stack = g_array_new(FALSE, FALSE, sizeof(guint));
  GString *text = g_string_new(NULL);
  const char *p = tmpl;
  fmt_t *f;

  while (*p) {
    const char *close;
    gchar *in;
    op_t op = { OP_TEXT, NULL, NULL, 0, -1, -1, FALSE, 0 };

    if (*p == '}') {
      g_string_append_c(text, '}');
      p += (p[1] == '}') ? 2 : 1;
      continue;
    }
    if (*p != '{') {
      g_string_append_c(text, *p++);
      continue;
    }
    if (p[1] == '{') {
      g_string_append_c(text, '{');
      p += 2;
      continue;
    }
    close = strchr(p, '}');
    if (!close) {
      g_set_error(err, FMT_ERROR, 0, "unclosed '{' at position %d",
                  (int)(p - tmpl) + 1);
      goto error;
    }
    add_text(ops, text);
    in = g_strndup(p + 1, close - p - 1);
    p = close + 1;

    if (!strcmp(in, "/")) {
      guint start;
      g_free(in);
      if (!stack->len) {
        g_set_error(err, FMT_ERROR, 0, "{/} without {?...} or {!...}");
        goto error;
      }
      start = g_array_index(stack, guint, stack->len - 1);
      g_array_set_size(stack, stack->len - 1);
      g_array_index(ops, op_t, start).jump = ops->len;
      op.type = OP_END;
      g_array_append_val(ops, op);
      continue;
    }
    if (*in == '?' || *in == '!') {
      guint idx = ops->len;
      op.type = (*in == '?') ? OP_IF : OP_IFNOT;
      op.str = g_strdup(in + 1);
      g_free(in);
      g_array_append_val(ops, op);
      g_array_append_val(stack, idx);
      continue;
    }
    if (*in == '#') {
      op.type = OP_STYLE;
      if (in[1])
        op.str = g_strdup(in + 1);
      g_free(in);
      g_array_append_val(ops, op);
      continue;
    }
    if (!strcmp(in, ">")) {
      op.type = OP_RIGHT;
      g_free(in);
      g_array_append_val(ops, op);
      continue;
    }
    {
      char *colon = strchr(in, ':');
      op.type = OP_FIELD;
      if (colon) {
        *colon = 0;
        parse_spec(colon + 1, &op);
      }
      if (!*in) {
        g_free(op.arg);
        g_free(in);
        g_set_error(err, FMT_ERROR, 0, "empty field name");
        goto error;
      }
      op.str = in;
      g_array_append_val(ops, op);
    }
  }
  add_text(ops, text);
  if (stack->len) {
    g_set_error(err, FMT_ERROR, 0, "missing {/}");
    goto error;
  }
  g_array_free(stack, TRUE);
  g_string_free(text, TRUE);
  f = g_new0(fmt_t, 1);
  f->ops = ops;
  return f;

error:
  free_ops(ops);
  g_array_free(stack, TRUE);
  g_string_free(text, TRUE);
  return NULL;
}

/* -- Rendering -- */

typedef struct {
  gchar *text;
  const char *style;
  gboolean flex;
  gboolean right;   // Position of {>}
} piece_t;

//  truncate_cells(text, cells, ellipsis)
// Truncate the text to the given number of cells.
static gchar *truncate_cells(const char *text, guint cells, gboolean ellipsis)
{
  const char *p = text;
  const char *ell = utf8_mode ? "\xe2\x80\xa6" : ".";   // "…"
  guint w = 0;

  if (fmt_text_width(text, -1) <= cells)
    return g_strdup(text);
  if (ellipsis) {
    if (!cells)
      return g_strdup("");
    cells--;
  }
  while (*p) {
    guint cw = char_cells(p);
    if (w + cw > cells)
      break;
    w += cw;
    p = next_char((char *)p);
  }
  if (ellipsis) {
    gchar *head = g_strndup(text, p - text);
    gchar *res = g_strconcat(head, ell, NULL);
    g_free(head);
    return res;
  }
  return g_strndup(text, p - text);
}

static gchar *format_value(const op_t *op, gchar *value)
{
  guint w;
  int pad;
  gchar *v = value;

  if (op->max >= 0) {
    v = truncate_cells(value, op->max, FALSE);
    g_free(value);
  }
  if (op->width < 0)
    return v;
  w = fmt_text_width(v, -1);
  pad = op->width - (int)w;
  if (pad <= 0)
    return v;
  {
    GString *s = g_string_new(NULL);
    int left = 0, right = 0;
    switch (op->align) {
      case '>': left = pad; break;
      case '^': left = pad / 2; right = pad - left; break;
      default:  right = pad;
    }
    g_string_append_printf(s, "%*s%s%*s", left, "", v, right, "");
    g_free(v);
    return g_string_free(s, FALSE);
  }
}

static gboolean is_set(const char *v)
{
  return v && *v && strcmp(v, "0");
}

void fmt_render(const fmt_t *f, fmt_getter_t getter, gpointer data,
                guint width, sstr_t *out)
{
  GArray *pieces = g_array_new(FALSE, FALSE, sizeof(piece_t));
  const char *style = NULL;
  guint i, total = 0, flexw = 0;
  int right_at = -1;

  for (i = 0; i < f->ops->len; i++) {
    const op_t *op = &g_array_index(f->ops, op_t, i);
    piece_t pc = { NULL, style, FALSE, FALSE };

    switch (op->type) {
      case OP_TEXT:
          pc.text = g_strdup(op->str);
          break;
      case OP_FIELD:
        {
          gchar *v = getter(op->str, op->arg, data);
          if (!v)
            v = g_strdup("");
          pc.text = format_value(op, v);
          pc.flex = op->flex;
        }
          break;
      case OP_IF:
      case OP_IFNOT:
        {
          gchar *v = getter(op->str, NULL, data);
          gboolean set = is_set(v);
          g_free(v);
          if (set != (op->type == OP_IF))
            i = op->jump;   // Skip to the matching {/}
        }
          continue;
      case OP_STYLE:
          style = op->str ? g_intern_string(op->str) : NULL;
          continue;
      case OP_RIGHT:
          if (right_at < 0)
            right_at = pieces->len;
          continue;
      case OP_END:
          continue;
    }
    g_array_append_val(pieces, pc);
  }

  for (i = 0; i < pieces->len; i++) {
    piece_t *pc = &g_array_index(pieces, piece_t, i);
    guint w = fmt_text_width(pc->text, -1);
    total += w;
    if (pc->flex)
      flexw += w;
  }

  if (width) {
    if (total > width && flexw) {
      // Shrink the flexible fields
      guint fixed = total - flexw;
      guint avail = width > fixed ? width - fixed : 0;
      for (i = 0; i < pieces->len; i++) {
        piece_t *pc = &g_array_index(pieces, piece_t, i);
        if (pc->flex) {
          guint w = fmt_text_width(pc->text, -1);
          gchar *t = truncate_cells(pc->text, MIN(w, avail), TRUE);
          avail -= MIN(fmt_text_width(t, -1), avail);
          g_free(pc->text);
          pc->text = t;
        }
      }
      total = 0;
      for (i = 0; i < pieces->len; i++)
        total += fmt_text_width(g_array_index(pieces, piece_t, i).text, -1);
    }
    if (right_at >= 0 && total < width) {
      // Push the text after {>} to the right
      const char *st = right_at > 0 ?
        g_array_index(pieces, piece_t, right_at - 1).style : NULL;
      piece_t pad = { NULL, st, FALSE, TRUE };
      pad.text = g_strnfill(width - total, ' ');
      g_array_insert_val(pieces, right_at, pad);
    }
  }

  for (i = 0; i < pieces->len; i++) {
    piece_t *pc = &g_array_index(pieces, piece_t, i);
    sstr_append(out, pc->text, -1, pc->style);
    g_free(pc->text);
  }
  g_array_free(pieces, TRUE);
}

/* vim: set et cindent cinoptions=>2\:2(0 ts=2 sw=2:  For Vim users... */
