// Tests for the OMEMO key store and cryptography (omemo_core.c): two
// stores exchange messages without any network.

#include <string.h>
#include <glib.h>
#include <glib/gstdio.h>

#include "omemo_core.h"

static gchar *tmpdir;

static gchar *store_dir(const char *name)
{
  return g_build_filename(tmpdir, name, NULL);
}

static omemo_store *open_store(const char *name, gboolean blind)
{
  gchar *dir = store_dir(name);
  GError *err = NULL;
  omemo_store *st = omemo_store_open(dir, blind, &err);
  g_assert_no_error(err);
  g_assert_nonnull(st);
  g_free(dir);
  return st;
}

static gchar *decrypt(omemo_store *to, const char *from_jid, guint32 from_dev,
                      omemo_message_t *m, GError **err)
{
  guint32 me = omemo_store_device_id(to);
  guint i;
  for (i = 0; i < m->keys->len; i++) {
    omemo_key_t *k = g_ptr_array_index(m->keys, i);
    if (k->rid == me)
      return omemo_decrypt(to, from_jid, from_dev, k, m->iv, m->payload,
                           err);
  }
  g_assert_not_reached();
  return NULL;
}

static omemo_message_t *encrypt_to(omemo_store *from, const char *text,
                                   const char *jid, guint32 dev)
{
  omemo_addr_t a = { jid, dev };
  GError *err = NULL;
  omemo_message_t *m = omemo_encrypt(from, text, &a, 1, &err);
  g_assert_no_error(err);
  g_assert_nonnull(m);
  return m;
}

static void test_fingerprint(void)
{
  guint8 key[33];
  gchar *fpr;
  int i;

  key[0] = 0x05;
  for (i = 1; i < 33; i++)
    key[i] = (guint8)i;
  fpr = omemo_fingerprint(key, sizeof key);
  g_assert_cmpstr(fpr, ==, "01020304 05060708 090a0b0c 0d0e0f10 "
                           "11121314 15161718 191a1b1c 1d1e1f20");
  g_assert_true(omemo_fingerprint_match(fpr, "01020304"));
  g_assert_true(omemo_fingerprint_match(fpr, "01:02:03:04:05:06:07:08"));
  g_assert_true(omemo_fingerprint_match(fpr, "0102030405060708090A0B0C"));
  g_assert_false(omemo_fingerprint_match(fpr, "0102"));        // Too short
  g_assert_false(omemo_fingerprint_match(fpr, "02030405"));
  g_assert_false(omemo_fingerprint_match(fpr, "0102030x"));
  g_free(fpr);
}

