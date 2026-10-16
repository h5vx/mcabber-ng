#ifndef __MCABBER_MSGFORMAT_H__
#define __MCABBER_MSGFORMAT_H__ 1

#include <time.h>
#include <glib.h>

#include <mcabber/fmt.h>

void   msgfmt_init(void);

//  msgfmt_head(timestamp, flags, nick, out)
// Append the head of a message (time, mark, nickname...) to out.
void   msgfmt_head(time_t timestamp, guint flags, const char *nick,
                   sstr_t *out);
gchar *msgfmt_head_text(time_t timestamp, guint flags, const char *nick);

gboolean msgfmt_ui_has(const char *name);
gboolean msgfmt_ui(const char *name, const char *kind,
                   const char *const *fields, fmt_getter_t getter,
                   gpointer data, guint width, sstr_t *out);
gchar   *msgfmt_icon(const char *key);

#endif /* __MCABBER_MSGFORMAT_H__ */

/* vim: set et cindent cinoptions=>2\:2(0 ts=2 sw=2:  For Vim users... */
