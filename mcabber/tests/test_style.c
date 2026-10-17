#include <glib.h>

#include "style.h"

static void test_colors(void)
{
  style_color_t c;
  g_assert_true(style_parse_color("default", &c, NULL));
  g_assert_cmpint(c.kind, ==, STYLE_COLOR_DEFAULT);
  g_assert_true(style_parse_color("red", &c, NULL));
  g_assert_cmpint(c.kind, ==, STYLE_COLOR_INDEX);
  g_assert_cmpuint(c.value, ==, 1);
  g_assert_true(style_parse_color("brightBlue", &c, NULL));
  g_assert_cmpuint(c.value, ==, 12);
  g_assert_true(style_parse_color("208", &c, NULL));
  g_assert_cmpuint(c.value, ==, 208);
  g_assert_true(style_parse_color("#c0caf5", &c, NULL));
  g_assert_cmpint(c.kind, ==, STYLE_COLOR_RGB);
  g_assert_cmpuint(c.value, ==, 0xc0caf5);
  g_assert_false(style_parse_color("256", &c, NULL));
  g_assert_false(style_parse_color("#12345", &c, NULL));
  g_assert_false(style_parse_color("purple", &c, NULL));
}

static void test_specs(void)
{
  style_spec_t s;
  GError *err = NULL;

  g_assert_true(style_parse("fg=#c0caf5 bg=default bold", &s, NULL));
  g_assert_cmpint(s.fg.kind, ==, STYLE_COLOR_RGB);
  g_assert_cmpint(s.bg.kind, ==, STYLE_COLOR_DEFAULT);
  g_assert_cmpuint(s.attrs, ==, STYLE_BOLD);

  g_assert_true(style_parse("white on blue underline italic", &s, NULL));
  g_assert_cmpuint(s.fg.value, ==, 7);
  g_assert_cmpuint(s.bg.value, ==, 4);
  g_assert_cmpuint(s.attrs, ==, STYLE_UNDERLINE | STYLE_ITALIC);

  g_assert_true(style_parse("brightred", &s, NULL));
  g_assert_cmpuint(s.fg.value, ==, 9);
  g_assert_cmpint(s.bg.kind, ==, STYLE_COLOR_UNSET);

  g_assert_true(style_parse("dim", &s, NULL));
  g_assert_cmpint(s.fg.kind, ==, STYLE_COLOR_UNSET);
  g_assert_cmpuint(s.attrs, ==, STYLE_DIM);

  g_assert_false(style_parse("red green", &s, &err));
  g_clear_error(&err);
  g_assert_false(style_parse("fg=nope", &s, &err));
  g_clear_error(&err);
}

static void test_palette(void)
{
  g_assert_cmpuint(style_palette_rgb(1), ==, 0xcd0000);
  g_assert_cmpuint(style_palette_rgb(16), ==, 0x000000);
  g_assert_cmpuint(style_palette_rgb(196), ==, 0xff0000);
  g_assert_cmpuint(style_palette_rgb(231), ==, 0xffffff);
  g_assert_cmpuint(style_palette_rgb(232), ==, 0x080808);
  g_assert_cmpuint(style_rgb_to_256(0xff0000), ==, 196);
  g_assert_cmpuint(style_rgb_to_256(0x7aa2f7), ==, 111);
  g_assert_cmpuint(style_rgb_to_256(0x1a1b26), ==, 234);
  g_assert_cmpuint(style_rgb_to_16(0xff1010), ==, 9);
  g_assert_cmpuint(style_rgb_to_16(0x000010), ==, 0);
}

int main(int argc, char **argv)
{
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/style/colors", test_colors);
  g_test_add_func("/style/specs", test_specs);
  g_test_add_func("/style/palette", test_palette);
  return g_test_run();
}
