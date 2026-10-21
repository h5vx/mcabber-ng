#ifndef __MCABBER_MUCSTATE_H__
#define __MCABBER_MUCSTATE_H__ 1

// What we have seen of each room, kept across connections and restarts:
// the time of the last message, the keys of the last messages (to drop the
// ones the room sends again in its history) and the topic.

#include <time.h>
#include <glib.h>

// The state is saved in this file (by mucstate_save()).
void mucstate_open(const char *path);
void mucstate_close(void);
gboolean mucstate_save(void);
gboolean mucstate_dirty(void);

// Time of the last message received from room (0 if none)
time_t mucstate_last(const char *room);
// TRUE if a message with this key has already been received
gboolean mucstate_seen(const char *room, const char *key);
// Record a message
void mucstate_add(const char *room, const char *key, time_t when);

const char *mucstate_topic(const char *room);
void mucstate_set_topic(const char *room, const char *topic);

// Key of a message: its stanza-id when the room gives one, or a hash of
// the sender and the text.
gchar *mucstate_key(const char *stanza_id, const char *nick,
                    const char *text);

#endif /* __MCABBER_MUCSTATE_H__ */

/* vim: set et cindent cinoptions=>2\:2(0 ts=2 sw=2:  For Vim users... */
