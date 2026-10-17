#include <locale.h>
#include <string.h>
#include <glib.h>

#include "inputlayout.h"

static il_layout_t l;

static void check_rows(const char *buf, gsize cursor, guint width,
                       const char **rows, guint crow, guint ccol)
{
  guint i, n = g_strv_length((gchar **)rows);

  il_layout(buf, cursor, width, &l);
  g_assert_cmpuint(l.rows, ==, n);
  for (i = 0; i < n; i++) {
    gchar *r = g_strndup(buf + il_row_start(&l, i),
                         il_row_end(&l, i) - il_row_start(&l, i));
    g_assert_cmpstr(r, ==, rows[i]);
    g_free(r);
  }
  g_assert_cmpuint(l.cursor_row, ==, crow);
  g_assert_cmpuint(l.cursor_col, ==, ccol);
}

static void test_simple(void)
{
  const char *r1[] = { "", NULL };
  const char *r2[] = { "hello", NULL };
  check_rows("", 0, 10, r1, 0, 0);
  check_rows("hello", 5, 10, r2, 0, 5);
  check_rows("hello", 2, 10, r2, 0, 2);
}

static void test_newlines(void)
{
  const char *r[] = { "ab", "", "cd", NULL };
  const char *r2[] = { "ab", "", NULL };
  check_rows("ab\n\ncd", 6, 10, r, 2, 2);
  check_rows("ab\n\ncd", 3, 10, r, 1, 0);
  check_rows("ab\n\ncd", 2, 10, r, 0, 2);
  check_rows("ab\n", 3, 10, r2, 1, 0);
}

static void test_wrap(void)
{
  const char *r[] = { "abcd", "efgh", "ij", NULL };
  const char *full[] = { "abcd", "", NULL };
  check_rows("abcdefghij", 10, 4, r, 2, 2);
  check_rows("abcdefghij", 4, 4, r, 1, 0);
  check_rows("abcdefghij", 3, 4, r, 0, 3);
  // The cursor after a full row is on the next row
  check_rows("abcd", 4, 4, full, 1, 0);
}

static void test_wide(void)
{
  // Wide characters are not split
  const char *r[] = { "a日", "本語", NULL };
  const char *r3[] = { "a日", "本語", "", NULL };
  const char *buf = "a日本語";
  check_rows(buf, strlen("a日本"), 4, r, 1, 2);
  // A cursor after a full row goes to the next row
  check_rows(buf, strlen(buf), 4, r3, 2, 0);
  // Cyrillic is one cell wide
  {
    const char *r2[] = { "при", "вет", NULL };
    check_rows("привет", strlen("при"), 3, r2, 1, 0);
  }
}

static void test_offset_at(void)
{
  const char *buf = "hello\nab\nworld";
  il_layout(buf, 0, 20, &l);
  g_assert_cmpuint(l.rows, ==, 3);
  g_assert_cmpuint(il_offset_at(buf, &l, 0, 3), ==, 3);
  // Shorter row: end of the row
  g_assert_cmpuint(il_offset_at(buf, &l, 1, 4), ==, 8);
  g_assert_cmpuint(il_offset_at(buf, &l, 2, 1), ==, 10);
  {
    const char *w = "日本";
    il_layout(w, 0, 20, &l);
    // Column 1 is in the middle of the first character
    g_assert_cmpuint(il_offset_at(w, &l, 0, 1), ==, 0);
    g_assert_cmpuint(il_offset_at(w, &l, 0, 2), ==, 3);
  }
}

int main(int argc, char **argv)
{
  int ret;
  setlocale(LC_ALL, "C.UTF-8");
  g_test_init(&argc, &argv, NULL);
  il_layout_init(&l);
  g_test_add_func("/inputlayout/simple", test_simple);
  g_test_add_func("/inputlayout/newlines", test_newlines);
  g_test_add_func("/inputlayout/wrap", test_wrap);
  g_test_add_func("/inputlayout/wide", test_wide);
  g_test_add_func("/inputlayout/offset_at", test_offset_at);
  ret = g_test_run();
  il_layout_free(&l);
  return ret;
}
