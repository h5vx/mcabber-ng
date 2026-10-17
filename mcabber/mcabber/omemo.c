/*
 * omemo.c          -- OMEMO end-to-end encryption (XEP-0384 v0.3)
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

// The XMPP side of OMEMO: device lists and bundles (PEP), sending and
// receiving encrypted messages, the /omemo command.  Only one-to-one chats
// are supported.

#include <string.h>
#include <stdlib.h>

#include "config.h"
#include "omemo.h"
#include "xmpp.h"
#include "xmpp_helper.h"
#include "xmpp_defines.h"
#include "settings.h"
#include "roster.h"
#include "screen.h"
#include "logprint.h"
#include "utils.h"

#define NS_HINTS          "urn:xmpp:hints"
#define NS_EME            "urn:xmpp:eme:0"
#define NS_XDATA          "jabber:x:data"

static omemo_store *store;
static gchar *store_jid;      // Bare JID of the account using the store
static gboolean online;

// Messages waiting for device lists or bundles, per bare JID
typedef struct {
  LmMessage *m;
  gchar *text;
} queued_t;

typedef struct {
  GSList *msgs;               // queued_t*
} pending_t;

static GHashTable *pending;   // bare JID -> pending_t*
static GHashTable *requested; // "jid/device" or "jid" -> request in progress
static GHashTable *failed;    // "jid" or "jid/device" that can't be fetched
static GHashTable *warned;    // "jid/device" already reported as untrusted

static void flush_all(void);
static void fetch_devicelist(const char *jid);

gboolean omemo_enabled(void)
{
  const char *v = settings_opt_get("omemo");
  return !v || atoi(v) != 0;
}

static gboolean blind_trust(void)
{
  return g_strcmp0(settings_opt_get("omemo_trust"), "manual") != 0;
}

/* -- Helpers ------------------------------------------------------------- */

static void pending_free(gpointer p)
{
  pending_t *pd = p;
  GSList *l;
  for (l = pd->msgs; l; l = l->next) {
    queued_t *q = l->data;
    lm_message_unref(q->m);
    g_free(q->text);
    g_free(q);
  }
  g_slist_free(pd->msgs);
  g_free(pd);
}

static pending_t *pending_get(const char *jid)
{
  pending_t *pd = g_hash_table_lookup(pending, jid);
  if (!pd) {
    pd = g_new0(pending_t, 1);
    g_hash_table_insert(pending, g_strdup(jid), pd);
  }
  return pd;
}

static gchar *dev_name(const char *jid, guint32 device)
{
  return g_strdup_printf("%s/%u", jid, device);
}

static void buddy_info(const char *jid, const char *fmt, ...)
  G_GNUC_PRINTF(2, 3);
static void buddy_info(const char *jid, const char *fmt, ...)
{
  va_list ap;
  gchar *msg;
  va_start(ap, fmt);
  msg = g_strdup_vprintf(fmt, ap);
  va_end(ap);
  scr_WriteIncomingMessage(jid, msg, 0, HBB_PREFIX_INFO | HBB_PREFIX_NOFLAG,
                           0);
  g_free(msg);
}

static void buddy_error(const char *jid, const char *fmt, ...)
  G_GNUC_PRINTF(2, 3);
static void buddy_error(const char *jid, const char *fmt, ...)
{
  va_list ap;
  gchar *msg;
  va_start(ap, fmt);
  msg = g_strdup_vprintf(fmt, ap);
  va_end(ap);
  scr_WriteIncomingMessage(jid, msg, 0, HBB_PREFIX_ERR, 0);
  g_free(msg);
}

static GBytes *node_bytes(LmMessageNode *node)
{
  const char *v = node ? lm_message_node_get_value(node) : NULL;
  guchar *data;
  gsize len;
  if (!v)
    return NULL;
  data = g_base64_decode(v, &len);
  if (!len) {
    g_free(data);
    return NULL;
  }
  return g_bytes_new_take(data, len);
}

static void add_b64_child(LmMessageNode *parent, const char *name,
                          GBytes *b, const char *attr, guint32 id)
{
  gchar *v = g_base64_encode(g_bytes_get_data(b, NULL), g_bytes_get_size(b));
  LmMessageNode *n = lm_message_node_add_child(parent, name, v);
  if (attr) {
    gchar *s = g_strdup_printf("%u", id);
    lm_message_node_set_attribute(n, attr, s);
    g_free(s);
  }
  g_free(v);
}

static guint32 attr_u32(LmMessageNode *node, const char *attr)
{
  const char *v = lm_message_node_get_attribute(node, attr);
  return v ? (guint32)g_ascii_strtoull(v, NULL, 10) : 0;
}

static LmMessage *pubsub_iq(const char *to, LmMessageSubType type,
                            LmMessageNode **pubsub)
{
  LmMessage *iq = lm_message_new_with_sub_type(to, LM_MESSAGE_TYPE_IQ, type);
  *pubsub = lm_message_node_add_child(iq->node, "pubsub", NULL);
  lm_message_node_set_attribute(*pubsub, "xmlns", NS_PUBSUB);
  return iq;
}

static void send_iq(LmMessage *iq, LmHandleMessageFunction cb, gpointer data,
                    GDestroyNotify destroy)
{
  // The data is released with the handler
  LmMessageHandler *h = lm_message_handler_new(cb, data, destroy);
  lm_connection_send_with_reply(lconnection, iq, h, NULL);
  lm_message_handler_unref(h);
  lm_message_unref(iq);
}

