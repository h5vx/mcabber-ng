#include <locale.h>
#include <glib.h>

#include "utf8.h"

static void test_next_prev(void)
{
  char s[] = "aж€😀b";
  char *p = s;

  p = next_char(p);
  g_assert_cmpuint(get_char(p), ==, 0x436);
  p = next_char(p);
  g_assert_cmpuint(get_char(p), ==, 0x20ac);
  p = next_char(p);
  g_assert_cmpuint(get_char(p), ==, 0x1f600);
  p = next_char(p);
  g_assert_cmpint(*p, ==, 'b');
  p = prev_char(p, s);
  g_assert_cmpuint(get_char(p), ==, 0x1f600);
}

static void test_put_char(void)
{
  char buf[8] = { 0 };
  char *end = put_char(buf, 0x436);
  *end = 0;
  g_assert_cmpstr(buf, ==, "ж");
}

int main(int argc, char **argv)
{
  setlocale(LC_ALL, "C.UTF-8");
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/utf8/next_prev", test_next_prev);
  g_test_add_func("/utf8/put_char", test_put_char);
  return g_test_run();
}
