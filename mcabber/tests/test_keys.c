#include <glib.h>

#include "keys.h"

static void check_canon(const char *in, const char *expected)
{
  GError *err = NULL;
  gchar *out = key_canonicalize(in, &err);
  if (!out)
    g_error("cannot parse \"%s\": %s", in, err->message);
  g_assert_cmpstr(out, ==, expected);
  g_free(out);
}

static void check_invalid(const char *in)
{
  GError *err = NULL;
  gchar *out = key_canonicalize(in, &err);
  if (out)
    g_error("\"%s\" should be invalid, got \"%s\"", in, out);
  g_assert_nonnull(err);
  g_error_free(err);
}

static void test_parse(void)
{
  check_canon("Alt+j", "Alt+j");
  check_canon("alt-J", "Alt+J");
  check_canon("M-j", "Alt+j");
  check_canon("meta+shift+j", "Alt+J");
  check_canon("Shift+Alt+j", "Alt+J");
  check_canon("C-x", "Ctrl+x");
  check_canon("Ctrl+X", "Ctrl+x");
  check_canon("ctrl+shift+x", "Ctrl+Shift+x");
  check_canon("Ctrl+i", "Tab");
  check_canon("Ctrl+m", "Enter");
  check_canon("Ctrl+[", "Esc");
  check_canon("Alt+Ctrl+m", "Alt+Enter");
  check_canon("Ctrl+Alt+Shift+Left", "Ctrl+Alt+Shift+Left");
  check_canon("alt+return", "Alt+Enter");
  check_canon("PageUp", "PgUp");
  check_canon("ctrl+pagedown", "Ctrl+PgDn");
  check_canon("f5", "F5");
  check_canon("Shift+F12", "Shift+F12");
  check_canon("F", "F");
  check_canon("Alt+ж", "Alt+ж");
  check_canon("Alt+Ж", "Alt+Ж");
  check_canon("Shift+ж", "Ж");
  check_canon("Alt++", "Alt+Plus");
  check_canon("Alt+plus", "Alt+Plus");
  check_canon("Alt+-", "Alt+-");
  check_canon("Ctrl+=", "Ctrl+=");
  check_canon("-", "-");
  check_canon("Ctrl+Space", "Ctrl+Space");
  check_canon("Key(600)", "Key(600)");
  check_canon("Mouse(0x10)", "Mouse(0x10)");
  check_canon("  Esc  ", "Esc");

  check_invalid("");
  check_invalid("Ctrl+");
  check_invalid("Hyper+x");
  check_invalid("F0");
  check_invalid("F64");
  check_invalid("Key(x)");
  check_invalid("jk");
}

static void check_byte(int c, const char *expected)
{
  keyspec_t k;
  gchar *s;
  g_assert_true(key_from_byte(c, &k));
  s = key_format(&k);
  g_assert_cmpstr(s, ==, expected);
  g_free(s);
}

static void test_bytes(void)
{
  check_byte(0, "Ctrl+Space");
  check_byte(1, "Ctrl+a");
  check_byte(8, "Ctrl+h");
  check_byte(9, "Tab");
  check_byte(10, "Ctrl+j");
  check_byte(13, "Enter");
  check_byte(26, "Ctrl+z");
  check_byte(27, "Esc");
  check_byte(28, "Ctrl+\\");
  check_byte(31, "Ctrl+_");
  check_byte(' ', "Space");
  check_byte('a', "a");
  check_byte(127, "Backspace");
  check_byte(0x436, "ж");
}

static void check_csi(const char *params, char final, const char *expected)
{
  keyspec_t k;
  gchar *s;
  if (!key_from_csi(params, final, &k))
    g_error("CSI %s%c not decoded", params, final);
  s = key_format(&k);
  g_assert_cmpstr(s, ==, expected);
  g_free(s);
}