static void test_conversation(void)
{
  omemo_store *alice = open_store("alice", TRUE);
  omemo_store *bob = open_store("bob", TRUE);
  guint32 adev = omemo_store_device_id(alice);
  guint32 bdev = omemo_store_device_id(bob);
  omemo_bundle_t *bb;
  omemo_message_t *m;
  GError *err = NULL;
  gchar *text;
  omemo_key_t *k;

  g_assert_cmpuint(adev, !=, 0);
  g_assert_cmpuint(adev, !=, bdev);
  g_assert_true(omemo_store_bundle_changed(alice));   // New identity
  g_assert_false(omemo_store_bundle_changed(alice));

  bb = omemo_store_bundle(bob);
  g_assert_nonnull(bb);
  g_assert_cmpuint(bb->prekeys->len, ==, 100);
  g_assert_cmpuint(g_bytes_get_size(bb->ik), ==, 33);

  // Alice builds a session from Bob's bundle and writes first
  g_assert_false(omemo_store_has_session(alice, "bob@example.org", bdev));
  g_assert_true(omemo_store_build_session(alice, "bob@example.org", bdev, bb,
                                          &err));
  g_assert_no_error(err);
  omemo_bundle_free(bb);
  g_assert_true(omemo_store_has_session(alice, "bob@example.org", bdev));
  g_assert_cmpint(omemo_store_get_trust(alice, "bob@example.org", bdev), ==,
                  OMEMO_TRUST_BLIND);

  m = encrypt_to(alice, "Hello, Bob! Привет 👋", "bob@example.org", bdev);
  k = g_ptr_array_index(m->keys, 0);
  g_assert_true(k->prekey);
  g_assert_cmpuint(g_bytes_get_size(m->iv), ==, 12);

  text = decrypt(bob, "alice@example.org", adev, m, &err);
  g_assert_no_error(err);
  g_assert_cmpstr(text, ==, "Hello, Bob! Привет 👋");
  g_free(text);
  g_assert_cmpint(omemo_store_get_trust(bob, "alice@example.org", adev), ==,
                  OMEMO_TRUST_BLIND);

  // The pre-key used by Alice has been replaced
  g_assert_true(omemo_store_bundle_changed(bob));
  bb = omemo_store_bundle(bob);
  g_assert_cmpuint(bb->prekeys->len, ==, 100);
  omemo_bundle_free(bb);

  // The same message again: duplicate
  text = decrypt(bob, "alice@example.org", adev, m, &err);
  g_assert_null(text);
  g_assert_error(err, OMEMO_ERROR, OMEMO_ERROR_DUPLICATE);
  g_clear_error(&err);
  omemo_message_free(m);

  // Bob answers in the established session
  m = encrypt_to(bob, "Hi Alice", "alice@example.org", adev);
  k = g_ptr_array_index(m->keys, 0);
  g_assert_false(k->prekey);
  text = decrypt(alice, "bob@example.org", bdev, m, &err);
  g_assert_no_error(err);
  g_assert_cmpstr(text, ==, "Hi Alice");
  g_free(text);
  omemo_message_free(m);

  // Alice's next message is a normal one too
  m = encrypt_to(alice, "", "bob@example.org", bdev);
  k = g_ptr_array_index(m->keys, 0);
  g_assert_false(k->prekey);

  // Key transport (no payload) also decrypts
  {
    omemo_key_t *kk = g_ptr_array_index(m->keys, 0);
    text = omemo_decrypt(bob, "alice@example.org", adev, kk, m->iv, NULL,
                         &err);
    g_assert_no_error(err);
    g_assert_cmpstr(text, ==, "");
    g_free(text);
  }
  omemo_message_free(m);

  // Everything survives a restart
  omemo_store_close(bob);
  bob = open_store("bob", TRUE);
  g_assert_cmpuint(omemo_store_device_id(bob), ==, bdev);
  m = encrypt_to(alice, "After restart", "bob@example.org", bdev);
  text = decrypt(bob, "alice@example.org", adev, m, &err);
  g_assert_no_error(err);
  g_assert_cmpstr(text, ==, "After restart");
  g_free(text);

  omemo_message_free(m);

  // A tampered payload is rejected
  m = encrypt_to(alice, "Tampered", "bob@example.org", bdev);
  {
    gsize len;
    guint8 *p = g_bytes_unref_to_data(m->payload, &len);
    p[0] ^= 1;
    m->payload = g_bytes_new_take(p, len);
  }
  text = decrypt(bob, "alice@example.org", adev, m, &err);
  g_assert_null(text);
  g_assert_error(err, OMEMO_ERROR, OMEMO_ERROR_INVALID);
  g_clear_error(&err);
  omemo_message_free(m);

  omemo_store_close(alice);
  omemo_store_close(bob);
}

