/*
 * omemo_core.c     -- OMEMO key storage and cryptography
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

// The Signal protocol is provided by libomemo-c (or libsignal-protocol-c,
// which has the same API); the primitives it needs come from libgcrypt.
//
// Everything is kept in GKeyFiles in the store directory:
//   identity.ini  our identity, signed pre-key and pre-keys
//   sessions.ini  [jid] device = session record
//   trust.ini     [jid] device = trust;identity key
//   contacts.ini  [jid] policy, devices (cached device list)
// Binary values are base64-encoded, group names are URI-escaped JIDs.

#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <glib/gstdio.h>
#include <gcrypt.h>

#include <signal_protocol.h>
#include <key_helper.h>
#include <session_builder.h>
#include <session_cipher.h>
#include <session_pre_key.h>
#include <protocol.h>
#include <curve.h>

#include "omemo_core.h"

#define PREKEY_COUNT  100
#define AES_KEY_LEN   16
#define AES_TAG_LEN   16
#define AES_IV_LEN    12

struct omemo_store {
  gchar *dir;
  gboolean blind_trust;
  signal_context *ctx;
  signal_protocol_store_context *sctx;
  GKeyFile *identity, *sessions, *trust, *contacts;
  gboolean bundle_changed;
  guint prekeys_removed;
};

G_DEFINE_QUARK(omemo-error-quark, omemo_error)

static const char *trust_names[] = {
  "unknown", "undecided", "blind", "verified", "distrusted"
};

const char *omemo_trust_name(omemo_trust_t trust)
{
  if ((guint)trust < G_N_ELEMENTS(trust_names))
    return trust_names[trust];
  return "unknown";
}

//  omemo_trust_usable(trust)
// TRUE if we encrypt for a device with this trust level.
gboolean omemo_trust_usable(omemo_trust_t trust)
{
  return trust == OMEMO_TRUST_BLIND || trust == OMEMO_TRUST_VERIFIED;
}

static omemo_trust_t trust_from_name(const char *name)
{
  guint i;
  for (i = 0; i < G_N_ELEMENTS(trust_names); i++)
    if (!g_strcmp0(name, trust_names[i]))
      return (omemo_trust_t)i;
  return OMEMO_TRUST_UNKNOWN;
}

/* -- Helpers ------------------------------------------------------------- */

static gchar *jid_group(const char *jid, size_t len)
{
  gchar *j = g_strndup(jid, len);
  gchar *g = g_uri_escape_string(j, "@", FALSE);
  g_free(j);
  return g;
}

static gchar *addr_group(const signal_protocol_address *a)
{
  return jid_group(a->name, a->name_len);
}

static gchar *dev_key(guint32 device)
{
  return g_strdup_printf("%u", device);
}

static GBytes *kf_get_bytes(GKeyFile *kf, const char *group, const char *key)
{
  gchar *v = g_key_file_get_string(kf, group, key, NULL);
  guchar *data;
  gsize len;

  if (!v)
    return NULL;
  data = g_base64_decode(v, &len);
  g_free(v);
  return g_bytes_new_take(data, len);
}

static void kf_set_bytes(GKeyFile *kf, const char *group, const char *key,
                         const guint8 *data, gsize len)
{
  gchar *v = g_base64_encode(data, len);
  g_key_file_set_string(kf, group, key, v);
  g_free(v);
}

static gboolean save_kf(omemo_store *st, GKeyFile *kf, const char *name)
{
  gchar *path = g_build_filename(st->dir, name, NULL);
  gsize len;
  gchar *data = g_key_file_to_data(kf, &len, NULL);
  GError *err = NULL;
  gboolean ok;

#if GLIB_CHECK_VERSION(2, 66, 0)
  ok = g_file_set_contents_full(path, data, len,
                                G_FILE_SET_CONTENTS_CONSISTENT, 0600, &err);
#else
  ok = g_file_set_contents(path, data, len, &err);
  if (ok)
    g_chmod(path, 0600);
#endif
  if (!ok) {
    g_warning("OMEMO: cannot save %s: %s", path, err->message);
    g_error_free(err);
  }
  g_free(data);
  g_free(path);
  return ok;
}

static GKeyFile *load_kf(omemo_store *st, const char *name)
{
  GKeyFile *kf = g_key_file_new();
  gchar *path = g_build_filename(st->dir, name, NULL);
  g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, NULL);
  g_free(path);
  return kf;
}

#define save_identity_file(st) save_kf(st, st->identity, "identity.ini")
#define save_sessions(st)      save_kf(st, st->sessions, "sessions.ini")
#define save_trust(st)         save_kf(st, st->trust, "trust.ini")
#define save_contacts(st)      save_kf(st, st->contacts, "contacts.ini")

static GBytes *buffer_to_bytes(signal_buffer *b)
{
  GBytes *r = g_bytes_new(signal_buffer_data(b), signal_buffer_len(b));
  signal_buffer_free(b);
  return r;
}

static GBytes *public_key_bytes(ec_public_key *key)
{
  signal_buffer *b = NULL;
  if (ec_public_key_serialize(&b, key) < 0)
    return NULL;
  return buffer_to_bytes(b);
}

static guint32 random_u32(void)
{
  guint32 v;
  gcry_randomize(&v, sizeof v, GCRY_STRONG_RANDOM);
  return v;
}

/* -- Crypto provider (libgcrypt) ----------------------------------------- */

static void crypto_init(void)
{
  if (gcry_control(GCRYCTL_INITIALIZATION_FINISHED_P))
    return;
  gcry_check_version(NULL);
  gcry_control(GCRYCTL_DISABLE_SECMEM, 0);
  gcry_control(GCRYCTL_INITIALIZATION_FINISHED, 0);
}

static int cp_random(uint8_t *data, size_t len, void *ud)
{
  gcry_randomize(data, len, GCRY_STRONG_RANDOM);
  return SG_SUCCESS;
}

static int cp_hmac_init(void **hctx, const uint8_t *key, size_t key_len,
                        void *ud)
{
  gcry_mac_hd_t hd;
  if (gcry_mac_open(&hd, GCRY_MAC_HMAC_SHA256, 0, NULL))
    return SG_ERR_UNKNOWN;
  if (gcry_mac_setkey(hd, key, key_len)) {
    gcry_mac_close(hd);
    return SG_ERR_UNKNOWN;
  }
  *hctx = hd;
  return SG_SUCCESS;
}

static int cp_hmac_update(void *hctx, const uint8_t *data, size_t len,
                          void *ud)
{
  return gcry_mac_write(hctx, data, len) ? SG_ERR_UNKNOWN : SG_SUCCESS;
}

