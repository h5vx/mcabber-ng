#ifndef __MCABBER_OMEMO_CORE_H__
#define __MCABBER_OMEMO_CORE_H__ 1

// OMEMO (XEP-0384 v0.3, "eu.siacs.conversations.axolotl") key storage
// and cryptography.  This part knows nothing about XMPP or the UI, so that
// it can be tested on its own.

#include <glib.h>

#define OMEMO_NS              "eu.siacs.conversations.axolotl"
#define OMEMO_NODE_DEVICELIST OMEMO_NS ".devicelist"
#define OMEMO_NODE_BUNDLES    OMEMO_NS ".bundles:"

#define OMEMO_ERROR (omemo_error_quark())
GQuark omemo_error_quark(void);

typedef enum {
  OMEMO_ERROR_FAILED,
  OMEMO_ERROR_NO_SESSION,     // No session with the sender device
  OMEMO_ERROR_NOT_FOR_US,     // No key for our device in the message
  OMEMO_ERROR_UNTRUSTED,      // Identity key changed or device distrusted
  OMEMO_ERROR_DUPLICATE,      // Already decrypted (e.g. carbon copy)
  OMEMO_ERROR_INVALID,        // Malformed data, bad signature or MAC
} omemo_error_t;

typedef enum {
  OMEMO_TRUST_UNKNOWN = 0,    // We have never seen this device's identity
  OMEMO_TRUST_UNDECIDED,      // Known, but the user must decide
  OMEMO_TRUST_BLIND,          // Trusted automatically (not verified)
  OMEMO_TRUST_VERIFIED,       // Trusted by the user
  OMEMO_TRUST_DISTRUSTED,     // Rejected by the user
} omemo_trust_t;

typedef enum {
  OMEMO_POLICY_DEFAULT = -1,  // Use the omemo_policy option
  OMEMO_POLICY_OFF = 0,
  OMEMO_POLICY_ON = 1,
} omemo_policy_t;

typedef struct omemo_store omemo_store;

typedef struct {
  guint32 id;
  GBytes *key;                // Serialized public key (33 bytes)
} omemo_prekey_t;

typedef struct {
  guint32 spk_id;
  GBytes *spk;                // Signed pre-key, public part
  GBytes *spk_sig;            // Its signature by the identity key
  GBytes *ik;                 // Identity key, public part
  GArray *prekeys;            // omemo_prekey_t
} omemo_bundle_t;

typedef struct {
  guint32 rid;                // Recipient device
  gboolean prekey;            // The key is in a PreKeySignalMessage
  GBytes *data;
} omemo_key_t;

typedef struct {
  GBytes *iv;
  GBytes *payload;
  GPtrArray *keys;            // omemo_key_t*
} omemo_message_t;

typedef struct {
  const char *jid;
  guint32 device;
} omemo_addr_t;

typedef struct {
  guint32 device;
  omemo_trust_t trust;
  gchar *fingerprint;         // NULL if the identity is unknown
  gboolean session;           // We have a session with this device
} omemo_devinfo_t;

// The store lives in dir; it is created (with a new identity) if needed.
omemo_store *omemo_store_open(const char *dir, gboolean blind_trust,
                              GError **error);
void omemo_store_close(omemo_store *st);
void omemo_store_set_blind_trust(omemo_store *st, gboolean blind_trust);

guint32 omemo_store_device_id(omemo_store *st);
gchar *omemo_store_own_fingerprint(omemo_store *st);

// Our bundle, to be published.  Free it with omemo_bundle_free().
omemo_bundle_t *omemo_store_bundle(omemo_store *st);
void omemo_bundle_free(omemo_bundle_t *b);
omemo_bundle_t *omemo_bundle_new(void);
// TRUE once after pre-keys have been used (and replaced): the bundle
// must then be published again.
gboolean omemo_store_bundle_changed(omemo_store *st);

gboolean omemo_store_has_session(omemo_store *st, const char *jid,
                                 guint32 device);
gboolean omemo_store_build_session(omemo_store *st, const char *jid,
                                   guint32 device, const omemo_bundle_t *b,
                                   GError **error);

// Encrypt text for the given devices.  Devices without a session are
// skipped; *error is set if no key could be produced at all.
omemo_message_t *omemo_encrypt(omemo_store *st, const char *text,
                               const omemo_addr_t *to, guint count,
                               GError **error);
void omemo_message_free(omemo_message_t *m);

// Decrypt the key sent by jid/device and then the payload.  A message
// without payload (key transport) returns an empty string.
gchar *omemo_decrypt(omemo_store *st, const char *jid, guint32 device,
                     const omemo_key_t *key, GBytes *iv, GBytes *payload,
                     GError **error);

omemo_trust_t omemo_store_get_trust(omemo_store *st, const char *jid,
                                    guint32 device);
gboolean omemo_store_set_trust(omemo_store *st, const char *jid,
                               guint32 device, omemo_trust_t trust);
// Remove the identity and the session of a device.
void omemo_store_forget(omemo_store *st, const char *jid, guint32 device);
// The devices of jid that we know something about (omemo_devinfo_t*).
GPtrArray *omemo_store_devices(omemo_store *st, const char *jid,
                               const guint32 *listed, guint nlisted);
const char *omemo_trust_name(omemo_trust_t trust);
gboolean omemo_trust_usable(omemo_trust_t trust);

omemo_policy_t omemo_store_get_policy(omemo_store *st, const char *jid);
void omemo_store_set_policy(omemo_store *st, const char *jid,
                            omemo_policy_t policy);

// Cached device lists.  omemo_store_get_devices() returns FALSE if the
// list of jid has never been seen.
gboolean omemo_store_get_devices(omemo_store *st, const char *jid,
                                 GArray **devices);
void omemo_store_set_devices(omemo_store *st, const char *jid,
                             const guint32 *devices, guint count);

// "05abcdef..." (33 bytes) -> "abcdef01 23456789 ..." (64 hex digits)
gchar *omemo_fingerprint(const guint8 *key, gsize len);
// Compare a user-supplied fingerprint (spaces, colons and case ignored)
gboolean omemo_fingerprint_match(const char *fpr, const char *input);

#endif /* __MCABBER_OMEMO_CORE_H__ */

/* vim: set et cindent cinoptions=>2\:2(0 ts=2 sw=2:  For Vim users... */
