/*
 * links.c          -- Find links in messages, label them
 *
 * Copyright (C) 2026 mcabber-ng contributors
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

#include "links.h"

#define LINK_REGEX \
  "\\b(?:(?:https?|ftps?|sftp|gemini|gopher|file)://" \
  "|(?:xmpp|mailto|magnet|tel|geo):" \
  "|www\\.)[^\\s<>\"]+"

//  trim_end(text, start, end)
// Remove the punctuation that ends a sentence rather than the link, and
// closing brackets that are not opened in the link.
static gsize trim_end(const char *text, gsize start, gsize end)
{
  static const char *pairs[] = { "()", "[]", "{}" };

  while (end > start) {
    char c = text[end - 1];
    gboolean strip = FALSE;
    guint i;

    if (strchr(".,;:!?'*", c)) {
      strip = TRUE;
    } else {
      for (i = 0; i < G_N_ELEMENTS(pairs); i++) {
        if (c == pairs[i][1]) {
          int depth = 0;
          gsize j;
          for (j = start; j < end; j++) {
            if (text[j] == pairs[i][0])
              depth++;
            else if (text[j] == pairs[i][1])
              depth--;
          }
          strip = depth < 0;
        }
      }
    }
    if (!strip)
      break;
    end--;
  }
  return end;
}

GArray *links_find(const char *text)
{
  static GRegex *re;
  GArray *links = g_array_new(FALSE, FALSE, sizeof(link_span_t));
  GMatchInfo *mi = NULL;

  if (!text)
    return links;
  if (!re)
    re = g_regex_new(LINK_REGEX, G_REGEX_CASELESS | G_REGEX_OPTIMIZE, 0,
                     NULL);

  g_regex_match(re, text, 0, &mi);
  while (g_match_info_matches(mi)) {
    gint s, e;
    if (g_match_info_fetch_pos(mi, 0, &s, &e)) {
      link_span_t span = { (gsize)s, trim_end(text, s, e) };
      gsize len = span.end - span.start;
      const char *colon = memchr(text + s, ':', len);
      const char *slashes = g_strstr_len(text + s, len, "://");
      // A scheme alone ("xmpp:", "https://") or "www" is not a link
      if (!(colon && colon + 1 == text + span.end) &&
          !(slashes && slashes + 3 == text + span.end) &&
          len > 4)
        g_array_append_val(links, span);
    }
    g_match_info_next(mi, NULL);
  }
  g_match_info_free(mi);
  return links;
}

gchar *links_url(const char *text, const link_span_t *span)
{
  gchar *url = g_strndup(text + span->start, span->end - span->start);
  if (!g_ascii_strncasecmp(url, "www.", 4)) {
    gchar *full = g_strconcat("https://", url, NULL);
    g_free(url);
    url = full;
  }
  return url;
}

gchar **links_labels(const char *chars, guint n)
{
  GPtrArray *queue = g_ptr_array_new_with_free_func(g_free);
  GPtrArray *keys = g_ptr_array_new_with_free_func(g_free);
  gchar **labels;
  const char *p;
  guint offset = 0, i;

  // The characters (UTF-8), without duplicates
  for (p = chars; p && *p; p = g_utf8_next_char(p)) {
    gchar *k = g_strndup(p, g_utf8_next_char(p) - p);
    gboolean dup = FALSE;
    for (i = 0; i < keys->len; i++)
      dup |= !strcmp(k, g_ptr_array_index(keys, i));
    if (dup || g_unichar_isspace(g_utf8_get_char(k)))
      g_free(k);
    else
      g_ptr_array_add(keys, k);
  }
  labels = g_new0(gchar *, n + 1);
  if (keys->len < 2) {
    // One key can't label more than one target
    if (keys->len == 1 && n == 1)
      labels[0] = g_strdup(g_ptr_array_index(keys, 0));
    g_ptr_array_free(keys, TRUE);
    g_ptr_array_free(queue, TRUE);
    if (n > 1 || !labels[0]) {
      g_free(labels);
      return g_new0(gchar *, 1);
    }
    return labels;
  }

  // Breadth-first: replace the first labels by longer ones until there are
  // enough of them.
  g_ptr_array_add(queue, g_strdup(""));
  while (queue->len - offset < n || queue->len == 1) {
    const char *prefix = g_ptr_array_index(queue, offset++);
    for (i = 0; i < keys->len; i++)
      g_ptr_array_add(queue, g_strconcat(prefix,
                                         (char *)g_ptr_array_index(keys, i),
                                         NULL));
  }
  for (i = 0; i < n; i++)
    labels[i] = g_strdup(g_ptr_array_index(queue, offset + i));

  g_ptr_array_free(queue, TRUE);
  g_ptr_array_free(keys, TRUE);
  return labels;
}

/* vim: set et cindent cinoptions=>2\:2(0 ts=2 sw=2:  For Vim users... */