static int cp_hmac_final(void *hctx, signal_buffer **output, void *ud)
{
  uint8_t md[32];
  size_t len = sizeof md;
  if (gcry_mac_read(hctx, md, &len))
    return SG_ERR_UNKNOWN;
  *output = signal_buffer_create(md, len);
  return *output ? SG_SUCCESS : SG_ERR_NOMEM;
}

static void cp_hmac_cleanup(void *hctx, void *ud)
{
  if (hctx)
    gcry_mac_close(hctx);
}

static int cp_sha512_init(void **dctx, void *ud)
{
  gcry_md_hd_t hd;
  if (gcry_md_open(&hd, GCRY_MD_SHA512, 0))
    return SG_ERR_UNKNOWN;
  *dctx = hd;
  return SG_SUCCESS;
}

static int cp_sha512_update(void *dctx, const uint8_t *data, size_t len,
                            void *ud)
{
  gcry_md_write(dctx, data, len);
  return SG_SUCCESS;
}

static int cp_sha512_final(void *dctx, signal_buffer **output, void *ud)
{
  unsigned char *md = gcry_md_read(dctx, GCRY_MD_SHA512);
  if (!md)
    return SG_ERR_UNKNOWN;
  *output = signal_buffer_create(md, 64);
  gcry_md_reset(dctx);
  return *output ? SG_SUCCESS : SG_ERR_NOMEM;
}

static void cp_sha512_cleanup(void *dctx, void *ud)
{
  if (dctx)
    gcry_md_close(dctx);
}

static int aes_algo(size_t key_len)
{
  switch (key_len) {
    case 16: return GCRY_CIPHER_AES128;
    case 24: return GCRY_CIPHER_AES192;
    case 32: return GCRY_CIPHER_AES256;
  }
  return 0;
}

static int cp_crypt(gboolean enc, signal_buffer **output, int cipher,
                    const uint8_t *key, size_t key_len,
                    const uint8_t *iv, size_t iv_len,
                    const uint8_t *in, size_t in_len)
{
  int algo = aes_algo(key_len);
  int mode;
  gcry_cipher_hd_t hd;
  guint8 *buf;
  size_t len = in_len;
  int ret = SG_ERR_UNKNOWN;

  if (!algo)
    return SG_ERR_INVAL;
  if (cipher == SG_CIPHER_AES_CBC_PKCS5)
    mode = GCRY_CIPHER_MODE_CBC;
  else if (cipher == SG_CIPHER_AES_CTR_NOPADDING)
    mode = GCRY_CIPHER_MODE_CTR;
  else
    return SG_ERR_INVAL;

  if (mode == GCRY_CIPHER_MODE_CBC) {
    if (enc)
      len = (in_len / 16 + 1) * 16;     // PKCS#5 padding
    else if (!in_len || in_len % 16)
      return SG_ERR_INVAL;
  }

  buf = g_malloc(len);
  memcpy(buf, in, in_len);
  if (enc && len > in_len)
    memset(buf + in_len, (int)(len - in_len), len - in_len);

  if (gcry_cipher_open(&hd, algo, mode, 0)) {
    g_free(buf);
    return SG_ERR_UNKNOWN;
  }
  if (gcry_cipher_setkey(hd, key, key_len))
    goto done;
  if (mode == GCRY_CIPHER_MODE_CTR ? gcry_cipher_setctr(hd, iv, iv_len)
                                   : gcry_cipher_setiv(hd, iv, iv_len))
    goto done;
  if (enc ? gcry_cipher_encrypt(hd, buf, len, NULL, 0)
          : gcry_cipher_decrypt(hd, buf, len, NULL, 0))
    goto done;

  if (!enc && mode == GCRY_CIPHER_MODE_CBC) {
    guint pad = buf[len - 1];
    guint i;
    if (pad < 1 || pad > 16 || pad > len) {
      ret = SG_ERR_INVAL;
      goto done;
    }
    for (i = 0; i < pad; i++)
      if (buf[len - 1 - i] != pad) {
        ret = SG_ERR_INVAL;
        goto done;
      }
    len -= pad;
  }
  *output = signal_buffer_create(buf, len);
  ret = *output ? SG_SUCCESS : SG_ERR_NOMEM;

done:
  gcry_cipher_close(hd);
  memset(buf, 0, len);
  g_free(buf);
  return ret;
}

static int cp_encrypt(signal_buffer **output, int cipher,
                      const uint8_t *key, size_t key_len,
                      const uint8_t *iv, size_t iv_len,
                      const uint8_t *plaintext, size_t plaintext_len,
                      void *ud)
{
  return cp_crypt(TRUE, output, cipher, key, key_len, iv, iv_len,
                  plaintext, plaintext_len);
}

static int cp_decrypt(signal_buffer **output, int cipher,
                      const uint8_t *key, size_t key_len,
                      const uint8_t *iv, size_t iv_len,
                      const uint8_t *ciphertext, size_t ciphertext_len,
                      void *ud)
{
  return cp_crypt(FALSE, output, cipher, key, key_len, iv, iv_len,
                  ciphertext, ciphertext_len);
}

// AES-128-GCM for the message payload
static gboolean gcm_crypt(gboolean enc, const guint8 *key,
                          const guint8 *iv, gsize iv_len,
                          guint8 *buf, gsize len, guint8 *tag)
{
  gcry_cipher_hd_t hd;
  gboolean ok = FALSE;

  if (gcry_cipher_open(&hd, GCRY_CIPHER_AES128, GCRY_CIPHER_MODE_GCM, 0))
    return FALSE;
  if (gcry_cipher_setkey(hd, key, AES_KEY_LEN) ||
      gcry_cipher_setiv(hd, iv, iv_len))
    goto done;
  if (enc) {
    if (gcry_cipher_encrypt(hd, buf, len, NULL, 0) ||
        gcry_cipher_gettag(hd, tag, AES_TAG_LEN))
      goto done;
  } else {
    if (gcry_cipher_decrypt(hd, buf, len, NULL, 0) ||
        gcry_cipher_checktag(hd, tag, AES_TAG_LEN))
      goto done;
  }
  ok = TRUE;
done:
  gcry_cipher_close(hd);
  return ok;
}

/* -- Session store ------------------------------------------------------- */

static int ss_load(signal_buffer **record, signal_buffer **user_record,
                   const signal_protocol_address *address, void *ud)
{
  omemo_store *st = ud;
  gchar *g = addr_group(address), *k = dev_key(address->device_id);
  GBytes *b = kf_get_bytes(st->sessions, g, k);

  g_free(g);
  g_free(k);
  if (!b)
    return 0;
  *record = signal_buffer_create(g_bytes_get_data(b, NULL),
                                 g_bytes_get_size(b));
  g_bytes_unref(b);
  return 1;
}

