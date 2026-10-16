/*
 * inputlayout.c -- Layout of the input buffer on the screen
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

#include "inputlayout.h"
#include "utf8.h"

void il_layout_init(il_layout_t *l)
{
  l->row_start = g_array_new(FALSE, FALSE, sizeof(gsize));
  l->row_end = g_array_new(FALSE, FALSE, sizeof(gsize));
  l->rows = l->cursor_row = l->cursor_col = 0;
}

void il_layout_free(il_layout_t *l)
{
  g_array_free(l->row_start, TRUE);
  g_array_free(l->row_end, TRUE);
  l->row_start = l->row_end = NULL;
}

// Screen width of the character at p (control characters use one cell)
static guint char_cells(const char *p)
{
  int w = get_char_width(p);
  return w < 0 ? 1 : (guint)w;
}

void il_layout(const char *buf, gsize cursor, guint width, il_layout_t *l)
{
  const char *p = buf;
  gsize start = 0;
  guint col = 0;
  gboolean cursor_set = FALSE;

  g_array_set_size(l->row_start, 0);
  g_array_set_size(l->row_end, 0);
  if (width < 1)
    width = 1;

  for (;;) {
    gsize off = p - buf;
    guint cw = 0;

    if (*p && *p != '\n') {
      cw = char_cells(p);
      // Soft wrap: the character goes to the next row
      if (col > 0 && col + cw > width) {
        g_array_append_val(l->row_start, start);
        g_array_append_val(l->row_end, off);
        start = off;
        col = 0;
      }
    }
    if (off == cursor && !cursor_set) {
      // A cursor at the end of a full row goes to the next row
      if (col >= width && (!*p || *p == '\n')) {
        g_array_append_val(l->row_start, start);
        g_array_append_val(l->row_end, off);
        start = off;
        col = 0;
      }
      l->cursor_row = l->row_start->len;
      l->cursor_col = col;
      cursor_set = TRUE;
    }
    if (!*p)
      break;
    if (*p == '\n') {
      g_array_append_val(l->row_start, start);
      g_array_append_val(l->row_end, off);
      p++;
      start = p - buf;
      col = 0;
      continue;
    }
    col += cw;
    p = next_char((char *)p);
  }
  g_array_append_val(l->row_start, start);
  {
    gsize end = p - buf;
    g_array_append_val(l->row_end, end);
  }
  l->rows = l->row_start->len;
  if (!cursor_set) {
    l->cursor_row = l->rows - 1;
    l->cursor_col = col;
  }
}

gsize il_offset_at(const char *buf, const il_layout_t *l, guint row,
                   guint col)
{
  const char *p, *end;
  guint c = 0;

  if (row >= l->rows)
    row = l->rows - 1;
  p = buf + il_row_start(l, row);
  end = buf + il_row_end(l, row);
  while (p < end) {
    guint cw = char_cells(p);
    if (c + cw > col)
      break;
    c += cw;
    p = next_char((char *)p);
  }
  return p - buf;
}

/* vim: set et cindent cinoptions=>2\:2(0 ts=2 sw=2:  For Vim users... */
