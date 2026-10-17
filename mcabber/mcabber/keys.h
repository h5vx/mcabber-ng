#ifndef __MCABBER_KEYS_H__
#define __MCABBER_KEYS_H__ 1

/* Human-readable key names: "Alt+j", "Ctrl+Left", "F5", "Alt+Enter"...
 *
 * Grammar:   key  := { modifier ("+" | "-") } base
 *            modifier := Ctrl | Control | C | Alt | Meta | M | A | Shift | S
 *            base := a named key (Enter, Tab, Up, PgUp, F1...) or a single
 *                    UTF-8 character ("j", "ж", "=", "-")
 * Names are case-insensitive, except single characters.
 * The canonical form is "Ctrl+Alt+Shift+Base".
 */

#include <glib.h>

#define KMOD_CTRL   1U
#define KMOD_ALT    2U
#define KMOD_SHIFT  4U

// Named keys.  Positive key values are Unicode code points.
enum {
  KN_NONE       = 0,
  KN_ENTER      = -1,
  KN_TAB        = -2,
  KN_BACKSPACE  = -3,
  KN_ESC        = -4,
  KN_UP         = -10,
  KN_DOWN       = -11,
  KN_LEFT       = -12,
  KN_RIGHT      = -13,
  KN_HOME       = -14,
  KN_END        = -15,
  KN_PGUP       = -16,
  KN_PGDN       = -17,
  KN_INS        = -18,
  KN_DEL        = -19,
  KN_BEGIN      = -20,
  KN_F0         = -100,   // Fn is KN_F0 - n, 1 <= n <= KN_FMAX
  KN_WHEELUP    = -200,
  KN_WHEELDOWN  = -201,
  KN_MOUSE      = -202,   // raw holds the mouse event mask
  KN_PASTE_START = -210,
  KN_PASTE_END  = -211,
  KN_RAW        = -1000,  // Unknown key, raw holds the (ncurses) code
};

#define KN_FMAX     63
#define KN_F(n)     (KN_F0 - (n))
#define KN_IS_F(k)  ((k) < KN_F0 && (k) >= KN_F0 - KN_FMAX)

typedef struct {
  guint mods;
  gint  key;
  gint  raw;
} keyspec_t;

#define KEYS_ERROR  (keys_error_quark())
GQuark   keys_error_quark(void);

void     key_normalize(keyspec_t *k);
gboolean key_parse(const char *name, keyspec_t *out, GError **err);
gchar   *key_format(const keyspec_t *k);
gchar   *key_canonicalize(const char *name, GError **err);
gboolean key_equal(const keyspec_t *a, const keyspec_t *b);

// Decoding of terminal input
gboolean key_from_byte(int c, keyspec_t *out);
gboolean key_from_ncurses(int code, keyspec_t *out);
gboolean key_from_terminfo_name(const char *name, keyspec_t *out);
gboolean key_from_csi(const char *params, char final, keyspec_t *out);
gboolean key_from_legacy(const char *code, keyspec_t *out);

#endif /* __MCABBER_KEYS_H__ */

/* vim: set et cindent cinoptions=>2\:2(0 ts=2 sw=2:  For Vim users... */