static int ss_sub_devices(signal_int_list **sessions, const char *name,
                          size_t name_len, void *ud)
{
  omemo_store *st = ud;
  gchar *g = jid_group(name, name_len);
  gchar **keys = g_key_file_get_keys(st->sessions, g, NULL, NULL);
  signal_int_list *list = signal_int_list_alloc();
  int n = 0;

  for (gchar **k = keys; k && *k; k++, n++)
    signal_int_list_push_back(list, (int)g_ascii_strtoull(*k, NULL, 10));
  g_strfreev(keys);
  g_free(g);
  *sessions = list;
  return n;
}

static int ss_store(const signal_protocol_address *address,
                    uint8_t *record, size_t record_len,
                    uint8_t *user_record, size_t user_record_len, void *ud)
{
  omemo_store *st = ud;
  gchar *g = addr_group(address), *k = dev_key(address->device_id);

  kf_set_bytes(st->sessions, g, k, record, record_len);
  g_free(g);
  g_free(k);
  return save_sessions(st) ? SG_SUCCESS : SG_ERR_UNKNOWN;
}

static int ss_contains(const signal_protocol_address *address, void *ud)
{
  omemo_store *st = ud;
  gchar *g = addr_group(address), *k = dev_key(address->device_id);
  int r = g_key_file_has_key(st->sessions, g, k, NULL);
  g_free(g);
  g_free(k);
  return r;
}

static int ss_delete(const signal_protocol_address *address, void *ud)
{
  omemo_store *st = ud;
  gchar *g = addr_group(address), *k = dev_key(address->device_id);
  int r = g_key_file_remove_key(st->sessions, g, k, NULL);
  g_free(g);
  g_free(k);
  if (r)
    save_sessions(st);
  return r;
}

static int ss_delete_all(const char *name, size_t name_len, void *ud)
{
  omemo_store *st = ud;
  gchar *g = jid_group(name, name_len);
  gsize n = 0;
  gchar **keys = g_key_file_get_keys(st->sessions, g, &n, NULL);
  g_strfreev(keys);
  g_key_file_remove_group(st->sessions, g, NULL);
  g_free(g);
  save_sessions(st);
  return (int)n;
}

/* -- Pre-key stores ------------------------------------------------------ */

static int pk_load(signal_buffer **record, uint32_t id, void *ud)
{
  omemo_store *st = ud;
  gchar *k = dev_key(id);
  GBytes *b = kf_get_bytes(st->identity, "prekeys", k);
  g_free(k);
  if (!b)
    return SG_ERR_INVALID_KEY_ID;
  *record = signal_buffer_create(g_bytes_get_data(b, NULL),
                                 g_bytes_get_size(b));
  g_bytes_unref(b);
  return SG_SUCCESS;
}

static int pk_store(uint32_t id, uint8_t *record, size_t len, void *ud)
{
  omemo_store *st = ud;
  gchar *k = dev_key(id);
  kf_set_bytes(st->identity, "prekeys", k, record, len);
  g_free(k);
  return save_identity_file(st) ? SG_SUCCESS : SG_ERR_UNKNOWN;
}

static int pk_contains(uint32_t id, void *ud)
{
  omemo_store *st = ud;
  gchar *k = dev_key(id);
  int r = g_key_file_has_key(st->identity, "prekeys", k, NULL);
  g_free(k);
  return r;
}

static int pk_remove(uint32_t id, void *ud)
{
  omemo_store *st = ud;
  gchar *k = dev_key(id);
  if (g_key_file_remove_key(st->identity, "prekeys", k, NULL)) {
    st->prekeys_removed++;
    save_identity_file(st);
  }
  g_free(k);
  return SG_SUCCESS;
}

static int spk_load(signal_buffer **record, uint32_t id, void *ud)
{
  omemo_store *st = ud;
  GBytes *b;

  if ((guint32)g_key_file_get_uint64(st->identity, "signed_prekey", "id",
                                     NULL) != id)
    return SG_ERR_INVALID_KEY_ID;
  b = kf_get_bytes(st->identity, "signed_prekey", "record");
  if (!b)
    return SG_ERR_INVALID_KEY_ID;
  *record = signal_buffer_create(g_bytes_get_data(b, NULL),
                                 g_bytes_get_size(b));
  g_bytes_unref(b);
  return SG_SUCCESS;
}

static int spk_store(uint32_t id, uint8_t *record, size_t len, void *ud)
{
  omemo_store *st = ud;
  g_key_file_set_uint64(st->identity, "signed_prekey", "id", id);
  kf_set_bytes(st->identity, "signed_prekey", "record", record, len);
  return save_identity_file(st) ? SG_SUCCESS : SG_ERR_UNKNOWN;
}

static int spk_contains(uint32_t id, void *ud)
{
  omemo_store *st = ud;
  return g_key_file_has_key(st->identity, "signed_prekey", "record", NULL) &&
         (guint32)g_key_file_get_uint64(st->identity, "signed_prekey", "id",
                                        NULL) == id;
}

static int spk_remove(uint32_t id, void *ud)
{
  return SG_SUCCESS;    // We only have one signed pre-key
}

/* -- Identity store ------------------------------------------------------ */

static int ik_get_pair(signal_buffer **public_data, signal_buffer **private_data,
                       void *ud)
{
  omemo_store *st = ud;
  GBytes *pub = kf_get_bytes(st->identity, "identity", "public");
  GBytes *priv = kf_get_bytes(st->identity, "identity", "private");
  int ret = SG_ERR_UNKNOWN;

  if (pub && priv) {
    *public_data = signal_buffer_create(g_bytes_get_data(pub, NULL),
                                        g_bytes_get_size(pub));
    *private_data = signal_buffer_create(g_bytes_get_data(priv, NULL),
                                         g_bytes_get_size(priv));
    ret = SG_SUCCESS;
  }
  if (pub) g_bytes_unref(pub);
  if (priv) g_bytes_unref(priv);
  return ret;
}

static int ik_get_registration_id(void *ud, uint32_t *registration_id)
{
  omemo_store *st = ud;
  *registration_id = (guint32)g_key_file_get_uint64(st->identity, "identity",
                                                     "registration_id", NULL);
  return SG_SUCCESS;
}

// Stored identity: trust level and key (NULL if unknown)
static omemo_trust_t identity_lookup(omemo_store *st, const char *group,
                                     const char *key, GBytes **ik)
{
  gchar *v = g_key_file_get_string(st->trust, group, key, NULL);
  gchar *sep;
  omemo_trust_t trust;

  if (ik)
    *ik = NULL;
  if (!v)
    return OMEMO_TRUST_UNKNOWN;
  sep = strchr(v, ';');
  if (sep)
    *sep++ = '\0';
  trust = trust_from_name(v);
  if (ik && sep && *sep) {
    gsize len;
    guchar *data = g_base64_decode(sep, &len);
    *ik = g_bytes_new_take(data, len);
  }
  g_free(v);
  return trust;
}

