#ifndef __MCABBER_XMPP_MUC_H__
#define __MCABBER_XMPP_MUC_H__ 1

typedef struct {
  char *to;
  char *from;
  char *passwd;
  char *reason;
  char *evid;
  gboolean reply;
} event_muc_invitation_t;

void destroy_event_muc_invitation(event_muc_invitation_t *invitation);
void roompresence(gpointer room, void *presencedata);
void got_invite(const char* from, const char *to, const char* reason,
                const char* passwd, gboolean reply);
void got_muc_message(const char *from, LmMessageNode *x,
                     time_t timestamp);
// Room history and reconnections
gboolean muc_message_check(const char *room, const char *nick,
                           const char *text, const char *stanza_id,
                           time_t timestamp);
gboolean muc_topic_check(const char *room, const char *topic);
gboolean muc_backlog(const char *room);
gboolean muc_joined(const char *room);
void muc_forget_room(const char *room);
void muc_rejoin(void);
void muc_disconnected(void);
void muc_save_state(void);

void handle_muc_presence(const char *from, LmMessageNode * xmldata,
                         const char *roomjid, const char *rname,
                         enum imstatus ust, const char *ustmsg,
                         time_t usttime, char bpprio);

#endif /* __MCABBER_XMPP_MUC_H__ */

/* vim: set et cindent cinoptions=>2\:2(0 ts=2 sw=2:  For Vim users... */
