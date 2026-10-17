#ifndef __MCABBER_LINKS_H__
#define __MCABBER_LINKS_H__ 1

#include <glib.h>

// Links in a text: byte offsets of [start, end)
typedef struct {
  gsize start, end;
} link_span_t;

// Find the links (URLs with a scheme, xmpp:, mailto:, www.) in text.
// Returns an array of link_span_t.
GArray *links_find(const char *text);

// The URL to open for a link found in text ("www." gets "https://")
gchar *links_url(const char *text, const link_span_t *span);

// EasyMotion-like labels for n targets, made of the characters of chars
// (UTF-8).  No label is a prefix of another; the first targets get the
// shortest labels.  Returns a NULL-terminated array (g_strfreev).
gchar **links_labels(const char *chars, guint n);

#endif /* __MCABBER_LINKS_H__ */

/* vim: set et cindent cinoptions=>2\:2(0 ts=2 sw=2:  For Vim users... */