static void identity_set(omemo_store *st, const char *group, const char *key,
                         omemo_trust_t trust, const guint8 *ik, gsize len)
{
  gchar *b64 = g_base64_encode(ik, len);
  gchar *v = g_strdup_printf("%s;%s", omemo_trust_name(trust), b64);
  g_key_file_set_string(st->trust, group, key, v);
  g_free(v);
  g_free(b64);
}

static gboolean group_has_verified(omemo_store *st, const char *group)
{
  gchar **keys = g_key_file_get_keys(st->trust, group, NULL, NULL);
  gboolean found = FALSE;
  for (gchar **k = keys; k && *k && !found; k++)
    found = identity_lookup(st, group, *k, NULL) == OMEMO_TRUST_VERIFIED;
  g_strfreev(keys);
  return found;
}

static int ik_save(const signal_protocol_address *address,
                   uint8_t *key_data, size_t key_len, void *ud)
{
  omemo_store *st = ud;
  gchar *g = addr_group(address), *k = dev_key(address->device_id);
  GBytes *old;
  omemo_trust_t trust = identity_lookup(st, g, k, &old);

  if (!key_data) {
    g_key_file_remove_key(st->trust, g, k, NULL);
  } else if (!old || g_bytes_get_size(old) != key_len ||
             memcmp(g_bytes_get_data(old, NULL), key_data, key_len)) {
    // A new device: Blind Trust Before Verification, unless the user has
    // already verified one of the devices of this contact.
    if (trust == OMEMO_TRUST_UNKNOWN || old)
      trust = (st->blind_trust && !group_has_verified(st, g))
              ? OMEMO_TRUST_BLIND : OMEMO_TRUST_UNDECIDED;
    identity_set(st, g, k, trust, key_data, key_len);
  }
  if (old)
    g_bytes_unref(old);
  g_free(g);
  g_free(k);
  return save_trust(st) ? SG_SUCCESS : SG_ERR_UNKNOWN;
}

static int ik_is_trusted(const signal_protocol_address *address,
                         uint8_t *key_data, size_t key_len, void *ud)
{
  omemo_store *st = ud;
  gchar *g = addr_group(address), *k = dev_key(address->device_id);
  GBytes *old;
  omemo_trust_t trust = identity_lookup(st, g, k, &old);
  int r = 1;

  if (old) {
    // The identity of a device must never change
    if (g_bytes_get_size(old) != key_len ||
        memcmp(g_bytes_get_data(old, NULL), key_data, key_len))
      r = 0;
    g_bytes_unref(old);
  }
  if (trust == OMEMO_TRUST_DISTRUSTED)
    r = 0;
  g_free(g);
  g_free(k);
  return r;
}

/* -- Store setup --------------------------------------------------------- */

static gboolean generate_prekeys(omemo_store *st, guint count)
{
  signal_protocol_key_helper_pre_key_list_node *head = NULL, *n;
  guint32 start = (guint32)g_key_file_get_uint64(st->identity, "identity",
                                                  "next_prekey_id", NULL);
  if (start < 1 || start >= 0xFFFFFF - count)
    start = 1;
  if (signal_protocol_key_helper_generate_pre_keys(&head, start, count,
                                                   st->ctx) < 0)
    return FALSE;
  for (n = head; n; n = signal_protocol_key_helper_key_list_next(n)) {
    session_pre_key *pk = signal_protocol_key_helper_key_list_element(n);
    signal_buffer *b = NULL;
    gchar *k;
    if (session_pre_key_serialize(&b, pk) < 0)
      continue;
    k = dev_key(session_pre_key_get_id(pk));
    kf_set_bytes(st->identity, "prekeys", k, signal_buffer_data(b),
                 signal_buffer_len(b));
    g_free(k);
    signal_buffer_bzero_free(b);
  }
  signal_protocol_key_helper_key_list_free(head);
  g_key_file_set_uint64(st->identity, "identity", "next_prekey_id",
                        start + count);
  return save_identity_file(st);
}

static gboolean generate_identity(omemo_store *st)
{
  ratchet_identity_key_pair *ikp = NULL;
  session_signed_pre_key *spk = NULL;
  signal_buffer *b = NULL;
  guint32 regid = 0, device;
  gboolean ok = FALSE;

  if (signal_protocol_key_helper_generate_identity_key_pair(&ikp, st->ctx) < 0)
    return FALSE;
  if (signal_protocol_key_helper_generate_registration_id(&regid, 0,
                                                          st->ctx) < 0)
    goto done;
  do
    device = random_u32() & 0x7fffffff;
  while (!device);

  g_key_file_set_uint64(st->identity, "identity", "device_id", device);
  g_key_file_set_uint64(st->identity, "identity", "registration_id", regid);
  if (ec_public_key_serialize(&b,
                              ratchet_identity_key_pair_get_public(ikp)) < 0)
    goto done;
  kf_set_bytes(st->identity, "identity", "public", signal_buffer_data(b),
               signal_buffer_len(b));
  signal_buffer_free(b);
  b = NULL;
  if (ec_private_key_serialize(&b,
                               ratchet_identity_key_pair_get_private(ikp)) < 0)
    goto done;
  kf_set_bytes(st->identity, "identity", "private", signal_buffer_data(b),
               signal_buffer_len(b));
  signal_buffer_bzero_free(b);
  b = NULL;

  if (signal_protocol_key_helper_generate_signed_pre_key(&spk, ikp, 1,
          (guint64)g_get_real_time() / 1000, st->ctx) < 0)
    goto done;
  if (session_signed_pre_key_serialize(&b, spk) < 0)
    goto done;
  g_key_file_set_uint64(st->identity, "signed_prekey", "id", 1);
  kf_set_bytes(st->identity, "signed_prekey", "record",
               signal_buffer_data(b), signal_buffer_len(b));
  signal_buffer_bzero_free(b);
  b = NULL;

  g_key_file_set_uint64(st->identity, "identity", "next_prekey_id", 1);
  ok = generate_prekeys(st, PREKEY_COUNT);

done:
  if (spk) SIGNAL_UNREF(spk);
  if (ikp) SIGNAL_UNREF(ikp);
  return ok;
}