static const char *iq_error_name(LmMessage *m)
{
  LmMessageNode *e = lm_message_node_get_child(m->node, "error");
  LmMessageNode *c;
  if (!e)
    return "error";
  for (c = e->children; c; c = c->next)
    if (!g_strcmp0(lm_message_node_get_attribute(c, "xmlns"),
                   NS_XMPP_STANZAS) && g_strcmp0(c->name, "text"))
      return c->name;
  return "error";
}

/* -- Device lists -------------------------------------------------------- */

static GArray *parse_devicelist(LmMessageNode *items)
{
  GArray *devices = g_array_new(FALSE, FALSE, sizeof(guint32));
  LmMessageNode *item, *list, *dev;

  for (item = items ? items->children : NULL; item; item = item->next) {
    if (g_strcmp0(item->name, "item"))
      continue;
    list = lm_message_node_find_xmlns(item, OMEMO_NS);
    if (!list || g_strcmp0(list->name, "list"))
      continue;
    for (dev = list->children; dev; dev = dev->next) {
      guint32 id;
      if (g_strcmp0(dev->name, "device"))
        continue;
      id = attr_u32(dev, "id");
      if (id)
        g_array_append_val(devices, id);
    }
    break;    // Only the first (current) item
  }
  return devices;
}

static void publish_devicelist(GArray *devices, gboolean with_options);