static void test_trust(void)
{
  omemo_store *carol = open_store("carol", TRUE);
  omemo_store *d1 = open_store("dave1", TRUE);
  omemo_store *d2 = open_store("dave2", TRUE);
  guint32 dev1 = omemo_store_device_id(d1), dev2 = omemo_store_device_id(d2);
  omemo_bundle_t *b;
  GError *err = NULL;
  GPtrArray *devs;
  guint32 listed[2] = { dev1, dev2 };
  omemo_addr_t to[2] = { { "dave@example.org", dev1 },
                         { "dave@example.org", dev2 } };
  omemo_message_t *m;

  b = omemo_store_bundle(d1);
  g_assert_true(omemo_store_build_session(carol, "dave@example.org", dev1, b,
                                          &err));
  omemo_bundle_free(b);
  g_assert_cmpint(omemo_store_get_trust(carol, "dave@example.org", dev1), ==,
                  OMEMO_TRUST_BLIND);

  // Once a device is verified, new devices are not trusted automatically
  g_assert_true(omemo_store_set_trust(carol, "dave@example.org", dev1,
                                      OMEMO_TRUST_VERIFIED));
  b = omemo_store_bundle(d2);
  g_assert_true(omemo_store_build_session(carol, "dave@example.org", dev2, b,
                                          &err));
  omemo_bundle_free(b);
  g_assert_cmpint(omemo_store_get_trust(carol, "dave@example.org", dev2), ==,
                  OMEMO_TRUST_UNDECIDED);
  g_assert_false(omemo_trust_usable(OMEMO_TRUST_UNDECIDED));

  devs = omemo_store_devices(carol, "dave@example.org", listed, 2);
  g_assert_cmpuint(devs->len, ==, 2);
  {
    omemo_devinfo_t *d = g_ptr_array_index(devs, 0);
    g_assert_nonnull(d->fingerprint);
    g_assert_true(d->session);
  }
  g_ptr_array_free(devs, TRUE);

  // Encrypting for both gives two keys; the caller filters by trust
  m = omemo_encrypt(carol, "x", to, 2, &err);
  g_assert_no_error(err);
  g_assert_cmpuint(m->keys->len, ==, 2);
  omemo_message_free(m);

  // A distrusted device: sessions can't be used any more
  g_assert_true(omemo_store_set_trust(carol, "dave@example.org", dev2,
                                      OMEMO_TRUST_DISTRUSTED));
  m = omemo_encrypt(carol, "y", &to[1], 1, &err);
  g_assert_null(m);
  g_assert_nonnull(err);
  g_clear_error(&err);

  // Unknown device: no session
  {
    omemo_addr_t none = { "nobody@example.org", 42 };
    m = omemo_encrypt(carol, "z", &none, 1, &err);
    g_assert_null(m);
    g_assert_error(err, OMEMO_ERROR, OMEMO_ERROR_NO_SESSION);
    g_clear_error(&err);
  }

  // Manual trust: nothing is trusted automatically
  {
    omemo_store *eve = open_store("eve", FALSE);
    b = omemo_store_bundle(d1);
    g_assert_true(omemo_store_build_session(eve, "dave@example.org", dev1, b,
                                            &err));
    omemo_bundle_free(b);
    g_assert_cmpint(omemo_store_get_trust(eve, "dave@example.org", dev1), ==,
                    OMEMO_TRUST_UNDECIDED);
    omemo_store_close(eve);
  }

  omemo_store_close(carol);
  omemo_store_close(d1);
  omemo_store_close(d2);
}

static void test_contacts(void)
{
  omemo_store *st = open_store("frank", TRUE);
  GArray *devs;
  guint32 list[3] = { 7, 4294967295u, 12 };

  g_assert_false(omemo_store_get_devices(st, "g@example.org", &devs));
  g_assert_null(devs);
  omemo_store_set_devices(st, "g@example.org", list, 3);
  g_assert_true(omemo_store_get_devices(st, "g@example.org", &devs));
  g_assert_cmpuint(devs->len, ==, 3);
  g_assert_cmpuint(g_array_index(devs, guint32, 1), ==, 4294967295u);
  g_array_free(devs, TRUE);
  omemo_store_set_devices(st, "h@example.org", NULL, 0);
  g_assert_true(omemo_store_get_devices(st, "h@example.org", &devs));
  g_assert_cmpuint(devs->len, ==, 0);
  g_array_free(devs, TRUE);

  // A JID with characters that are special in key files
  g_assert_cmpint(omemo_store_get_policy(st, "a[b]=c@example.org"), ==,
                  OMEMO_POLICY_DEFAULT);
  omemo_store_set_policy(st, "a[b]=c@example.org", OMEMO_POLICY_OFF);
  omemo_store_close(st);
  st = open_store("frank", TRUE);
  g_assert_cmpint(omemo_store_get_policy(st, "a[b]=c@example.org"), ==,
                  OMEMO_POLICY_OFF);
  omemo_store_set_policy(st, "a[b]=c@example.org", OMEMO_POLICY_DEFAULT);
  g_assert_cmpint(omemo_store_get_policy(st, "a[b]=c@example.org"), ==,
                  OMEMO_POLICY_DEFAULT);
  omemo_store_close(st);
}

static void rm_rf(const char *path)
{
  GDir *d = g_dir_open(path, 0, NULL);
  const char *name;
  if (d) {
    while ((name = g_dir_read_name(d))) {
      gchar *p = g_build_filename(path, name, NULL);
      rm_rf(p);
      g_free(p);
    }
    g_dir_close(d);
    g_rmdir(path);
  } else {
    g_unlink(path);
  }
}

int main(int argc, char **argv)
{
  int r;
  g_test_init(&argc, &argv, NULL);
  tmpdir = g_dir_make_tmp("mcng-omemo-XXXXXX", NULL);
  g_test_add_func("/omemo/fingerprint", test_fingerprint);
  g_test_add_func("/omemo/conversation", test_conversation);
  g_test_add_func("/omemo/trust", test_trust);
  g_test_add_func("/omemo/contacts", test_contacts);
  r = g_test_run();
  rm_rf(tmpdir);
  g_free(tmpdir);
  return r;
}