omemo_store *omemo_store_open(const char *dir, gboolean blind_trust,
                              GError **error)
{
  omemo_store *st;
  signal_crypto_provider cp = {
    .random_func = cp_random,
    .hmac_sha256_init_func = cp_hmac_init,
    .hmac_sha256_update_func = cp_hmac_update,
    .hmac_sha256_final_func = cp_hmac_final,
    .hmac_sha256_cleanup_func = cp_hmac_cleanup,
    .sha512_digest_init_func = cp_sha512_init,
    .sha512_digest_update_func = cp_sha512_update,
    .sha512_digest_final_func = cp_sha512_final,
    .sha512_digest_cleanup_func = cp_sha512_cleanup,
    .encrypt_func = cp_encrypt,
    .decrypt_func = cp_decrypt,
  };

  crypto_init();

  if (g_mkdir_with_parents(dir, 0700)) {
    g_set_error(error, OMEMO_ERROR, OMEMO_ERROR_FAILED,
                "Cannot create %s: %s", dir, g_strerror(errno));
    return NULL;
  }
  g_chmod(dir, 0700);

  st = g_new0(omemo_store, 1);
  st->dir = g_strdup(dir);
  st->blind_trust = blind_trust;
  st->identity = load_kf(st, "identity.ini");
  st->sessions = load_kf(st, "sessions.ini");
  st->trust = load_kf(st, "trust.ini");
  st->contacts = load_kf(st, "contacts.ini");

  if (signal_context_create(&st->ctx, st) < 0)
    goto fail;
  cp.user_data = st;
  signal_context_set_crypto_provider(st->ctx, &cp);
  if (signal_protocol_store_context_create(&st->sctx, st->ctx) < 0)
    goto fail;

  {
    signal_protocol_session_store ss = {
      .load_session_func = ss_load,
      .get_sub_device_sessions_func = ss_sub_devices,
      .store_session_func = ss_store,
      .contains_session_func = ss_contains,
      .delete_session_func = ss_delete,
      .delete_all_sessions_func = ss_delete_all,
      .user_data = st,
    };
    signal_protocol_pre_key_store pks = {
      .load_pre_key = pk_load,
      .store_pre_key = pk_store,
      .contains_pre_key = pk_contains,
      .remove_pre_key = pk_remove,
      .user_data = st,
    };
    signal_protocol_signed_pre_key_store spks = {
      .load_signed_pre_key = spk_load,
      .store_signed_pre_key = spk_store,
      .contains_signed_pre_key = spk_contains,
      .remove_signed_pre_key = spk_remove,
      .user_data = st,
    };
    signal_protocol_identity_key_store iks = {
      .get_identity_key_pair = ik_get_pair,
      .get_local_registration_id = ik_get_registration_id,
      .save_identity = ik_save,
      .is_trusted_identity = ik_is_trusted,
      .user_data = st,
    };
    signal_protocol_store_context_set_session_store(st->sctx, &ss);
    signal_protocol_store_context_set_pre_key_store(st->sctx, &pks);
    signal_protocol_store_context_set_signed_pre_key_store(st->sctx, &spks);
    signal_protocol_store_context_set_identity_key_store(st->sctx, &iks);
  }

  if (!g_key_file_has_key(st->identity, "identity", "device_id", NULL)) {
    if (!generate_identity(st))
      goto fail;
    st->bundle_changed = TRUE;
  }
  return st;

fail:
  g_set_error(error, OMEMO_ERROR, OMEMO_ERROR_FAILED,
              "Cannot initialize the OMEMO store in %s", dir);
  omemo_store_close(st);
  return NULL;
}

void omemo_store_close(omemo_store *st)
{
  if (!st)
    return;
  if (st->sctx)
    signal_protocol_store_context_destroy(st->sctx);
  if (st->ctx)
    signal_context_destroy(st->ctx);
  g_key_file_free(st->identity);
  g_key_file_free(st->sessions);
  g_key_file_free(st->trust);
  g_key_file_free(st->contacts);
  g_free(st->dir);
  g_free(st);
}

void omemo_store_set_blind_trust(omemo_store *st, gboolean blind_trust)
{
  st->blind_trust = blind_trust;
}

guint32 omemo_store_device_id(omemo_store *st)
{
  return (guint32)g_key_file_get_uint64(st->identity, "identity", "device_id",
                                        NULL);
}

gchar *omemo_store_own_fingerprint(omemo_store *st)
{
  GBytes *pub = kf_get_bytes(st->identity, "identity", "public");
  gchar *fpr;
  if (!pub)
    return NULL;
  fpr = omemo_fingerprint(g_bytes_get_data(pub, NULL), g_bytes_get_size(pub));
  g_bytes_unref(pub);
  return fpr;
}

/* -- Bundles and sessions ------------------------------------------------ */

omemo_bundle_t *omemo_bundle_new(void)
{
  omemo_bundle_t *b = g_new0(omemo_bundle_t, 1);
  b->prekeys = g_array_new(FALSE, TRUE, sizeof(omemo_prekey_t));
  return b;
}

void omemo_bundle_free(omemo_bundle_t *b)
{
  guint i;
  if (!b)
    return;
  for (i = 0; i < b->prekeys->len; i++)
    g_bytes_unref(g_array_index(b->prekeys, omemo_prekey_t, i).key);
  g_array_free(b->prekeys, TRUE);
  if (b->spk) g_bytes_unref(b->spk);
  if (b->spk_sig) g_bytes_unref(b->spk_sig);
  if (b->ik) g_bytes_unref(b->ik);
  g_free(b);
}

omemo_bundle_t *omemo_store_bundle(omemo_store *st)
{
  omemo_bundle_t *b = omemo_bundle_new();
  session_signed_pre_key *spk = NULL;
  GBytes *rec;
  gchar **keys;

  b->ik = kf_get_bytes(st->identity, "identity", "public");
  rec = kf_get_bytes(st->identity, "signed_prekey", "record");
  if (!b->ik || !rec ||
      session_signed_pre_key_deserialize(&spk, g_bytes_get_data(rec, NULL),
                                         g_bytes_get_size(rec), st->ctx) < 0) {
    if (rec) g_bytes_unref(rec);
    omemo_bundle_free(b);
    return NULL;
  }
  g_bytes_unref(rec);
  b->spk_id = session_signed_pre_key_get_id(spk);
  b->spk = public_key_bytes(ec_key_pair_get_public(
                              session_signed_pre_key_get_key_pair(spk)));
  b->spk_sig = g_bytes_new(session_signed_pre_key_get_signature(spk),
                           session_signed_pre_key_get_signature_len(spk));
  SIGNAL_UNREF(spk);

  keys = g_key_file_get_keys(st->identity, "prekeys", NULL, NULL);
  for (gchar **k = keys; k && *k; k++) {
    session_pre_key *pk = NULL;
    omemo_prekey_t p;
    rec = kf_get_bytes(st->identity, "prekeys", *k);
    if (!rec)
      continue;
    if (session_pre_key_deserialize(&pk, g_bytes_get_data(rec, NULL),
                                    g_bytes_get_size(rec), st->ctx) >= 0) {
      p.id = session_pre_key_get_id(pk);
      p.key = public_key_bytes(ec_key_pair_get_public(
                                 session_pre_key_get_key_pair(pk)));
      if (p.key)
        g_array_append_val(b->prekeys, p);
      SIGNAL_UNREF(pk);
    }
    g_bytes_unref(rec);
  }
  g_strfreev(keys);
  return b;
}

