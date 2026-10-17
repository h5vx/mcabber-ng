#ifndef __MCABBER_PASTE_H__
#define __MCABBER_PASTE_H__ 1

#include <glib.h>

//  paste_normalize(text)
// Prepare pasted text for the input line: CR LF and CR become LF, tabs
// are expanded, other control characters and trailing newlines are
// removed.  Returns a newly allocated string.
gchar *paste_normalize(const char *text);

//  paste_count_lines(text)
// Number of lines of the (normalized) text.
guint  paste_count_lines(const char *text);

#endif /* __MCABBER_PASTE_H__ */

/* vim: set et cindent cinoptions=>2\:2(0 ts=2 sw=2:  For Vim users... */
