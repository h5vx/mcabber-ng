#include <locale.h>
#include <string.h>
#include <glib.h>

#include "fmt.h"

static gchar *getter(const char *name, const char *arg, gpointer data)
{
  GHashTable *h = data;
  const char *v;
  if (arg)
    return g_strdup_printf("%s(%s)", name, arg);
  v = g_hash_table_lookup(h, name);
  return v ? g_strdup(v) : NULL;
}

static GHashTable *fields;

static gchar *render(const char *tmpl, guint width, sstr_t **ps)
{
  GError *err = NULL;
  fmt_t *f = fmt_compile(tmpl, &err);
  sstr_t *s = sstr_new();
  gchar *res;

  if (!f)
    g_error("cannot compile \"%s\": %s", tmpl, err->message);
  fmt_render(f, getter, fields, width, s);
  fmt_free(f);
  res = g_strdup(s->text->str);
  g_assert_cmpuint(s->width, ==, fmt_text_width(res, -1));
  if (ps)
    *ps = s;
  else
    sstr_free(s);
  return res;
}

static void check(const char *tmpl, guint width, const char *expected)
{
  gchar *r = render(tmpl, width, NULL);
  g_assert_cmpstr(r, ==, expected);
  g_free(r);
}

static void test_fields(void)
{
  check("hello", 0, "hello");
  check("{nick}: hi", 0, "bob: hi");
  check("<{nick}>", 0, "<bob>");
  check("{missing}x", 0, "x");
  check("{time:%H:%M}", 0, "time(%H:%M)");
  check("{{literal}} }", 0, "{literal} }");
}

static void test_align(void)
{
  check("[{nick:6}]", 0, "[bob   ]");
  check("[{nick:>6}]", 0, "[   bob]");
  check("[{nick:^7}]", 0, "[  bob  ]");
  check("[{long:.4}]", 0, "[abcd]");
  check("[{long:>8.4}]", 0, "[    abcd]");
  check("[{wide:>5}]", 0, "[ 日本]");
  check("[{wide:.3}]", 0, "[日]");
}

static void test_conditions(void)
{
  check("{?nick}<{nick}> {/}text", 0, "<bob> text");
  check("{?missing}<{missing}> {/}text", 0, "text");
  check("{!missing}none{/}", 0, "none");
  check("{?zero}yes{/}{!zero}no{/}", 0, "no");
  check("{?empty}yes{/}{!empty}no{/}", 0, "no");
  check("{?nick}a{?missing}b{/}c{/}d", 0, "acd");
  check("{?missing}a{?nick}b{/}c{/}d", 0, "d");
}

static void test_styles(void)
{
  sstr_t *s;
  srun_t *r;
  gchar *t = render("{#time}12:00{#} {#nick}{nick}{#}: hi", 0, &s);

  g_assert_cmpstr(t, ==, "12:00 bob: hi");
  g_assert_cmpuint(s->runs->len, ==, 4);
  r = &g_array_index(s->runs, srun_t, 0);
  g_assert_cmpstr(r->style, ==, "time");
  g_assert_cmpuint(r->len, ==, 5);
  r = &g_array_index(s->runs, srun_t, 1);
  g_assert_null(r->style);
  r = &g_array_index(s->runs, srun_t, 2);
  g_assert_cmpstr(r->style, ==, "nick");
  g_assert_cmpuint(r->start, ==, 6);
  g_free(t);
  sstr_free(s);

  t = render("{#c_$nick}x{#c_$missing}y", 0, &s);
  r = &g_array_index(s->runs, srun_t, 0);
  g_assert_cmpstr(r->style, ==, "c_bob");
  r = &g_array_index(s->runs, srun_t, 1);
  g_assert_cmpstr(r->style, ==, "c_");
  g_free(t);
  sstr_free(s);
}

static void test_width(void)
{
  // Right alignment
  check("{nick}{>}[{n}]", 12, "bob      [3]");
  check("{nick}{>}[{n}]", 0, "bob[3]");
  check("{nick}{>}{missing}", 12, "bob");
  // Flexible field
  check("<{long:*}>", 6, "<abc…>");
  check("<{long:*}>", 20, "<abcdefgh>");
  check("{long:*}{>}|{nick}", 8, "abc…|bob");
  check("{long:*}{>}|{nick}", 15, "abcdefgh   |bob");
}

static void test_errors(void)
{
  GError *err = NULL;
  g_assert_null(fmt_compile("{nick", &err));
  g_clear_error(&err);
  g_assert_null(fmt_compile("{?a}x", &err));
  g_clear_error(&err);
  g_assert_null(fmt_compile("x{/}", &err));
  g_clear_error(&err);
  g_assert_null(fmt_compile("{:5}", &err));
  g_clear_error(&err);
}

int main(int argc, char **argv)
{
  int ret;
  setlocale(LC_ALL, "C.UTF-8");
  g_test_init(&argc, &argv, NULL);
  fields = g_hash_table_new(g_str_hash, g_str_equal);
  g_hash_table_insert(fields, "nick", "bob");
  g_hash_table_insert(fields, "long", "abcdefgh");
  g_hash_table_insert(fields, "wide", "日本");
  g_hash_table_insert(fields, "zero", "0");
  g_hash_table_insert(fields, "empty", "");
  g_hash_table_insert(fields, "n", "3");
  g_test_add_func("/fmt/fields", test_fields);
  g_test_add_func("/fmt/align", test_align);
  g_test_add_func("/fmt/conditions", test_conditions);
  g_test_add_func("/fmt/styles", test_styles);
  g_test_add_func("/fmt/width", test_width);
  g_test_add_func("/fmt/errors", test_errors);
  ret = g_test_run();
  g_hash_table_destroy(fields);
  return ret;
}
