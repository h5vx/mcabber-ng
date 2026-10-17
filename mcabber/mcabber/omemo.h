#ifndef __MCABBER_OMEMO_H__
#define __MCABBER_OMEMO_H__ 1

#include <glib.h>
#include <loudmouth/loudmouth.h>

#ifdef HAVE_OMEMO

#include "omemo_core.h"

gboolean omemo_enabled(void);
void omemo_connected(void);
void omemo_disconnected(void);
void omemo_deinit(void);

#define OMEMO_FALLBACK_BODY \
  "I sent you an OMEMO encrypted message but your client doesn't seem " \
  "to support that."

// The feature advertised in entity capabilities (PEP notifications)
#define OMEMO_NOTIFY_FEATURE OMEMO_NODE_DEVICELIST "+notify"

// PEP notification; returns TRUE if it was an OMEMO device list
gboolean omemo_handle_event(const char *from, LmMessageNode *event);

// What to do with a message to bjid
typedef enum {
  OMEMO_SEND_PLAIN,
  OMEMO_SEND_ENCRYPTED,
  OMEMO_SEND_REFUSE,          // Encryption required but impossible
} omemo_send_mode_t;
omemo_send_mode_t omemo_send_mode(const char *bjid, gchar **reason);

// Encrypt and send m (its body is the fallback text).  The message can be
// queued until the keys of the recipient have been fetched; it is unref'd.
void omemo_send(LmMessage *m, const char *bjid, const char *text);

// Decrypt the OMEMO element of message node.  Returns NULL if there is no
// such element.  *drop is set if nothing should be displayed (key
// transport, duplicate); *ok is FALSE if the returned text is an error
// message instead of the plaintext.  sender is the bare JID of the sender
// (our own JID for a sent carbon).
gchar *omemo_receive(const char *sender, LmMessageNode *node,
                     gboolean *drop, gboolean *ok);

// A chat window has been opened: fetch the device list if unknown.
void omemo_prefetch(const char *bjid);

// /omemo
void omemo_command(const char *arg);
// Status shown in the chat status line ("omemo", or NULL)
const char *omemo_status(const char *bjid);

#endif /* HAVE_OMEMO */

#endif /* __MCABBER_OMEMO_H__ */

/* vim: set et cindent cinoptions=>2\:2(0 ts=2 sw=2:  For Vim users... */
