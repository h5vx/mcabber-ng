/*
 * keys.c       -- Human-readable key names
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

#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "keys.h"

// Only the KEY_* constants are used here, we don't call ncurses.
#if HAVE_NCURSESW_NCURSES_H
# include <ncursesw/ncurses.h>
#elif HAVE_NCURSES_NCURSES_H
# include <ncurses/ncurses.h>
#else
# include <ncurses.h>
#endif

GQuark keys_error_quark(void)
{
  return g_quark_from_static_string("mcabber-keys");
}

typedef struct {
  const char *name;
  gint key;
} keyname_t;

// The first name of each key is the canonical one
static const keyname_t named_keys[] = {
  { "Enter",     KN_ENTER },     { "Return",   KN_ENTER },
  { "Ret",       KN_ENTER },     { "CR",       KN_ENTER },
  { "Tab",       KN_TAB },
  { "Backspace", KN_BACKSPACE }, { "BS",       KN_BACKSPACE },
  { "BkSp",      KN_BACKSPACE },
  { "Esc",       KN_ESC },       { "Escape",   KN_ESC },
  { "Up",        KN_UP },
  { "Down",      KN_DOWN },
  { "Left",      KN_LEFT },
  { "Right",     KN_RIGHT },
  { "Home",      KN_HOME },
  { "End",       KN_END },
  { "PgUp",      KN_PGUP },      { "PageUp",   KN_PGUP },
  { "Prior",     KN_PGUP },
  { "PgDn",      KN_PGDN },      { "PageDown", KN_PGDN },
  { "PgDown",    KN_PGDN },      { "Next",     KN_PGDN },
  { "Ins",       KN_INS },       { "Insert",   KN_INS },
  { "Del",       KN_DEL },       { "Delete",   KN_DEL },
  { "Begin",     KN_BEGIN },
  { "WheelUp",   KN_WHEELUP },
  { "WheelDown", KN_WHEELDOWN },
  { "PasteStart", KN_PASTE_START },
  { "PasteEnd",  KN_PASTE_END },
  // Characters with a name
  { "Space",     ' ' },          { "Spc",      ' ' },
  { "Plus",      '+' },
  { NULL, 0 }
};

static const struct {
  const char *name;
  guint mod;
} modifiers[] = {
  { "ctrl", KMOD_CTRL }, { "control", KMOD_CTRL }, { "c", KMOD_CTRL },
  { "alt", KMOD_ALT },   { "meta", KMOD_ALT },     { "m", KMOD_ALT },
  { "shift", KMOD_SHIFT }, { "s", KMOD_SHIFT },
  { NULL, 0 }
};

//  key_normalize(k)
// Give equivalent key combinations the same representation.
void key_normalize(keyspec_t *k)
{
  if (k->key > 0 && k->key < 0x110000) {
    gunichar c = k->key;
    if (k->mods & KMOD_CTRL) {
      // Ctrl+i is Tab, etc.: the terminal can't tell them apart
      guint other = k->mods & ~(KMOD_CTRL|KMOD_ALT);
      if (!other) {
        gint named = KN_NONE;
        if (c == 'i' || c == 'I')      named = KN_TAB;
        else if (c == 'm' || c == 'M') named = KN_ENTER;
        else if (c == '[')             named = KN_ESC;
        if (named != KN_NONE) {
          k->key = named;
          k->mods &= ~KMOD_CTRL;
          return;
        }
      }
      // Ctrl+A is Ctrl+a (use Ctrl+Shift+a to be explicit)
      if (g_unichar_isupper(c))
        k->key = g_unichar_tolower(c);
    } else if ((k->mods & KMOD_SHIFT) && g_unichar_isalpha(c)) {
      // Shift+j is J
      k->key = g_unichar_toupper(c);
      k->mods &= ~KMOD_SHIFT;
    }
  }
}

gboolean key_equal(const keyspec_t *a, const keyspec_t *b)
{
  return a->mods == b->mods && a->key == b->key &&
         ((a->key != KN_RAW && a->key != KN_MOUSE) || a->raw == b->raw);
}

//  parse_base(name, out)
// Parse the key name, without modifiers.
static gboolean parse_base(const char *name, keyspec_t *out)
{
  const keyname_t *kn;
  const char *end;

  if (!*name)
    return FALSE;

  // A single UTF-8 character
  if (g_utf8_validate(name, -1, &end) &&
      *g_utf8_next_char(name) == '\0') {
    out->key = g_utf8_get_char(name);
    return out->key > 0;
  }

  for (kn = named_keys; kn->name; kn++) {
    if (!g_ascii_strcasecmp(name, kn->name)) {
      out->key = kn->key;
      return TRUE;
    }
  }

  // F1..F63
  if ((name[0] == 'F' || name[0] == 'f') && g_ascii_isdigit(name[1])) {
    char *e;
    long n = strtol(name+1, &e, 10);
    if (!*e && n >= 1 && n <= KN_FMAX) {
      out->key = KN_F(n);
      return TRUE;
    }
    return FALSE;
  }

  // Key(n): raw ncurses key code;  Mouse(n): raw mouse event
  if (!g_ascii_strncasecmp(name, "Key(", 4) ||
      !g_ascii_strncasecmp(name, "Mouse(", 6)) {
    gboolean mouse = (name[0] == 'M' || name[0] == 'm');
    const char *p = strchr(name, '(') + 1;
    char *e;
    long n = strtol(p, &e, 0);
    if (e == p || strcmp(e, ")") || n < 0)
      return FALSE;
    out->key = mouse ? KN_MOUSE : KN_RAW;
    out->raw = n;
    return TRUE;
  }
  return FALSE;
}

gboolean key_parse(const char *name, keyspec_t *out, GError **err)
{
  const char *p;
  keyspec_t k = { 0, KN_NONE, 0 };

  if (!name) {
    g_set_error(err, KEYS_ERROR, 0, "empty key name");
    return FALSE;
  }
  while (*name == ' ')
    name++;
  p = name;

  // Modifiers: "<mod>+" or "<mod>-", followed by something
  while (*p) {
    const char *sep = p;
    int i, found = -1;

    while (g_ascii_isalpha(*sep))
      sep++;
    if (sep == p || (*sep != '+' && *sep != '-') || !sep[1])
      break;
    for (i = 0; modifiers[i].name; i++) {
      if (strlen(modifiers[i].name) == (size_t)(sep - p) &&
          !g_ascii_strncasecmp(p, modifiers[i].name, sep - p)) {
        found = i;
        break;
      }
    }
    if (found < 0)
      break;
    k.mods |= modifiers[found].mod;
    p = sep + 1;
  }

  {
    gchar *base = g_strchomp(g_strdup(p));
    gboolean ok = parse_base(base, &k);
    g_free(base);
    if (!ok) {
      g_set_error(err, KEYS_ERROR, 0, "unknown key name \"%s\"", name);
      return FALSE;
    }
  }

  key_normalize(&k);
  *out = k;
  return TRUE;
}

gchar *key_format(const keyspec_t *k)
{
  GString *s = g_string_new(NULL);
  const keyname_t *kn;

  if (k->mods & KMOD_CTRL)  g_string_append(s, "Ctrl+");
  if (k->mods & KMOD_ALT)   g_string_append(s, "Alt+");
  if (k->mods & KMOD_SHIFT) g_string_append(s, "Shift+");

  if (k->key == KN_RAW) {
    g_string_append_printf(s, "Key(%d)", k->raw);
    return g_string_free(s, FALSE);
  }
  if (k->key == KN_MOUSE) {
    g_string_append_printf(s, "Mouse(0x%x)", k->raw);
    return g_string_free(s, FALSE);
  }
  if (KN_IS_F(k->key)) {
    g_string_append_printf(s, "F%d", KN_F0 - k->key);
    return g_string_free(s, FALSE);
  }
  for (kn = named_keys; kn->name; kn++) {
    if (kn->key == k->key) {
      g_string_append(s, kn->name);
      return g_string_free(s, FALSE);
    }
  }
  if (k->key > 0)
    g_string_append_unichar(s, k->key);
  else
    g_string_append_printf(s, "Key(%d)", k->key);
  return g_string_free(s, FALSE);
}

gchar *key_canonicalize(const char *name, GError **err)
{
  keyspec_t k;
  if (!key_parse(name, &k, err))
    return NULL;
  return key_format(&k);
}

//  key_from_byte(c, out)
// Decode a single byte (or code point) read from the terminal.
gboolean key_from_byte(int c, keyspec_t *out)
{
  keyspec_t k = { 0, KN_NONE, 0 };

  if (c < 0)
    return FALSE;
  switch (c) {
    case 0:   k.mods = KMOD_CTRL; k.key = ' '; break;
    case 9:   k.key = KN_TAB; break;
    case 13:  k.key = KN_ENTER; break;
    case 27:  k.key = KN_ESC; break;
    case 127: k.key = KN_BACKSPACE; break;
    case 28:  k.mods = KMOD_CTRL; k.key = '\\'; break;
    case 29:  k.mods = KMOD_CTRL; k.key = ']'; break;
    case 30:  k.mods = KMOD_CTRL; k.key = '^'; break;
    case 31:  k.mods = KMOD_CTRL; k.key = '_'; break;
    default:
      if (c < 27) {
        k.mods = KMOD_CTRL;
        k.key = 'a' + c - 1;
      } else {
        k.key = c;
      }
  }
  *out = k;
  return TRUE;
}

//  key_from_ncurses(code, out)
// Decode an ncurses KEY_* code.  Codes above KEY_MAX are defined at run
// time and must be decoded with key_from_terminfo_name(keyname(code)).
gboolean key_from_ncurses(int code, keyspec_t *out)
{
  keyspec_t k = { 0, KN_NONE, 0 };

  if (code < KEY_MIN)
    return key_from_byte(code, out);

  if (code >= KEY_F(1) && code <= KEY_F(KN_FMAX)) {
    // xterm convention: F13-F24 are Shift+F1-F12, F25-F36 Ctrl+F1-F12...
    static const guint fmods[] = { 0, KMOD_SHIFT, KMOD_CTRL,
                                   KMOD_CTRL|KMOD_SHIFT, KMOD_ALT,
                                   KMOD_ALT|KMOD_SHIFT };
    int n = code - KEY_F(0) - 1;
    k.mods = fmods[n / 12];
    k.key = KN_F(n % 12 + 1);
    *out = k;
    return TRUE;
  }

  switch (code) {
    case KEY_DOWN:      k.key = KN_DOWN; break;
    case KEY_UP:        k.key = KN_UP; break;
    case KEY_LEFT:      k.key = KN_LEFT; break;
    case KEY_RIGHT:     k.key = KN_RIGHT; break;
    case KEY_HOME:      k.key = KN_HOME; break;
    case KEY_END:       k.key = KN_END; break;
    case KEY_BACKSPACE: k.key = KN_BACKSPACE; break;
    case KEY_DC:        k.key = KN_DEL; break;
    case KEY_IC:        k.key = KN_INS; break;
    case KEY_NPAGE:     k.key = KN_PGDN; break;
    case KEY_PPAGE:     k.key = KN_PGUP; break;
    case KEY_ENTER:     k.key = KN_ENTER; break;
    case KEY_B2:        k.key = KN_BEGIN; break;
    case KEY_BEG:       k.key = KN_BEGIN; break;
    case KEY_BTAB:      k.mods = KMOD_SHIFT; k.key = KN_TAB; break;
    case KEY_SF:        k.mods = KMOD_SHIFT; k.key = KN_DOWN; break;
    case KEY_SR:        k.mods = KMOD_SHIFT; k.key = KN_UP; break;
    case KEY_SLEFT:     k.mods = KMOD_SHIFT; k.key = KN_LEFT; break;
    case KEY_SRIGHT:    k.mods = KMOD_SHIFT; k.key = KN_RIGHT; break;
    case KEY_SHOME:     k.mods = KMOD_SHIFT; k.key = KN_HOME; break;
    case KEY_SEND:      k.mods = KMOD_SHIFT; k.key = KN_END; break;
    case KEY_SPREVIOUS: k.mods = KMOD_SHIFT; k.key = KN_PGUP; break;
    case KEY_SNEXT:     k.mods = KMOD_SHIFT; k.key = KN_PGDN; break;
    case KEY_SDC:       k.mods = KMOD_SHIFT; k.key = KN_DEL; break;
    case KEY_SIC:       k.mods = KMOD_SHIFT; k.key = KN_INS; break;
    default:
      k.key = KN_RAW;
      k.raw = code;
      *out = k;
      return FALSE;
  }
  *out = k;
  return TRUE;
}

// xterm modifier parameter: 1 + (Shift=1 | Alt=2 | Ctrl=4 | Meta=8)
static guint csi_mods(long m)
{
  guint mods = 0;
  if (m < 2)
    return 0;
  m--;
  if (m & 1) mods |= KMOD_SHIFT;
  if (m & 2) mods |= KMOD_ALT;
  if (m & 4) mods |= KMOD_CTRL;
  if (m & 8) mods |= KMOD_ALT;
  return mods;
}

//  key_from_terminfo_name(name, out)
// Decode ncurses extended key names, like "kUP5" (Ctrl+Up).
gboolean key_from_terminfo_name(const char *name, keyspec_t *out)
{
  static const keyname_t tinames[] = {
    { "kUP", KN_UP }, { "kDN", KN_DOWN }, { "kLFT", KN_LEFT },
    { "kRIT", KN_RIGHT }, { "kHOM", KN_HOME }, { "kEND", KN_END },
    { "kNXT", KN_PGDN }, { "kPRV", KN_PGUP }, { "kDC", KN_DEL },
    { "kIC", KN_INS }, { "kBEG", KN_BEGIN },
    { NULL, 0 }
  };
  const keyname_t *kn;

  if (!name)
    return FALSE;
  for (kn = tinames; kn->name; kn++) {
    size_t len = strlen(kn->name);
    if (!strncmp(name, kn->name, len)) {
      const char *p = name + len;
      long m = 1;
      if (*p) {
        char *e;
        m = strtol(p, &e, 10);
        if (*e || m < 2 || m > 16)
          return FALSE;
      }
      out->mods = csi_mods(m);
      out->key = kn->key;
      out->raw = 0;
      return TRUE;
    }
  }
  return FALSE;
}

//  key_from_csi(params, final, out)
// Decode a CSI sequence: ESC '[' params final.
// Handles the xterm "1;mX" and "n;m~" forms, "27;m;c~" (modifyOtherKeys)
// and "c;mu" (CSI u / fixterms / kitty).
gboolean key_from_csi(const char *params, char final, keyspec_t *out)
{
  long p[4] = { 0, 0, 0, 0 };
  int np = 0;
  const char *s = params;
  keyspec_t k = { 0, KN_NONE, 0 };

  // Parse up to 4 numeric parameters (kitty sub-parameters are ignored)
  while (*s && np < 4) {
    char *e;
    p[np] = strtol(s, &e, 10);
    if (e == s && *s != ';')
      return FALSE;
    np++;
    s = e;
    while (*s == ':' || g_ascii_isdigit(*s))   // ":alternate:keys"
      s++;
    if (*s == ';')
      s++;
    else if (*s)
      return FALSE;
  }

  switch (final) {
    case 'A': k.key = KN_UP; break;
    case 'B': k.key = KN_DOWN; break;
    case 'C': k.key = KN_RIGHT; break;
    case 'D': k.key = KN_LEFT; break;
    case 'H': k.key = KN_HOME; break;
    case 'F': k.key = KN_END; break;
    case 'E': k.key = KN_BEGIN; break;
    case 'P': k.key = KN_F(1); break;
    case 'Q': k.key = KN_F(2); break;
    case 'R': k.key = KN_F(3); break;
    case 'S': k.key = KN_F(4); break;
    case 'Z': k.mods = KMOD_SHIFT; k.key = KN_TAB; break;
    case 'u':
      if (np < 1 || p[0] <= 0)
        return FALSE;
      if (!key_from_byte(p[0], &k))
        return FALSE;
      if (k.mods)     // Control characters are not expected here
        return FALSE;
      k.mods = csi_mods(p[1]);
      break;
    case '~':
      if (np < 1)
        return FALSE;
      if (p[0] == 27 && np >= 3) {   // modifyOtherKeys
        if (!key_from_byte(p[2], &k) || k.mods)
          return FALSE;
        k.mods = csi_mods(p[1]);
        break;
      }
      switch (p[0]) {
        case 1: case 7: k.key = KN_HOME; break;
        case 2:  k.key = KN_INS; break;
        case 3:  k.key = KN_DEL; break;
        case 4: case 8: k.key = KN_END; break;
        case 5:  k.key = KN_PGUP; break;
        case 6:  k.key = KN_PGDN; break;
        case 11: case 12: case 13: case 14: case 15:
          k.key = KN_F(p[0] - 10); break;
        case 17: case 18: case 19: case 20: case 21:
          k.key = KN_F(p[0] - 11); break;
        case 23: case 24:
          k.key = KN_F(p[0] - 12); break;
        case 200: k.key = KN_PASTE_START; break;
        case 201: k.key = KN_PASTE_END; break;
        default:
          return FALSE;
      }
      if (np >= 2)
        k.mods = csi_mods(p[1]);
      *out = k;
      return TRUE;
    default:
      return FALSE;
  }

  if (final != 'u' && final != '~' && final != 'Z' && np >= 2)
    k.mods |= csi_mods(p[1]);
  key_normalize(&k);
  *out = k;
  return TRUE;
}

//  key_from_legacy(code, out)
// Decode the key codes used by the original mcabber "bind" command:
// "265", "M106" (Meta-j), "MU1078" (Meta-ж), "U1078", "MK3" (Ctrl-PgUp).
gboolean key_from_legacy(const char *code, keyspec_t *out)
{
  static const keyspec_t mkeys[] = {
    { 0, KN_NONE, 0 }, { 0, KN_NONE, 0 }, { 0, KN_NONE, 0 },
    { KMOD_CTRL, KN_PGUP, 0 },             // MKEY_CTRL_PGUP = 3
    { KMOD_CTRL, KN_PGDN, 0 },
    { KMOD_SHIFT, KN_PGUP, 0 },
    { KMOD_SHIFT, KN_PGDN, 0 },
    { KMOD_CTRL|KMOD_SHIFT, KN_PGUP, 0 },
    { KMOD_CTRL|KMOD_SHIFT, KN_PGDN, 0 },
    { KMOD_CTRL, KN_HOME, 0 },
    { KMOD_CTRL, KN_END, 0 },
    { KMOD_CTRL, KN_INS, 0 },
    { KMOD_CTRL, KN_DEL, 0 },
    { KMOD_CTRL|KMOD_SHIFT, KN_HOME, 0 },
    { KMOD_CTRL|KMOD_SHIFT, KN_END, 0 },   // MKEY_CTRL_SHIFT_END = 14
  };
  gboolean meta = FALSE, unicode = FALSE;
  const char *p = code;
  char *e;
  long n;
  keyspec_t k;

  if (!p || !*p)
    return FALSE;
  if (p[0] == 'M' && p[1] == 'K') {
    n = strtol(p+2, &e, 10);
    if (*e || e == p+2 || n < 3 || n > 14)
      return FALSE;
    *out = mkeys[n];
    return TRUE;
  }
  if (*p == 'M') {
    meta = TRUE;
    p++;
  }
  if (*p == 'U') {
    unicode = TRUE;
    p++;
  }
  if (!g_ascii_isdigit(*p))
    return FALSE;
  n = strtol(p, &e, 10);
  if (*e)
    return FALSE;

  if (unicode) {
    if (n <= 0 || n >= 0x110000)
      return FALSE;
    k.mods = 0;
    k.key = n;
    k.raw = 0;
  } else if (n >= 512) {
    // Codes defined at run time by the original key sequence table
    switch (n) {
      case 521: k.mods = KMOD_CTRL; k.key = KN_UP; break;
      case 514: k.mods = KMOD_CTRL; k.key = KN_DOWN; break;
      case 515: case 516: k.mods = KMOD_CTRL; k.key = KN_LEFT; break;
      case 517: case 518: k.mods = KMOD_CTRL; k.key = KN_RIGHT; break;
      case 520: k.mods = KMOD_SHIFT; k.key = KN_UP; break;
      case 513: k.mods = KMOD_SHIFT; k.key = KN_DOWN; break;
      default:
        return FALSE;
    }
    k.raw = 0;
  } else {
    key_from_ncurses(n, &k);
  }
  if (meta)
    k.mods |= KMOD_ALT;
  key_normalize(&k);
  *out = k;
  return TRUE;
}

/* vim: set et cindent cinoptions=>2\:2(0 ts=2 sw=2:  For Vim users... */