static void test_csi(void)
{
  keyspec_t k;

  check_csi("1;5", 'A', "Ctrl+Up");
  check_csi("1;2", 'B', "Shift+Down");
  check_csi("1;3", 'C', "Alt+Right");
  check_csi("1;6", 'D', "Ctrl+Shift+Left");
  check_csi("1;8", 'D', "Ctrl+Alt+Shift+Left");
  check_csi("", 'H', "Home");
  check_csi("1;5", 'F', "Ctrl+End");
  check_csi("5;5", '~', "Ctrl+PgUp");
  check_csi("6;2", '~', "Shift+PgDn");
  check_csi("3", '~', "Del");
  check_csi("15;2", '~', "Shift+F5");
  check_csi("24", '~', "F12");
  check_csi("1;2", 'P', "Shift+F1");
  check_csi("13;2", 'u', "Shift+Enter");
  check_csi("13;5", 'u', "Ctrl+Enter");
  check_csi("97;5", 'u', "Ctrl+a");
  check_csi("97;2", 'u', "A");
  check_csi("106;3", 'u', "Alt+j");
  check_csi("27;5;13", '~', "Ctrl+Enter");
  check_csi("", 'Z', "Shift+Tab");
  check_csi("200", '~', "PasteStart");
  check_csi("201", '~', "PasteEnd");

  g_assert_false(key_from_csi("99", '~', &k));
  g_assert_false(key_from_csi("1;5", 'x', &k));
  g_assert_false(key_from_csi("a", 'A', &k));
}

static void check_ti(const char *name, const char *expected)
{
  keyspec_t k;
  gchar *s;
  if (!key_from_terminfo_name(name, &k))
    g_error("terminfo name %s not decoded", name);
  s = key_format(&k);
  g_assert_cmpstr(s, ==, expected);
  g_free(s);
}

static void test_terminfo(void)
{
  keyspec_t k;

  check_ti("kUP5", "Ctrl+Up");
  check_ti("kDN2", "Shift+Down");
  check_ti("kLFT3", "Alt+Left");
  check_ti("kRIT6", "Ctrl+Shift+Right");
  check_ti("kHOM5", "Ctrl+Home");
  check_ti("kEND7", "Ctrl+Alt+End");
  check_ti("kNXT5", "Ctrl+PgDn");
  check_ti("kPRV4", "Alt+Shift+PgUp");
  check_ti("kDC5", "Ctrl+Del");
  check_ti("kIC3", "Alt+Ins");
  g_assert_false(key_from_terminfo_name("kfoo5", &k));
  g_assert_false(key_from_terminfo_name("kUPx", &k));
}

static void check_legacy(const char *code, const char *expected)
{
  keyspec_t k;
  gchar *s;
  if (!key_from_legacy(code, &k))
    g_error("legacy code %s not decoded", code);
  s = key_format(&k);
  g_assert_cmpstr(s, ==, expected);
  g_free(s);
}

static void test_legacy(void)
{
  keyspec_t k;

  check_legacy("M106", "Alt+j");
  check_legacy("M13", "Alt+Enter");
  check_legacy("M27", "Alt+Esc");
  check_legacy("MU1078", "Alt+ж");
  check_legacy("U1078", "ж");
  check_legacy("265", "F1");
  check_legacy("269", "F5");
  check_legacy("277", "Shift+F1");
  check_legacy("MK3", "Ctrl+PgUp");
  check_legacy("MK14", "Ctrl+Shift+End");
  check_legacy("8", "Ctrl+h");
  check_legacy("127", "Backspace");
  check_legacy("353", "Shift+Tab");
  check_legacy("521", "Ctrl+Up");
  check_legacy("516", "Ctrl+Left");
  g_assert_false(key_from_legacy("Alt+j", &k));
  g_assert_false(key_from_legacy("MK99", &k));
  g_assert_false(key_from_legacy("", &k));
}

int main(int argc, char **argv)
{
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/keys/parse", test_parse);
  g_test_add_func("/keys/bytes", test_bytes);
  g_test_add_func("/keys/csi", test_csi);
  g_test_add_func("/keys/terminfo", test_terminfo);
  g_test_add_func("/keys/legacy", test_legacy);
  return g_test_run();
}
