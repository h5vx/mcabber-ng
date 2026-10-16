#include <glib.h>

#include "layout.h"

#define ASSERT_RECT(r, Y, X, H, W) do { \
    g_assert_cmpint((r).y, ==, (Y)); g_assert_cmpint((r).x, ==, (X)); \
    g_assert_cmpint((r).h, ==, (H)); g_assert_cmpint((r).w, ==, (W)); \
  } while (0)

static void test_default(void)
{
  layout_opts_t o = { 30, 100, 5, 24, FALSE, FALSE, 1 };
  ui_layout_t l;

  layout_compute(&o, &l);
  // Same geometry as the original mcabber
  ASSERT_RECT(l.roster,      0,  0, 22, 24);
  ASSERT_RECT(l.chat,        0, 24, 22, 76);
  ASSERT_RECT(l.chatstatus, 22,  0,  1, 100);
  ASSERT_RECT(l.log,        23,  0,  5, 100);
  ASSERT_RECT(l.mainstatus, 28,  0,  1, 100);
  ASSERT_RECT(l.input,      29,  0,  1, 100);
}

static void test_options(void)
{
  layout_opts_t o = { 30, 100, 5, 24, TRUE, TRUE, 3 };
  ui_layout_t l;

  layout_compute(&o, &l);
  ASSERT_RECT(l.log,         0,  0,  5, 100);
  ASSERT_RECT(l.chatstatus,  5,  0,  1, 100);
  ASSERT_RECT(l.roster,      6, 76, 20, 24);
  ASSERT_RECT(l.chat,        6,  0, 20, 76);
  ASSERT_RECT(l.mainstatus, 26,  0,  1, 100);
  ASSERT_RECT(l.input,      27,  0,  3, 100);
}

static void test_hidden(void)
{
  layout_opts_t o = { 30, 100, 0, 0, FALSE, FALSE, 1 };
  ui_layout_t l;

  layout_compute(&o, &l);
  ASSERT_RECT(l.chat,        0,  0, 27, 100);
  g_assert_cmpint(l.roster.w, ==, 0);
  g_assert_cmpint(l.log.h, ==, 0);
  ASSERT_RECT(l.chatstatus, 27,  0,  1, 100);
}

static void test_small(void)
{
  layout_opts_t o = { 6, 10, 5, 24, FALSE, FALSE, 4 };
  ui_layout_t l;

  layout_compute(&o, &l);
  // The chat area keeps one line, the input line takes the rest
  g_assert_cmpint(l.chat.h, ==, 1);
  g_assert_cmpint(l.input.h, ==, 3);
  g_assert_cmpint(l.log.h, ==, 0);
  g_assert_cmpint(l.roster.w, ==, 9);
  g_assert_cmpint(l.input.y + l.input.h, ==, 6);

  o.lines = 2;    // Less than the minimum
  layout_compute(&o, &l);
  g_assert_cmpint(l.chat.h, ==, 1);
  g_assert_cmpint(l.input.h, ==, 1);
}

int main(int argc, char **argv)
{
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/layout/default", test_default);
  g_test_add_func("/layout/options", test_options);
  g_test_add_func("/layout/hidden", test_hidden);
  g_test_add_func("/layout/small", test_small);
  return g_test_run();
}