gboolean omemo_store_bundle_changed(omemo_store *st)
{
  gboolean r = st->bundle_changed;
  st->bundle_changed = FALSE;
  return r;
}

// Replace the pre-keys used by incoming sessions
static void refill_prekeys(omemo_store *st)
{
  if (!st->prekeys_removed)
    return;
  generate_prekeys(st, st->prekeys_removed);
  st->prekeys_removed = 0;
  st->bundle_changed = TRUE;
}

static void make_addr(signal_protocol_address *a, const char *jid,
                      guint32 device)
{
  a->name = jid;
  a->name_len = strlen(jid);
  a->device_id = (int32_t)device;
}

gboolean omemo_store_has_session(omemo_store *st, const char *jid,
                                 guint32 device)
{
  signal_protocol_address a;
  make_addr(&a, jid, device);
  return signal_protocol_session_contains_session(st->sctx, &a) == 1;
}

static omemo_error_t map_error(int r)
{
  switch (r) {
    case SG_ERR_DUPLICATE_MESSAGE:
      return OMEMO_ERROR_DUPLICATE;
    case SG_ERR_NO_SESSION:
      return OMEMO_ERROR_NO_SESSION;
    case SG_ERR_UNTRUSTED_IDENTITY:
      return OMEMO_ERROR_UNTRUSTED;
    case SG_ERR_INVALID_KEY:
    case SG_ERR_INVALID_KEY_ID:
    case SG_ERR_INVALID_MAC:
    case SG_ERR_INVALID_MESSAGE:
    case SG_ERR_INVALID_VERSION:
    case SG_ERR_LEGACY_MESSAGE:
    case SG_ERR_INVALID_PROTO_BUF:
      return OMEMO_ERROR_INVALID;
  }
  return OMEMO_ERROR_FAILED;
}

static const char *error_text(int r)
{
  switch (r) {
    case SG_ERR_DUPLICATE_MESSAGE:  return "duplicate message";
    case SG_ERR_NO_SESSION:         return "no session";
    case SG_ERR_UNTRUSTED_IDENTITY: return "untrusted identity";
    case SG_ERR_INVALID_KEY:        return "invalid key";
    case SG_ERR_INVALID_KEY_ID:     return "unknown pre-key";
    case SG_ERR_INVALID_MAC:        return "bad MAC";
    case SG_ERR_INVALID_MESSAGE:    return "invalid message";
    case SG_ERR_INVALID_VERSION:    return "unsupported version";
    case SG_ERR_LEGACY_MESSAGE:     return "legacy message";
    case SG_ERR_INVALID_PROTO_BUF:  return "malformed message";
  }
  return "internal error";
}

static void set_sg_error(GError **error, int r, const char *what)
{
  g_set_error(error, OMEMO_ERROR, map_error(r), "%s: %s (%d)", what,
              error_text(r), r);
}

gboolean omemo_store_build_session(omemo_store *st, const char *jid,
                                   guint32 device, const omemo_bundle_t *b,
                                   GError **error)
{
  signal_protocol_address a;
  ec_public_key *ik = NULL, *spk = NULL, *pk = NULL;
  session_pre_key_bundle *bundle = NULL;
  session_builder *builder = NULL;
  const omemo_prekey_t *p;
  int r;

  if (!b->ik || !b->spk || !b->spk_sig || !b->prekeys->len) {
    g_set_error(error, OMEMO_ERROR, OMEMO_ERROR_INVALID,
                "Incomplete bundle");
    return FALSE;
  }
  p = &g_array_index(b->prekeys, omemo_prekey_t,
                     random_u32() % b->prekeys->len);

  if ((r = curve_decode_point(&ik, g_bytes_get_data(b->ik, NULL),
                              g_bytes_get_size(b->ik), st->ctx)) < 0 ||
      (r = curve_decode_point(&spk, g_bytes_get_data(b->spk, NULL),
                              g_bytes_get_size(b->spk), st->ctx)) < 0 ||
      (r = curve_decode_point(&pk, g_bytes_get_data(p->key, NULL),
                              g_bytes_get_size(p->key), st->ctx)) < 0) {
    set_sg_error(error, r, "Bad key in bundle");
    goto done;
  }
  r = session_pre_key_bundle_create(&bundle, 0, (int)device, p->id, pk,
                                    b->spk_id, spk,
                                    g_bytes_get_data(b->spk_sig, NULL),
                                    g_bytes_get_size(b->spk_sig), ik);
  if (r < 0) {
    set_sg_error(error, r, "Bad bundle");
    goto done;
  }
  make_addr(&a, jid, device);
  r = session_builder_create(&builder, st->sctx, &a, st->ctx);
  if (r >= 0)
    r = session_builder_process_pre_key_bundle(builder, bundle);
  if (r < 0)
    set_sg_error(error, r, "Cannot build session");

done:
  if (builder) session_builder_free(builder);
  if (bundle) SIGNAL_UNREF(bundle);
  if (ik) SIGNAL_UNREF(ik);
  if (spk) SIGNAL_UNREF(spk);
  if (pk) SIGNAL_UNREF(pk);
  return r >= 0;
}

/* -- Messages ------------------------------------------------------------ */

void omemo_message_free(omemo_message_t *m)
{
  guint i;
  if (!m)
    return;
  if (m->iv) g_bytes_unref(m->iv);
  if (m->payload) g_bytes_unref(m->payload);
  for (i = 0; m->keys && i < m->keys->len; i++) {
    omemo_key_t *k = g_ptr_array_index(m->keys, i);
    g_bytes_unref(k->data);
    g_free(k);
  }
  if (m->keys)
    g_ptr_array_free(m->keys, TRUE);
  g_free(m);
}

