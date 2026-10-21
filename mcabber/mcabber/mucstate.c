/*
 * mucstate.c       -- What we have seen of each room
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

// A GKeyFile: one group per room (URI-escaped JID) with
//   last  = time of the last message
//   keys  = keys of the last messages (most recent last)
//   topic = the room topic

#include <string.h>
#include <glib/gstdio.h>

#include "mucstate.h"

#define MAX_KEYS 200

static GKeyFile *kf;
static gchar *kf_path;
static gboolean dirty;

void mucstate_open(const char *path)
{
  mucstate_close();
  kf = g_key_file_new();
  kf_path = g_strdup(path);
  if (path)
    g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, NULL);
  dirty = FALSE;
}

void mucstate_close(void)
{
  if (kf)
    g_key_file_free(kf);
  kf = NULL;
  g_free(kf_path);
  kf_path = NULL;
}

gboolean mucstate_dirty(void)
{
  return dirty;
}

gboolean mucstate_save(void)
{
  gchar *data, *dir;
  gsize len;
  gboolean ok;

  if (!kf || !kf_path || !dirty)
    return TRUE;
  dir = g_path_get_dirname(kf_path);
  g_mkdir_with_parents(dir, 0700);
  g_free(dir);
  data = g_key_file_to_data(kf, &len, NULL);
  ok = g_file_set_contents(kf_path, data, len, NULL);
  if (ok)
    g_chmod(kf_path, 0600);
  g_free(data);
  dirty = !ok;
  return ok;
}

static gchar *group(const char *room)
{
  gchar *low = g_utf8_strdown(room, -1);
  gchar *g = g_uri_escape_string(low, "@", FALSE);
  g_free(low);
  return g;
}

static void ensure_open(void)
{
  if (!kf)
    mucstate_open(NULL);
}

time_t mucstate_last(const char *room)
{
  gchar *g;
  time_t t;

  ensure_open();
  g = group(room);
  t = (time_t)g_key_file_get_int64(kf, g, "last", NULL);
  g_free(g);
  return t;
}

gboolean mucstate_seen(const char *room, const char *key)
{
  gchar *g, **keys;
  gboolean found = FALSE;

  if (!key)
    return FALSE;
  ensure_open();
  g = group(room);
  keys = g_key_file_get_string_list(kf, g, "keys", NULL, NULL);
  for (gchar **k = keys; k && *k && !found; k++)
    found = !strcmp(*k, key);
  g_strfreev(keys);
  g_free(g);
  return found;
}

void mucstate_add(const char *room, const char *key, time_t when)
{
  gchar *g;

  ensure_open();
  g = group(room);
  if (when > (time_t)g_key_file_get_int64(kf, g, "last", NULL))
    g_key_file_set_int64(kf, g, "last", (gint64)when);
  if (key) {
    gsize n = 0, skip;
    gchar **keys = g_key_file_get_string_list(kf, g, "keys", &n, NULL);
    gchar **list = g_new0(gchar *, n + 2);
    gsize i, j = 0;

    // Keep the last MAX_KEYS keys
    skip = (n + 1 > MAX_KEYS) ? n + 1 - MAX_KEYS : 0;
    for (i = skip; i < n; i++)
      list[j++] = keys[i];
    list[j++] = (gchar *)key;
    g_key_file_set_string_list(kf, g, "keys", (const gchar * const *)list,
                               j);
    g_free(list);
    g_strfreev(keys);
  }
  g_free(g);
  dirty = TRUE;
}

const char *mucstate_topic(const char *room)
{
  static gchar *topic;
  gchar *g;

  ensure_open();
  g = group(room);
  g_free(topic);
  topic = g_key_file_get_string(kf, g, "topic", NULL);
  g_free(g);
  return topic;
}

void mucstate_set_topic(const char *room, const char *topic)
{
  gchar *g;

  ensure_open();
  g = group(room);
  if (topic)
    g_key_file_set_string(kf, g, "topic", topic);
  else
    g_key_file_remove_key(kf, g, "topic", NULL);
  g_free(g);
  dirty = TRUE;
}

gchar *mucstate_key(const char *stanza_id, const char *nick,
                    const char *text)
{
  GChecksum *ck;
  gchar *key;

  if (stanza_id && *stanza_id)
    return g_strconcat("id:", stanza_id, NULL);
  ck = g_checksum_new(G_CHECKSUM_SHA1);
  g_checksum_update(ck, (const guchar *)(nick ? nick : ""), -1);
  g_checksum_update(ck, (const guchar *)"", 1);
  g_checksum_update(ck, (const guchar *)(text ? text : ""), -1);
  key = g_strconcat("h:", g_checksum_get_string(ck), NULL);
  g_checksum_free(ck);
  return key;
}

/* vim: set et cindent cinoptions=>2\:2(0 ts=2 sw=2:  For Vim users... */
