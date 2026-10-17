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
  g_assert_cmpint(l.roster.w, ==, 8);
  g_assert_cmpint(l.input.y + l.input.h, ==, 6);

  o.lines = 2;    // Less than the minimum
  layout_compute(&o, &l);
  g_assert_cmpint(l.chat.h, ==, 1);
  g_assert_cmpint(l.input.h, ==, 1);
}

static void test_borders(void)
{
  layout_opts_t o = { 30, 100, 5, 24, FALSE, FALSE, 1, TRUE };
  ui_layout_t l;

  layout_compute(&o, &l);
  g_assert_true(l.borders);
  // 30 lines: input (29), main status (28), frame rows 0-27
  g_assert_cmpint(l.frame_top, ==, 0);
  g_assert_cmpint(l.frame_bottom, ==, 27);
  ASSERT_RECT(l.roster,      1,  1, 20, 24);
  g_assert_cmpint(l.frame_vsep, ==, 25);
  ASSERT_RECT(l.chat,        1, 26, 20, 73);
  ASSERT_RECT(l.chatstatus,  0, 27,  1, 71);
  g_assert_cmpint(l.frame_mid, ==, 21);
  ASSERT_RECT(l.log,        22,  1,  5, 98);
  ASSERT_RECT(l.mainstatus, 28,  0,  1, 100);
  ASSERT_RECT(l.input,      29,  0,  1, 100);

  // Log on top, roster on the right
  o.log_on_top = TRUE;
  o.roster_on_right = TRUE;
  layout_compute(&o, &l);
  ASSERT_RECT(l.log,         1,  1,  5, 98);
  g_assert_cmpint(l.frame_mid, ==, 6);
  ASSERT_RECT(l.chat,        7,  1, 20, 73);
  g_assert_cmpint(l.frame_vsep, ==, 74);
  ASSERT_RECT(l.roster,      7, 75, 20, 24);
  ASSERT_RECT(l.chatstatus,  6,  2,  1, 71);

  // No log, no roster
  o.log_height = 0;
  o.roster_width = 0;
  layout_compute(&o, &l);
  g_assert_cmpint(l.frame_mid, ==, -1);
  g_assert_cmpint(l.frame_vsep, ==, -1);
  ASSERT_RECT(l.chat,        1,  1, 26, 98);

  // Too small: no frames
  o.lines = 4;
  layout_compute(&o, &l);
  g_assert_false(l.borders);
}

static void test_input_in_chat(void)
{
  layout_opts_t o = { 30, 100, 5, 24, FALSE, FALSE, 2, FALSE, TRUE, 0, FALSE };
  ui_layout_t l;

  layout_compute(&o, &l);
  // chat, chat status, input in the chat column; then log, main status
  ASSERT_RECT(l.chat,        0, 24, 21, 76);
  ASSERT_RECT(l.chatstatus, 21, 24,  1, 76);
  ASSERT_RECT(l.input,      22, 24,  2, 76);
  ASSERT_RECT(l.roster,      0,  0, 24, 24);
  ASSERT_RECT(l.log,        24,  0,  5, 100);
  ASSERT_RECT(l.mainstatus, 29,  0,  1, 100);

  // With frames and a members column
  o.borders = TRUE;
  o.members_width = 20;
  layout_compute(&o, &l);
  g_assert_true(l.borders);
  ASSERT_RECT(l.roster,      1,  1, 21, 24);
  g_assert_cmpint(l.frame_vsep, ==, 25);
  ASSERT_RECT(l.chat,        1, 26, 18, 52);
  g_assert_cmpint(l.frame_msep, ==, 78);
  ASSERT_RECT(l.members,     1, 79, 18, 20);
  ASSERT_RECT(l.chatstatus,  0, 27,  1, 50);
  g_assert_cmpint(l.frame_isep, ==, 19);
  ASSERT_RECT(l.input,      20, 26,  2, 73);
  g_assert_cmpint(l.frame_mid, ==, 22);
  ASSERT_RECT(l.log,        23,  1,  5, 98);
  g_assert_cmpint(l.frame_bottom, ==, 28);
  ASSERT_RECT(l.mainstatus, 29,  0,  1, 100);

  // Members on the left, without frames
  o.borders = FALSE;
  o.members_on_left = TRUE;
  layout_compute(&o, &l);
  ASSERT_RECT(l.members,     0, 24, 21, 20);
  ASSERT_RECT(l.chat,        0, 44, 21, 56);
}

int main(int argc, char **argv)
{
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/layout/default", test_default);
  g_test_add_func("/layout/options", test_options);
  g_test_add_func("/layout/hidden", test_hidden);
  g_test_add_func("/layout/small", test_small);
  g_test_add_func("/layout/borders", test_borders);
  g_test_add_func("/layout/input_in_chat", test_input_in_chat);
  return g_test_run();
}