omemo_message_t *omemo_encrypt(omemo_store *st, const char *text,
                               const omemo_addr_t *to, guint count,
                               GError **error)
{
  guint8 keytag[AES_KEY_LEN + AES_TAG_LEN], iv[AES_IV_LEN];
  gsize len = strlen(text);
  guint8 *buf = g_memdup2(text, len ? len : 1);
  omemo_message_t *m;
  int last = 0;
  guint i;

  gcry_randomize(keytag, AES_KEY_LEN, GCRY_STRONG_RANDOM);
  gcry_create_nonce(iv, sizeof iv);
  if (!gcm_crypt(TRUE, keytag, iv, sizeof iv, buf, len,
                 keytag + AES_KEY_LEN)) {
    g_free(buf);
    g_set_error(error, OMEMO_ERROR, OMEMO_ERROR_FAILED,
                "AES-GCM encryption failed");
    return NULL;
  }

  m = g_new0(omemo_message_t, 1);
  m->iv = g_bytes_new(iv, sizeof iv);
  m->payload = g_bytes_new_take(buf, len);
  m->keys = g_ptr_array_new();

  for (i = 0; i < count; i++) {
    signal_protocol_address a;
    session_cipher *cipher = NULL;
    ciphertext_message *cm = NULL;
    omemo_key_t *k;
    int r;

    make_addr(&a, to[i].jid, to[i].device);
    if (signal_protocol_session_contains_session(st->sctx, &a) != 1)
      continue;
    // libsignal does not check the identity when encrypting
    if (omemo_store_get_trust(st, to[i].jid, to[i].device) ==
        OMEMO_TRUST_DISTRUSTED) {
      last = SG_ERR_UNTRUSTED_IDENTITY;
      continue;
    }
    r = session_cipher_create(&cipher, st->sctx, &a, st->ctx);
    if (r >= 0)
      r = session_cipher_encrypt(cipher, keytag, sizeof keytag, &cm);
    if (r >= 0) {
      signal_buffer *ser = ciphertext_message_get_serialized(cm);
      k = g_new0(omemo_key_t, 1);
      k->rid = to[i].device;
      k->prekey = ciphertext_message_get_type(cm) == CIPHERTEXT_PREKEY_TYPE;
      k->data = g_bytes_new(signal_buffer_data(ser), signal_buffer_len(ser));
      g_ptr_array_add(m->keys, k);
    } else {
      last = r;
    }
    if (cm) SIGNAL_UNREF(cm);
    if (cipher) session_cipher_free(cipher);
  }
  memset(keytag, 0, sizeof keytag);

  if (!m->keys->len) {
    if (last)
      set_sg_error(error, last, "Cannot encrypt");
    else
      g_set_error(error, OMEMO_ERROR, OMEMO_ERROR_NO_SESSION,
                  "No session with any recipient device");
    omemo_message_free(m);
    return NULL;
  }
  return m;
}

gchar *omemo_decrypt(omemo_store *st, const char *jid, guint32 device,
                     const omemo_key_t *key, GBytes *iv, GBytes *payload,
                     GError **error)
{
  signal_protocol_address a;
  session_cipher *cipher = NULL;
  signal_buffer *plain = NULL;
  const guint8 *kdata = g_bytes_get_data(key->data, NULL);
  gsize klen = g_bytes_get_size(key->data);
  gchar *text = NULL;
  int r;

  make_addr(&a, jid, device);
  r = session_cipher_create(&cipher, st->sctx, &a, st->ctx);
  if (r >= 0) {
    if (key->prekey) {
      pre_key_signal_message *pm = NULL;
      r = pre_key_signal_message_deserialize(&pm, kdata, klen, st->ctx);
      if (r >= 0)
        r = session_cipher_decrypt_pre_key_signal_message(cipher, pm, NULL,
                                                          &plain);
      if (pm) SIGNAL_UNREF(pm);
      refill_prekeys(st);
    } else {
      signal_message *sm = NULL;
      r = signal_message_deserialize(&sm, kdata, klen, st->ctx);
      if (r >= 0)
        r = session_cipher_decrypt_signal_message(cipher, sm, NULL, &plain);
      if (sm) SIGNAL_UNREF(sm);
    }
    session_cipher_free(cipher);
  }
  if (r < 0) {
    set_sg_error(error, r, "Cannot decrypt the message key");
    return NULL;
  }

  if (!payload) {
    // Key transport message: only used to set up or advance the session
    signal_buffer_bzero_free(plain);
    return g_strdup("");
  }

  {
    const guint8 *k = signal_buffer_data(plain);
    gsize plen = g_bytes_get_size(payload);
    gsize len;
    guint8 tag[AES_TAG_LEN];
    guint8 *buf;

    if (signal_buffer_len(plain) >= AES_KEY_LEN + AES_TAG_LEN) {
      memcpy(tag, k + AES_KEY_LEN, AES_TAG_LEN);
      len = plen;
    } else if (signal_buffer_len(plain) == AES_KEY_LEN &&
               plen >= AES_TAG_LEN) {
      // Old clients append the tag to the payload
      memcpy(tag, (const guint8 *)g_bytes_get_data(payload, NULL) +
                  plen - AES_TAG_LEN, AES_TAG_LEN);
      len = plen - AES_TAG_LEN;
    } else {
      signal_buffer_bzero_free(plain);
      g_set_error(error, OMEMO_ERROR, OMEMO_ERROR_INVALID,
                  "Bad key length");
      return NULL;
    }
    buf = g_malloc(len + 1);
    memcpy(buf, g_bytes_get_data(payload, NULL), len);
    if (!gcm_crypt(FALSE, k, g_bytes_get_data(iv, NULL), g_bytes_get_size(iv),
                   buf, len, tag)) {
      g_free(buf);
      signal_buffer_bzero_free(plain);
      g_set_error(error, OMEMO_ERROR, OMEMO_ERROR_INVALID,
                  "Payload authentication failed");
      return NULL;
    }
    buf[len] = '\0';
    text = (gchar *)buf;
  }
  signal_buffer_bzero_free(plain);

  if (!g_utf8_validate(text, -1, NULL)) {
    g_free(text);
    g_set_error(error, OMEMO_ERROR, OMEMO_ERROR_INVALID,
                "The message is not valid UTF-8");
    return NULL;
  }
  return text;
}

/* -- Trust --------------------------------------------------------------- */

omemo_trust_t omemo_store_get_trust(omemo_store *st, const char *jid,
                                    guint32 device)
{
  gchar *g = jid_group(jid, strlen(jid)), *k = dev_key(device);
  omemo_trust_t t = identity_lookup(st, g, k, NULL);
  g_free(g);
  g_free(k);
  return t;
}

gboolean omemo_store_set_trust(omemo_store *st, const char *jid,
                               guint32 device, omemo_trust_t trust)
{
  gchar *g = jid_group(jid, strlen(jid)), *k = dev_key(device);
  GBytes *ik;
  gboolean ok = FALSE;

  identity_lookup(st, g, k, &ik);
  if (ik) {
    identity_set(st, g, k, trust, g_bytes_get_data(ik, NULL),
                 g_bytes_get_size(ik));
    g_bytes_unref(ik);
    ok = save_trust(st);
  }
  g_free(g);
  g_free(k);
  return ok;
}