static LmHandlerResult devicelist_published_cb(LmMessageHandler *h,
                                               LmConnection *c,
                                               LmMessage *m, gpointer data)
{
  GArray *devices = data;
  if (lm_message_get_sub_type(m) == LM_MESSAGE_SUB_TYPE_ERROR) {
    if (devices) {
      // Maybe the server does not support publish-options
      publish_devicelist(devices, FALSE);
    } else {
      scr_LogPrint(LPRINT_LOGNORM, "OMEMO: cannot publish the device list "
                   "(%s).", iq_error_name(m));
    }
  }
  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

static void array_free(gpointer p)
{
  g_array_free(p, TRUE);
}

static void add_publish_options(LmMessageNode *pubsub)
{
  LmMessageNode *po, *x, *f;
  po = lm_message_node_add_child(pubsub, "publish-options", NULL);
  x = lm_message_node_add_child(po, "x", NULL);
  lm_message_node_set_attributes(x, "xmlns", NS_XDATA, "type", "submit",
                                 NULL);
  f = lm_message_node_add_child(x, "field", NULL);
  lm_message_node_set_attributes(f, "var", "FORM_TYPE", "type", "hidden",
                                 NULL);
  lm_message_node_add_child(f, "value",
                            "http://jabber.org/protocol/pubsub#publish-options");
  f = lm_message_node_add_child(x, "field", NULL);
  lm_message_node_set_attribute(f, "var", "pubsub#access_model");
  lm_message_node_add_child(f, "value", "open");
}

static void publish_devicelist(GArray *devices, gboolean with_options)
{
  LmMessageNode *pubsub, *publish, *item, *list;
  LmMessage *iq = pubsub_iq(NULL, LM_MESSAGE_SUB_TYPE_SET, &pubsub);
  guint i;

  publish = lm_message_node_add_child(pubsub, "publish", NULL);
  lm_message_node_set_attribute(publish, "node", OMEMO_NODE_DEVICELIST);
  item = lm_message_node_add_child(publish, "item", NULL);
  lm_message_node_set_attribute(item, "id", "current");
  list = lm_message_node_add_child(item, "list", NULL);
  lm_message_node_set_attribute(list, "xmlns", OMEMO_NS);
  for (i = 0; i < devices->len; i++) {
    LmMessageNode *d = lm_message_node_add_child(list, "device", NULL);
    gchar *id = g_strdup_printf("%u", g_array_index(devices, guint32, i));
    lm_message_node_set_attribute(d, "id", id);
    g_free(id);
  }
  if (with_options)
    add_publish_options(pubsub);

  send_iq(iq, devicelist_published_cb,
          with_options ? g_array_copy(devices) : NULL,
          with_options ? array_free : NULL);
}

// Make sure our device is in our own device list
static void check_own_devicelist(GArray *devices)
{
  guint32 own = omemo_store_device_id(store);
  guint i;
  for (i = 0; i < devices->len; i++)
    if (g_array_index(devices, guint32, i) == own)
      return;
  {
    GArray *list = g_array_copy(devices);
    g_array_append_val(list, own);
    publish_devicelist(list, TRUE);
    g_array_free(list, TRUE);
  }
}

static void devicelist_update(const char *jid, GArray *devices)
{
  omemo_store_set_devices(store, jid, (guint32 *)(void *)devices->data,
                          devices->len);
  if (!g_strcmp0(jid, store_jid))
    check_own_devicelist(devices);
}

static LmHandlerResult devicelist_cb(LmMessageHandler *h, LmConnection *c,
                                     LmMessage *m, gpointer data)
{
  const char *jid = data;

  g_hash_table_remove(requested, jid);
  if (!store)
    return LM_HANDLER_RESULT_REMOVE_MESSAGE;

  if (lm_message_get_sub_type(m) == LM_MESSAGE_SUB_TYPE_RESULT) {
    LmMessageNode *ps = lm_message_node_find_xmlns(m->node, NS_PUBSUB);
    GArray *devices = parse_devicelist(ps ? lm_message_node_get_child(ps,
                                                       "items") : NULL);
    devicelist_update(jid, devices);
    g_array_free(devices, TRUE);
  } else {
    const char *err = iq_error_name(m);
    // No list: no OMEMO.  Other errors are not remembered.
    if (!strcmp(err, "item-not-found")) {
      GArray *none = g_array_new(FALSE, FALSE, sizeof(guint32));
      devicelist_update(jid, none);
      g_array_free(none, TRUE);
    } else {
      g_hash_table_add(failed, g_strdup(jid));
      scr_LogPrint(LPRINT_DEBUG, "OMEMO: cannot fetch the device list of "
                   "<%s> (%s).", jid, err);
    }
  }
  flush_all();
  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

// Returns FALSE if a request for jid is already in progress
static gboolean request_devicelist(const char *jid)
{
  LmMessageNode *pubsub, *items;
  LmMessage *iq;

  if (g_hash_table_contains(requested, jid))
    return FALSE;
  g_hash_table_add(requested, g_strdup(jid));
  iq = pubsub_iq(jid, LM_MESSAGE_SUB_TYPE_GET, &pubsub);
  items = lm_message_node_add_child(pubsub, "items", NULL);
  lm_message_node_set_attribute(items, "node", OMEMO_NODE_DEVICELIST);
  send_iq(iq, devicelist_cb, g_strdup(jid), g_free);
  return TRUE;
}

static void fetch_devicelist(const char *jid)
{
  request_devicelist(jid);
}

gboolean omemo_handle_event(const char *from, LmMessageNode *event)
{
  LmMessageNode *items;
  GArray *devices;
  gchar *jid;

  if (!store || !event)
    return FALSE;
  items = lm_message_node_get_child(event, "items");
  if (!items || g_strcmp0(lm_message_node_get_attribute(items, "node"),
                          OMEMO_NODE_DEVICELIST))
    return FALSE;

  jid = from ? jidtodisp(from) : g_strdup(store_jid);
  devices = parse_devicelist(items);
  devicelist_update(jid, devices);
  scr_LogPrint(LPRINT_DEBUG, "OMEMO: <%s> has %u device(s).", jid,
               devices->len);
  g_array_free(devices, TRUE);
  g_free(jid);
  return TRUE;
}

/* -- Bundles ------------------------------------------------------------- */

static LmHandlerResult bundle_published_cb(LmMessageHandler *h,
                                           LmConnection *c,
                                           LmMessage *m, gpointer data);

static void publish_bundle_with(gboolean with_options)
{
  omemo_bundle_t *b = omemo_store_bundle(store);
  LmMessageNode *pubsub, *publish, *item, *bundle, *pks;
  LmMessage *iq;
  gchar *node;
  guint i;

  if (!b) {
    scr_LogPrint(LPRINT_LOGNORM, "OMEMO: cannot read our own keys.");
    return;
  }
  iq = pubsub_iq(NULL, LM_MESSAGE_SUB_TYPE_SET, &pubsub);
  publish = lm_message_node_add_child(pubsub, "publish", NULL);
  node = g_strdup_printf(OMEMO_NODE_BUNDLES "%u",
                         omemo_store_device_id(store));
  lm_message_node_set_attribute(publish, "node", node);
  g_free(node);
  item = lm_message_node_add_child(publish, "item", NULL);
  lm_message_node_set_attribute(item, "id", "current");
  bundle = lm_message_node_add_child(item, "bundle", NULL);
  lm_message_node_set_attribute(bundle, "xmlns", OMEMO_NS);
  add_b64_child(bundle, "signedPreKeyPublic", b->spk, "signedPreKeyId",
                b->spk_id);
  add_b64_child(bundle, "signedPreKeySignature", b->spk_sig, NULL, 0);
  add_b64_child(bundle, "identityKey", b->ik, NULL, 0);
  pks = lm_message_node_add_child(bundle, "prekeys", NULL);
  for (i = 0; i < b->prekeys->len; i++) {
    omemo_prekey_t *p = &g_array_index(b->prekeys, omemo_prekey_t, i);
    add_b64_child(pks, "preKeyPublic", p->key, "preKeyId", p->id);
  }
  if (with_options)
    add_publish_options(pubsub);
  omemo_bundle_free(b);
  send_iq(iq, bundle_published_cb, GINT_TO_POINTER(with_options), NULL);
}

static LmHandlerResult bundle_published_cb(LmMessageHandler *h,
                                           LmConnection *c,
                                           LmMessage *m, gpointer data)
{
  if (lm_message_get_sub_type(m) == LM_MESSAGE_SUB_TYPE_ERROR) {
    if (GPOINTER_TO_INT(data))
      publish_bundle_with(FALSE);
    else
      scr_LogPrint(LPRINT_LOGNORM, "OMEMO: cannot publish our keys (%s).",
                   iq_error_name(m));
  }
  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

static void publish_bundle(void)
{
  if (store && online)
    publish_bundle_with(TRUE);
}

static omemo_bundle_t *parse_bundle(LmMessageNode *items)
{
  LmMessageNode *item = items ? lm_message_node_get_child(items, "item")
                              : NULL;
  LmMessageNode *bundle = item ? lm_message_node_find_xmlns(item, OMEMO_NS)
                               : NULL;
  LmMessageNode *spk, *pks, *p;
  omemo_bundle_t *b;

  if (!bundle || g_strcmp0(bundle->name, "bundle"))
    return NULL;
  b = omemo_bundle_new();
  spk = lm_message_node_get_child(bundle, "signedPreKeyPublic");
  b->spk = node_bytes(spk);
  b->spk_id = spk ? attr_u32(spk, "signedPreKeyId") : 0;
  b->spk_sig = node_bytes(lm_message_node_get_child(bundle,
                                                    "signedPreKeySignature"));
  b->ik = node_bytes(lm_message_node_get_child(bundle, "identityKey"));
  pks = lm_message_node_get_child(bundle, "prekeys");
  for (p = pks ? pks->children : NULL; p; p = p->next) {
    omemo_prekey_t pk;
    if (g_strcmp0(p->name, "preKeyPublic"))
      continue;
    pk.id = attr_u32(p, "preKeyId");
    pk.key = node_bytes(p);
    if (pk.key)
      g_array_append_val(b->prekeys, pk);
  }
  return b;
}

typedef struct {
  gchar *jid;
  guint32 device;
} bundle_req_t;

static void bundle_req_free(gpointer p)
{
  bundle_req_t *r = p;
  g_free(r->jid);
  g_free(r);
}

static LmHandlerResult bundle_cb(LmMessageHandler *h, LmConnection *c,
                                 LmMessage *m, gpointer data)
{
  bundle_req_t *r = data;
  gchar *name = dev_name(r->jid, r->device);

  g_hash_table_remove(requested, name);
  if (!store) {
    g_free(name);
    return LM_HANDLER_RESULT_REMOVE_MESSAGE;
  }

  if (lm_message_get_sub_type(m) == LM_MESSAGE_SUB_TYPE_RESULT) {
    LmMessageNode *ps = lm_message_node_find_xmlns(m->node, NS_PUBSUB);
    omemo_bundle_t *b = parse_bundle(ps ? lm_message_node_get_child(ps,
                                                       "items") : NULL);
    GError *err = NULL;
    if (!b || !omemo_store_build_session(store, r->jid, r->device, b,
                                         &err)) {
      scr_LogPrint(LPRINT_LOGNORM, "OMEMO: bad keys for device %u of <%s>"
                   "%s%s.", r->device, r->jid, err ? ": " : "",
                   err ? err->message : "");
      g_hash_table_add(failed, g_strdup(name));
      g_clear_error(&err);
    }
    omemo_bundle_free(b);
  } else {
    scr_LogPrint(LPRINT_LOGNORM, "OMEMO: cannot fetch the keys of device "
                 "%u of <%s> (%s).", r->device, r->jid, iq_error_name(m));
    g_hash_table_add(failed, g_strdup(name));
  }
  g_free(name);
  flush_all();
  return LM_HANDLER_RESULT_REMOVE_MESSAGE;
}

static gboolean request_bundle(const char *jid, guint32 device)
{
  LmMessageNode *pubsub, *items;
  LmMessage *iq;
  bundle_req_t *r;
  gchar *name = dev_name(jid, device), *node;

  if (g_hash_table_contains(requested, name)) {
    g_free(name);
    return FALSE;
  }
  g_hash_table_add(requested, name);
  iq = pubsub_iq(jid, LM_MESSAGE_SUB_TYPE_GET, &pubsub);
  items = lm_message_node_add_child(pubsub, "items", NULL);
  node = g_strdup_printf(OMEMO_NODE_BUNDLES "%u", device);
  lm_message_node_set_attributes(items, "node", node, "max_items", "1",
                                 NULL);
  g_free(node);
  r = g_new0(bundle_req_t, 1);
  r->jid = g_strdup(jid);
  r->device = device;
  send_iq(iq, bundle_cb, r, bundle_req_free);
  return TRUE;
}

/* -- Sending ------------------------------------------------------------- */

// Request what is missing to encrypt for jid's devices.  Returns TRUE if
// a device list or a bundle is still awaited.
static gboolean keys_awaited(const char *jid)
{
  GArray *devices;
  guint32 own = omemo_store_device_id(store);
  gboolean awaited = FALSE;
  guint i;

  if (!omemo_store_get_devices(store, jid, &devices)) {
    if (g_hash_table_contains(failed, jid))
      return FALSE;
    request_devicelist(jid);
    return TRUE;
  }
  for (i = 0; i < devices->len; i++) {
    guint32 d = g_array_index(devices, guint32, i);
    gchar *name;
    if (!g_strcmp0(jid, store_jid) && d == own)
      continue;
    if (omemo_store_has_session(store, jid, d) ||
        omemo_store_get_trust(store, jid, d) == OMEMO_TRUST_DISTRUSTED)
      continue;
    name = dev_name(jid, d);
    if (!g_hash_table_contains(failed, name)) {
      request_bundle(jid, d);
      awaited = TRUE;
    }
    g_free(name);
  }
  g_array_free(devices, TRUE);
  return awaited;
}

// Add the usable devices of jid to the recipients, and report the others.
static guint collect(const char *jid, GArray *to, GString *skipped)
{
  GArray *devices;
  guint32 own = omemo_store_device_id(store);
  guint i, n = 0;

  if (!omemo_store_get_devices(store, jid, &devices))
    return 0;
  for (i = 0; i < devices->len; i++) {
    guint32 d = g_array_index(devices, guint32, i);
    omemo_trust_t t;
    if (!g_strcmp0(jid, store_jid) && d == own)
      continue;
    if (!omemo_store_has_session(store, jid, d))
      continue;
    t = omemo_store_get_trust(store, jid, d);
    if (omemo_trust_usable(t)) {
      omemo_addr_t a = { jid, d };
      g_array_append_val(to, a);
      n++;
    } else if (t == OMEMO_TRUST_UNDECIDED && skipped) {
      gchar *name = dev_name(jid, d);
      if (!g_hash_table_contains(warned, name)) {
        g_hash_table_add(warned, name);
        g_string_append_printf(skipped, " %s:%u", jid, d);
      } else {
        g_free(name);
      }
    }
  }
  g_array_free(devices, TRUE);
  return n;
}

static void add_encrypted(LmMessage *m, omemo_message_t *em)
{
  LmMessageNode *enc, *header, *n;
  gchar *sid = g_strdup_printf("%u", omemo_store_device_id(store));
  guint i;

  enc = lm_message_node_add_child(m->node, "encrypted", NULL);
  lm_message_node_set_attribute(enc, "xmlns", OMEMO_NS);
  header = lm_message_node_add_child(enc, "header", NULL);
  lm_message_node_set_attribute(header, "sid", sid);
  g_free(sid);
  for (i = 0; i < em->keys->len; i++) {
    omemo_key_t *k = g_ptr_array_index(em->keys, i);
    add_b64_child(header, "key", k->data, "rid", k->rid);
    if (k->prekey) {
      // add_b64_child() appends at the end: find the node we just added
      for (n = header->children; n->next; n = n->next)
        ;
      lm_message_node_set_attribute(n, "prekey", "true");
    }
  }
  add_b64_child(header, "iv", em->iv, NULL, 0);
  add_b64_child(enc, "payload", em->payload, NULL, 0);

  n = lm_message_node_add_child(m->node, "store", NULL);
  lm_message_node_set_attribute(n, "xmlns", NS_HINTS);
  n = lm_message_node_add_child(m->node, "encryption", NULL);
  lm_message_node_set_attributes(n, "xmlns", NS_EME, "namespace", OMEMO_NS,
                                 "name", "OMEMO", NULL);
}

static void encrypt_and_send(LmMessage *m, const char *jid, const char *text)
{
  GArray *to = g_array_new(FALSE, FALSE, sizeof(omemo_addr_t));
  GString *skipped = g_string_new(NULL);
  GError *err = NULL;
  omemo_message_t *em;
  GArray *devices = NULL;
  guint n;

  n = collect(jid, to, skipped);
  if (g_strcmp0(jid, store_jid))
    collect(store_jid, to, skipped);

  if (skipped->len)
    buddy_info(jid, "OMEMO: not encrypting for untrusted device(s)%s.  "
               "See /omemo info and /omemo trust.", skipped->str);

  if (!n) {
    if (g_hash_table_contains(failed, jid))
      buddy_error(jid, "OMEMO: cannot fetch the devices of %s; the message "
                  "was not sent.  If you send it again, it will be sent "
                  "unencrypted (unless you use \"/omemo on\").", jid);
    else if (omemo_store_get_devices(store, jid, &devices) && !devices->len)
      buddy_error(jid, "OMEMO: %s does not use OMEMO; the message was not "
                  "sent.  Use \"/omemo off\" to send unencrypted messages.",
                  jid);
    else
      buddy_error(jid, "OMEMO: no trusted device with a session for %s; the "
                  "message was not sent.  See /omemo info.", jid);
    if (devices)
      g_array_free(devices, TRUE);
    goto done;
  }

  em = omemo_encrypt(store, text, (omemo_addr_t *)(void *)to->data, to->len,
                     &err);
  if (!em) {
    buddy_error(jid, "OMEMO: %s; the message was not sent.", err->message);
    g_error_free(err);
    goto done;
  }
  add_encrypted(m, em);
  omemo_message_free(em);
  lm_connection_send(lconnection, m, NULL);

done:
  g_string_free(skipped, TRUE);
  g_array_free(to, TRUE);
}

static void flush_pending(const char *jid)
{
  pending_t *pd = g_hash_table_lookup(pending, jid);
  gboolean awaited;
  GSList *msgs, *l;

  if (!pd || !online)
    return;
  // Our other devices get a copy
  awaited = keys_awaited(jid);
  if (g_strcmp0(jid, store_jid))
    awaited = keys_awaited(store_jid) || awaited;
  if (awaited)
    return;

  msgs = pd->msgs;
  pd->msgs = NULL;
  g_hash_table_remove(pending, jid);
  for (l = msgs; l; l = l->next) {
    queued_t *q = l->data;
    encrypt_and_send(q->m, jid, q->text);
    lm_message_unref(q->m);
    g_free(q->text);
    g_free(q);
  }
  g_slist_free(msgs);
}

static void flush_all(void)
{
  GList *keys = g_hash_table_get_keys(pending), *l;
  for (l = keys; l; l = l->next) {
    gchar *jid = g_strdup(l->data);
    flush_pending(jid);
    g_free(jid);
  }
  g_list_free(keys);
}

void omemo_send(LmMessage *m, const char *bjid, const char *text)
{
  pending_t *pd;
  queued_t *q;

  if (!store || !online) {
    lm_message_unref(m);
    return;
  }
  pd = pending_get(bjid);
  q = g_new0(queued_t, 1);
  q->m = m;
  q->text = g_strdup(text);
  pd->msgs = g_slist_append(pd->msgs, q);
  flush_pending(bjid);
}

static gboolean is_room(const char *bjid)
{
  return roster_find(bjid, jidsearch, ROSTER_TYPE_ROOM) != NULL;
}

omemo_send_mode_t omemo_send_mode(const char *bjid, gchar **reason)
{
  omemo_policy_t policy;
  GArray *devices;
  gboolean known;
  omemo_send_mode_t mode;

  if (reason)
    *reason = NULL;
  if (!store || !bjid || is_room(bjid))
    return OMEMO_SEND_PLAIN;

  policy = omemo_store_get_policy(store, bjid);
  if (policy == OMEMO_POLICY_OFF)
    return OMEMO_SEND_PLAIN;
  if (!online || !omemo_enabled()) {
    if (policy == OMEMO_POLICY_ON) {
      if (reason)
        *reason = g_strdup("OMEMO is not available");
      return OMEMO_SEND_REFUSE;
    }
    return OMEMO_SEND_PLAIN;
  }
  if (policy == OMEMO_POLICY_ON)
    return OMEMO_SEND_ENCRYPTED;

  // Default policy
  if (!g_strcmp0(settings_opt_get("omemo_policy"), "manual"))
    return OMEMO_SEND_PLAIN;
  known = omemo_store_get_devices(store, bjid, &devices);
  // Unknown device list: encrypt, the list is fetched before sending.  If
  // it can't be fetched, the contact is treated as not using OMEMO.
  if (known)
    mode = devices->len ? OMEMO_SEND_ENCRYPTED : OMEMO_SEND_PLAIN;
  else
    mode = g_hash_table_contains(failed, bjid) ? OMEMO_SEND_PLAIN
                                               : OMEMO_SEND_ENCRYPTED;
  if (devices)
    g_array_free(devices, TRUE);
  return mode;
}

const char *omemo_status(const char *bjid)
{
  if (omemo_send_mode(bjid, NULL) == OMEMO_SEND_ENCRYPTED)
    return "omemo";
  return NULL;
}

void omemo_prefetch(const char *bjid)
{
  GArray *devices;
  if (!store || !online || !bjid || is_room(bjid) || !strchr(bjid, '@'))
    return;
  if (omemo_store_get_devices(store, bjid, &devices))
    g_array_free(devices, TRUE);
  else
    fetch_devicelist(bjid);
}

/* -- Receiving ----------------------------------------------------------- */

gchar *omemo_receive(const char *sender, LmMessageNode *node,
                     gboolean *drop, gboolean *ok)
{
  LmMessageNode *enc, *header, *k;
  GBytes *iv, *payload;
  guint32 sid, own;
  omemo_key_t key = { 0 };
  GError *err = NULL;
  gchar *text;

  *drop = FALSE;
  *ok = FALSE;
  enc = lm_message_node_find_xmlns(node, OMEMO_NS);
  if (!enc || g_strcmp0(enc->name, "encrypted"))
    return NULL;
  if (!store)
    return g_strdup("[OMEMO-encrypted message: OMEMO is not enabled]");

  header = lm_message_node_get_child(enc, "header");
  sid = header ? attr_u32(header, "sid") : 0;
  if (!sid)
    return g_strdup("[Malformed OMEMO message]");

  own = omemo_store_device_id(store);
  for (k = header->children; k; k = k->next) {
    const char *pk;
    if (g_strcmp0(k->name, "key") || attr_u32(k, "rid") != own)
      continue;
    key.rid = own;
    pk = lm_message_node_get_attribute(k, "prekey");
    key.prekey = pk && (!strcmp(pk, "true") || !strcmp(pk, "1"));
    key.data = node_bytes(k);
    break;
  }
  payload = node_bytes(lm_message_node_get_child(enc, "payload"));
  if (!key.data) {
    if (payload)
      g_bytes_unref(payload);
    if (sid == own) {   // Our own message, reflected
      *drop = TRUE;
      return g_strdup("");
    }
    return g_strdup("[OMEMO-encrypted message, not encrypted for this "
                    "device]");
  }
  iv = node_bytes(lm_message_node_get_child(header, "iv"));
  if (!iv) {
    g_bytes_unref(key.data);
    if (payload)
      g_bytes_unref(payload);
    return g_strdup("[Malformed OMEMO message]");
  }

  text = omemo_decrypt(store, sender, sid, &key, iv, payload, &err);
  g_bytes_unref(key.data);
  g_bytes_unref(iv);

  if (omemo_store_bundle_changed(store))
    publish_bundle();

  if (!text) {
    gchar *r;
    if (err->code == OMEMO_ERROR_DUPLICATE) {
      *drop = TRUE;
      r = g_strdup("");
    } else {
      r = g_strdup_printf("[Cannot decrypt OMEMO message from device %u: "
                          "%s]", sid, err->message);
    }
    g_error_free(err);
    if (payload)
      g_bytes_unref(payload);
    return r;
  }
  if (!payload) {   // Key transport
    *drop = TRUE;
    return text;
  }
  g_bytes_unref(payload);

  {
    omemo_trust_t t = omemo_store_get_trust(store, sender, sid);
    gchar *name = dev_name(sender, sid);
    if (!omemo_trust_usable(t) && !g_hash_table_contains(warned, name)) {
      g_hash_table_add(warned, name);
      name = NULL;
      scr_LogPrint(LPRINT_LOGNORM, "OMEMO: message from device %u of <%s>, "
                   "which you do not trust.  See /omemo info %s.", sid,
                   sender, sender);
    }
    g_free(name);
  }
  *ok = TRUE;
  return text;
}

/* -- Connection ---------------------------------------------------------- */

static void tables_init(void)
{
  if (pending)
    return;
  pending = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
                                  pending_free);
  requested = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
  failed = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
  warned = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
}

void omemo_connected(void)
{
  gchar *jid;
  GError *err = NULL;

  tables_init();
  g_hash_table_remove_all(pending);
  g_hash_table_remove_all(requested);
  g_hash_table_remove_all(failed);
  online = FALSE;

  if (!omemo_enabled())
    return;

  jid = jidtodisp(lm_connection_get_jid(lconnection));
  if (store && g_strcmp0(jid, store_jid)) {
    omemo_store_close(store);
    store = NULL;
  }
  if (!store) {
    gchar *dir = g_build_filename(settings_get_mcabber_config_dir(), "omemo",
                                  jid, NULL);
    store = omemo_store_open(dir, blind_trust(), &err);
    g_free(dir);
    if (!store) {
      scr_LogPrint(LPRINT_LOGNORM, "OMEMO: %s", err->message);
      g_error_free(err);
      g_free(jid);
      return;
    }
    g_free(store_jid);
    store_jid = g_strdup(jid);
  }
  g_free(jid);
  omemo_store_set_blind_trust(store, blind_trust());
  online = TRUE;

  request_devicelist(store_jid);
  publish_bundle();
  omemo_store_bundle_changed(store);
}

void omemo_disconnected(void)
{
  GList *keys, *l;
  online = FALSE;
  if (!pending)
    return;
  keys = g_hash_table_get_keys(pending);
  for (l = keys; l; l = l->next) {
    pending_t *pd = g_hash_table_lookup(pending, l->data);
    if (pd && pd->msgs)
      buddy_error(l->data, "OMEMO: disconnected; %u message(s) not sent.",
                  g_slist_length(pd->msgs));
  }
  g_list_free(keys);
  g_hash_table_remove_all(pending);
  g_hash_table_remove_all(requested);
}

void omemo_deinit(void)
{
  omemo_disconnected();
  if (pending) {
    g_hash_table_destroy(pending);
    g_hash_table_destroy(requested);
    g_hash_table_destroy(failed);
    g_hash_table_destroy(warned);
    pending = requested = failed = warned = NULL;
  }
  omemo_store_close(store);
  store = NULL;
  g_free(store_jid);
  store_jid = NULL;
}

/* -- /omemo -------------------------------------------------------------- */

static gchar *current_jid(void)
{
  const char *jid;
  if (!current_buddy)
    return NULL;
  if (!(buddy_gettype(BUDDATA(current_buddy)) & ROSTER_TYPE_USER))
    return NULL;
  jid = CURRENT_JID;
  return jid ? g_strdup(jid) : NULL;
}

// A JID argument contains '@' or '.'; a device ID or fingerprint doesn't.
static gboolean looks_like_jid(const char *s)
{
  return s && (strchr(s, '@') || strchr(s, '.'));
}

static void show_info(const char *jid)
{
  GArray *devices = NULL;
  GPtrArray *infos;
  GString *s = g_string_new(NULL);
  gboolean own = !g_strcmp0(jid, store_jid);
  omemo_policy_t p = omemo_store_get_policy(store, jid);
  guint32 own_id = omemo_store_device_id(store);
  guint i;

  if (!omemo_store_get_devices(store, jid, &devices) && online)
    fetch_devicelist(jid);

  if (own) {
    gchar *fpr = omemo_store_own_fingerprint(store);
    g_string_append_printf(s, "OMEMO: this device is %u, fingerprint %s",
                           own_id, fpr);
    g_free(fpr);
  } else {
    g_string_append_printf(s, "OMEMO for %s: %s (policy %s)", jid,
                           omemo_send_mode(jid, NULL) == OMEMO_SEND_ENCRYPTED
                           ? "messages are encrypted" : "not encrypted",
                           p == OMEMO_POLICY_ON ? "on" :
                           p == OMEMO_POLICY_OFF ? "off" : "default");
  }
  infos = omemo_store_devices(store, jid,
                              devices ? (guint32 *)(void *)devices->data
                                      : NULL,
                              devices ? devices->len : 0);
  if (!devices)
    g_string_append(s, "\nDevice list not known yet.");
  else if (!infos->len)
    g_string_append(s, own ? "\nNo other device." : "\nNo OMEMO device.");
  for (i = 0; i < infos->len; i++) {
    omemo_devinfo_t *d = g_ptr_array_index(infos, i);
    gboolean listed = FALSE;
    guint j;
    for (j = 0; devices && j < devices->len; j++)
      if (g_array_index(devices, guint32, j) == d->device)
        listed = TRUE;
    g_string_append_printf(s, "\n  %u  %-10s %s%s%s", d->device,
                           omemo_trust_name(d->trust),
                           d->fingerprint ? d->fingerprint : "(keys unknown)",
                           d->session ? "" : "  no session",
                           listed ? "" : "  inactive");
  }
  g_ptr_array_free(infos, TRUE);
  if (devices)
    g_array_free(devices, TRUE);

  if (own || !scr_buddy_buffer_exists(jid))
    scr_LogPrint(LPRINT_NORMAL, "%s", s->str);
  else
    buddy_info(jid, "%s", s->str);
  g_string_free(s, TRUE);
}

// Find the device designated by spec (device ID or fingerprint)
static gboolean find_device(const char *jid, const char *spec, guint32 *dev)
{
  GArray *devices = NULL;
  GPtrArray *infos;
  gboolean found = FALSE;
  guint i;

  omemo_store_get_devices(store, jid, &devices);
  infos = omemo_store_devices(store, jid,
                              devices ? (guint32 *)(void *)devices->data
                                      : NULL,
                              devices ? devices->len : 0);
  for (i = 0; i < infos->len && !found; i++) {
    omemo_devinfo_t *d = g_ptr_array_index(infos, i);
    gchar *id = g_strdup_printf("%u", d->device);
    if (!strcmp(id, spec) ||
        (d->fingerprint && omemo_fingerprint_match(d->fingerprint, spec))) {
      *dev = d->device;
      found = TRUE;
    }
    g_free(id);
  }
  g_ptr_array_free(infos, TRUE);
  if (devices)
    g_array_free(devices, TRUE);
  return found;
}

void omemo_command(const char *arg)
{
  gchar **args = split_arg(arg, 2, 1);    // subcommand, rest
  const char *sub = args[0] ? args[0] : "info";
  const char *rest = args[1];
  gchar *jid = NULL;
  gchar **rargs = NULL;
  const char *spec = NULL;

  if (!store) {
    scr_LogPrint(LPRINT_NORMAL, omemo_enabled() ?
                 "OMEMO is not ready (are you connected?)." :
                 "OMEMO is disabled (option \"omemo\").");
    goto done;
  }

  // [jid] [device|fingerprint]
  if (rest && *rest) {
    rargs = split_arg(rest, 2, 1);
    if (looks_like_jid(rargs[0])) {
      jid = jidtodisp(rargs[0]);
      spec = rargs[1];
    } else {
      spec = rest;
    }
  }
  if (!jid)
    jid = current_jid();

  if (!g_ascii_strcasecmp(sub, "info") || !g_ascii_strcasecmp(sub, "own")) {
    show_info(!g_ascii_strcasecmp(sub, "own") || !jid ? store_jid : jid);
  } else if (!g_ascii_strcasecmp(sub, "on") ||
             !g_ascii_strcasecmp(sub, "off") ||
             !g_ascii_strcasecmp(sub, "default")) {
    omemo_policy_t p = !g_ascii_strcasecmp(sub, "on") ? OMEMO_POLICY_ON :
                       !g_ascii_strcasecmp(sub, "off") ? OMEMO_POLICY_OFF :
                       OMEMO_POLICY_DEFAULT;
    if (!jid) {
      scr_LogPrint(LPRINT_NORMAL, "Please specify a contact.");
      goto done;
    }
    omemo_store_set_policy(store, jid, p);
    if (p == OMEMO_POLICY_ON)
      omemo_prefetch(jid);
    scr_LogPrint(LPRINT_NORMAL, "OMEMO policy for <%s>: %s.", jid, sub);
    scr_update_chat_status(TRUE);
  } else if (!g_ascii_strcasecmp(sub, "trust") ||
             !g_ascii_strcasecmp(sub, "distrust") ||
             !g_ascii_strcasecmp(sub, "forget")) {
    guint32 dev;
    if (!jid || !spec) {
      scr_LogPrint(LPRINT_NORMAL, "Usage: /omemo %s [jid] "
                   "device|fingerprint", sub);
      goto done;
    }
    if (!find_device(jid, spec, &dev)) {
      scr_LogPrint(LPRINT_NORMAL, "OMEMO: no device \"%s\" for <%s> "
                   "(see /omemo info).", spec, jid);
      goto done;
    }
    if (!g_ascii_strcasecmp(sub, "forget")) {
      omemo_store_forget(store, jid, dev);
      scr_LogPrint(LPRINT_NORMAL, "OMEMO: device %u of <%s> forgotten.",
                   dev, jid);
    } else {
      omemo_trust_t t = !g_ascii_strcasecmp(sub, "trust")
                        ? OMEMO_TRUST_VERIFIED : OMEMO_TRUST_DISTRUSTED;
      if (omemo_store_set_trust(store, jid, dev, t))
        scr_LogPrint(LPRINT_NORMAL, "OMEMO: device %u of <%s> is now %s.",
                     dev, jid, omemo_trust_name(t));
      else
        scr_LogPrint(LPRINT_NORMAL, "OMEMO: the keys of device %u of <%s> "
                     "are not known yet.", dev, jid);
    }
  } else if (!g_ascii_strcasecmp(sub, "fetch")) {
    const char *who = jid ? jid : store_jid;
    GHashTableIter it;
    gpointer key;
    gchar *prefix = g_strdup_printf("%s/", who);
    // Forget previous failures for this contact
    g_hash_table_iter_init(&it, failed);
    while (g_hash_table_iter_next(&it, &key, NULL))
      if (g_str_has_prefix(key, prefix) || !strcmp(key, who))
        g_hash_table_iter_remove(&it);
    g_free(prefix);
    if (online) {
      request_devicelist(who);
      scr_LogPrint(LPRINT_NORMAL, "OMEMO: fetching the devices of <%s>.",
                   who);
    }
  } else if (!g_ascii_strcasecmp(sub, "cleanup")) {
    // Publish a device list with only this device
    GArray *list = g_array_new(FALSE, FALSE, sizeof(guint32));
    guint32 own = omemo_store_device_id(store);
    g_array_append_val(list, own);
    if (online)
      publish_devicelist(list, TRUE);
    g_array_free(list, TRUE);
    scr_LogPrint(LPRINT_NORMAL, "OMEMO: published a device list with only "
                 "this device.");
  } else {
    scr_LogPrint(LPRINT_NORMAL, "Unknown /omemo subcommand \"%s\".", sub);
  }

done:
  g_free(jid);
  if (rargs)
    free_arg_lst(rargs);
  free_arg_lst(args);
}

/* vim: set et cindent cinoptions=>2\:2(0 ts=2 sw=2:  For Vim users... */
