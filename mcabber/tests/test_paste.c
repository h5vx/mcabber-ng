#include <locale.h>
#include <glib.h>

#include "paste.h"

static void check(const char *in, const char *expected)
{
  gchar *out = paste_normalize(in);
  g_assert_cmpstr(out, ==, expected);
  g_free(out);
}

static void test_normalize(void)
{
  check("hello", "hello");
  check("a\r\nb\r\nc\r\n", "a\nb\nc");
  check("a\rb", "a\nb");
  check("a\n\nb\n\n\n", "a\n\nb");
  check("\tx", "        x");
  check("ab\tx", "ab      x");
  check("жж\tx", "жж      x");
  check("a\033b\001c", "abc");
  check("\n\n", "");
}

static void test_count(void)
{
  g_assert_cmpuint(paste_count_lines("a"), ==, 1);
  g_assert_cmpuint(paste_count_lines("a\nb\n\nc"), ==, 4);
}

int main(int argc, char **argv)
{
  setlocale(LC_ALL, "C.UTF-8");
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/paste/normalize", test_normalize);
  g_test_add_func("/paste/count", test_count);
  return g_test_run();
}
