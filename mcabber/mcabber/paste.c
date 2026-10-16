/*
 * paste.c      -- Pasted text helpers
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

#include "paste.h"
#include "utf8.h"

#define TAB_WIDTH 8

gchar *paste_normalize(const char *text)
{
  GString *out = g_string_sized_new(strlen(text));
  const char *p = text;
  guint col = 0;

  while (*p) {
    unsigned char c = *p;
    if (c == '\r' || c == '\n') {
      if (c == '\r' && p[1] == '\n')
        p++;
      g_string_append_c(out, '\n');
      col = 0;
      p++;
    } else if (c == '\t') {
      do {
        g_string_append_c(out, ' ');
      } while (++col % TAB_WIDTH);
      p++;
    } else if (c < ' ' || c == 127) {
      p++;  // Other control characters are dropped
    } else {
      const char *next = next_char((char *)p);
      g_string_append_len(out, p, next - p);
      col++;
      p = next;
    }
  }
  while (out->len && out->str[out->len - 1] == '\n')
    g_string_truncate(out, out->len - 1);
  return g_string_free(out, FALSE);
}

guint paste_count_lines(const char *text)
{
  guint n = 1;
  for ( ; *text; text++)
    if (*text == '\n')
      n++;
  return n;
}

/* vim: set et cindent cinoptions=>2\:2(0 ts=2 sw=2:  For Vim users... */