void omemo_store_forget(omemo_store *st, const char *jid, guint32 device)
{
  gchar *g = jid_group(jid, strlen(jid)), *k = dev_key(device);
  if (g_key_file_remove_key(st->trust, g, k, NULL))
    save_trust(st);
  if (g_key_file_remove_key(st->sessions, g, k, NULL))
    save_sessions(st);
  g_free(g);
  g_free(k);
}

static gint cmp_devinfo(gconstpointer a, gconstpointer b)
{
  const omemo_devinfo_t *x = *(omemo_devinfo_t * const *)a;
  const omemo_devinfo_t *y = *(omemo_devinfo_t * const *)b;
  return x->device < y->device ? -1 : x->device > y->device;
}

static void devinfo_free(gpointer p)
{
  omemo_devinfo_t *d = p;
  g_free(d->fingerprint);
  g_free(d);
}

GPtrArray *omemo_store_devices(omemo_store *st, const char *jid,
                               const guint32 *listed, guint nlisted)
{
  GPtrArray *r = g_ptr_array_new_with_free_func(devinfo_free);
  gchar *g = jid_group(jid, strlen(jid));
  gchar **keys = g_key_file_get_keys(st->trust, g, NULL, NULL);
  GHashTable *seen = g_hash_table_new(NULL, NULL);
  guint32 own = omemo_store_device_id(st);
  GArray *ids = g_array_new(FALSE, FALSE, sizeof(guint32));
  guint i;

  for (i = 0; i < nlisted; i++)
    g_array_append_val(ids, listed[i]);
  for (gchar **k = keys; k && *k; k++) {
    guint32 id = (guint32)g_ascii_strtoull(*k, NULL, 10);
    g_array_append_val(ids, id);
  }
  g_strfreev(keys);

  for (i = 0; i < ids->len; i++) {
    guint32 id = g_array_index(ids, guint32, i);
    omemo_devinfo_t *d;
    gchar *k;
    GBytes *ik;

    if (g_hash_table_contains(seen, GUINT_TO_POINTER(id)))
      continue;
    g_hash_table_add(seen, GUINT_TO_POINTER(id));
    k = dev_key(id);
    d = g_new0(omemo_devinfo_t, 1);
    d->device = id;
    d->trust = identity_lookup(st, g, k, &ik);
    if (ik) {
      d->fingerprint = omemo_fingerprint(g_bytes_get_data(ik, NULL),
                                         g_bytes_get_size(ik));
      g_bytes_unref(ik);
    }
    d->session = g_key_file_has_key(st->sessions, g, k, NULL);
    g_free(k);
    if (id == own && !d->fingerprint) {
      devinfo_free(d);
      continue;
    }
    g_ptr_array_add(r, d);
  }
  g_ptr_array_sort(r, cmp_devinfo);
  g_array_free(ids, TRUE);
  g_hash_table_destroy(seen);
  g_free(g);
  return r;
}

/* -- Contacts: policy and device lists ----------------------------------- */

omemo_policy_t omemo_store_get_policy(omemo_store *st, const char *jid)
{
  gchar *g = jid_group(jid, strlen(jid));
  gchar *v = g_key_file_get_string(st->contacts, g, "policy", NULL);
  omemo_policy_t p = OMEMO_POLICY_DEFAULT;
  if (!g_strcmp0(v, "on"))
    p = OMEMO_POLICY_ON;
  else if (!g_strcmp0(v, "off"))
    p = OMEMO_POLICY_OFF;
  g_free(v);
  g_free(g);
  return p;
}

void omemo_store_set_policy(omemo_store *st, const char *jid,
                            omemo_policy_t policy)
{
  gchar *g = jid_group(jid, strlen(jid));
  if (policy == OMEMO_POLICY_DEFAULT)
    g_key_file_remove_key(st->contacts, g, "policy", NULL);
  else
    g_key_file_set_string(st->contacts, g, "policy",
                          policy == OMEMO_POLICY_ON ? "on" : "off");
  save_contacts(st);
  g_free(g);
}

gboolean omemo_store_get_devices(omemo_store *st, const char *jid,
                                 GArray **devices)
{
  gchar *g = jid_group(jid, strlen(jid));
  gsize n = 0;
  gchar **list;

  *devices = NULL;
  if (!g_key_file_has_key(st->contacts, g, "devices", NULL)) {
    g_free(g);
    return FALSE;
  }
  list = g_key_file_get_string_list(st->contacts, g, "devices", &n, NULL);
  *devices = g_array_new(FALSE, FALSE, sizeof(guint32));
  for (gsize i = 0; i < n; i++) {
    guint32 id = (guint32)g_ascii_strtoull(list[i], NULL, 10);
    if (id)
      g_array_append_val(*devices, id);
  }
  g_strfreev(list);
  g_free(g);
  return TRUE;
}

void omemo_store_set_devices(omemo_store *st, const char *jid,
                             const guint32 *devices, guint count)
{
  gchar *g = jid_group(jid, strlen(jid));
  gchar **list = g_new0(gchar *, count + 1);
  guint i;

  for (i = 0; i < count; i++)
    list[i] = dev_key(devices[i]);
  g_key_file_set_string_list(st->contacts, g, "devices",
                             (const gchar * const *)list, count);
  g_strfreev(list);
  g_free(g);
  save_contacts(st);
}

/* -- Fingerprints -------------------------------------------------------- */

gchar *omemo_fingerprint(const guint8 *key, gsize len)
{
  GString *s = g_string_new(NULL);
  gsize i;

  if (len == 33 && key[0] == 0x05) {    // Skip the key type
    key++;
    len--;
  }
  for (i = 0; i < len; i++) {
    if (i && !(i % 4))
      g_string_append_c(s, ' ');
    g_string_append_printf(s, "%02x", key[i]);
  }
  return g_string_free(s, FALSE);
}

static gchar *fpr_normalize(const char *s)
{
  GString *r = g_string_new(NULL);
  for (; *s; s++)
    if (g_ascii_isxdigit(*s))
      g_string_append_c(r, g_ascii_tolower(*s));
    else if (*s != ' ' && *s != ':' && *s != '-') {
      g_string_free(r, TRUE);
      return NULL;
    }
  return g_string_free(r, FALSE);
}

//  omemo_fingerprint_match(fpr, input)
// The input must be the whole fingerprint, or at least 8 hex digits of its
// beginning.
gboolean omemo_fingerprint_match(const char *fpr, const char *input)
{
  gchar *a = fpr_normalize(fpr), *b = fpr_normalize(input);
  gboolean r = a && b && strlen(b) >= 8 && g_str_has_prefix(a, b);
  g_free(a);
  g_free(b);
  return r;
}

/* vim: set et cindent cinoptions=>2\:2(0 ts=2 sw=2:  For Vim users... */
