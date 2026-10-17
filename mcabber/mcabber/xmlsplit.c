/*
 * xmlsplit.c   -- Raw XML helpers for the XMPP console
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

#include "xmlsplit.h"

//  tag_end(p)
// End of the tag beginning at p ('<'), quotes are skipped.  Returns the
// position after '>', or the end of the string.
static const char *tag_end(const char *p)
{
  char quote = 0;
  for (p++; *p; p++) {
    if (quote) {
      if (*p == quote)
        quote = 0;
    } else if (*p == '"' || *p == '\'') {
      quote = *p;
    } else if (*p == '>') {
      return p + 1;
    }
  }
  return p;
}

gchar **xml_split_stanzas(const char *data)
{
  GPtrArray *parts = g_ptr_array_new();
  const char *p = data, *start = data;
  int depth = 0;

  while (*p) {
    const char *end;
    gboolean boundary = FALSE;

    if (*p != '<') {
      p++;
      continue;
    }
    end = tag_end(p);
    // Each chunk of data is split separately: stanzas are top-level
    // elements (the stream element is never closed in a chunk).
    if (p[1] == '/') {                          // Closing tag
      if (depth > 0)
        depth--;
      boundary = (depth == 0);
    } else if (p[1] == '?' || p[1] == '!') {    // Declaration, comment
      boundary = (depth == 0);
    } else if (end - p >= 2 && end[-2] == '/') {  // Empty element
      boundary = (depth == 0);
    } else if (!strncmp(p + 1, "stream:stream", 13)) {
      boundary = TRUE;
    } else {                                    // Opening tag
      depth++;
    }
    p = end;
    if (boundary) {
      gchar *part = g_strstrip(g_strndup(start, p - start));
      if (*part)
        g_ptr_array_add(parts, part);
      else
        g_free(part);
      start = p;
    }
  }
  if (*start) {
    gchar *part = g_strstrip(g_strdup(start));
    if (*part)
      g_ptr_array_add(parts, part);
    else
      g_free(part);
  }
  g_ptr_array_add(parts, NULL);
  return (gchar **)g_ptr_array_free(parts, FALSE);
}

gchar *xml_mask_secrets(const char *xml)
{
  static const char *names[] = { "auth", "response", "password", NULL };
  GString *res = g_string_new(xml);
  int i;

  for (i = 0; names[i]; i++) {
    gchar *open = g_strdup_printf("<%s", names[i]);
    gchar *close = g_strdup_printf("</%s>", names[i]);
    gsize from = 0;
    for (;;) {
      char *o = strstr(res->str + from, open);
      char *gt, *c;
      char next;
      if (!o)
        break;
      next = o[strlen(open)];
      if (next != '>' && next != ' ' && next != '\t' && next != '\n' &&
          next != '/') {
        from = o - res->str + 1;
        continue;
      }
      gt = strchr(o, '>');
      if (!gt || gt[-1] == '/')
        break;
      c = strstr(gt, close);
      if (!c)
        break;
      if (c > gt + 1) {
        gsize pos = gt + 1 - res->str;
        g_string_erase(res, pos, c - gt - 1);
        g_string_insert(res, pos, "***");
        from = pos + 3;
      } else {
        from = c - res->str;
      }
    }
    g_free(open);
    g_free(close);
  }
  return g_string_free(res, FALSE);
}
