#include <string.h>
#include <glib.h>

#include "links.h"

// The links of text, separated by spaces
static gchar *found(const char *text)
{
  GArray *a = links_find(text);
  GString *s = g_string_new(NULL);
  guint i;
  for (i = 0; i < a->len; i++) {
    gchar *url = links_url(text, &g_array_index(a, link_span_t, i));
    if (i)
      g_string_append_c(s, ' ');
    g_string_append(s, url);
    g_free(url);
  }
  g_array_free(a, TRUE);
  return g_string_free(s, FALSE);
}

#define assert_found(text, expected) do { \
    gchar *r = found(text); \
    g_assert_cmpstr(r, ==, expected); \
    g_free(r); \
  } while (0)

static void test_find(void)
{
  assert_found("", "");
  assert_found("no link here", "");
  assert_found("see https://example.org/a?b=c&d#e now",
               "https://example.org/a?b=c&d#e");
  assert_found("two: http://a.b/x, and ftp://c.d/y.",
               "http://a.b/x ftp://c.d/y");
  assert_found("(https://en.wikipedia.org/wiki/Foo_(bar)) yes",
               "https://en.wikipedia.org/wiki/Foo_(bar)");
  assert_found("[https://a.b/c]", "https://a.b/c");
  assert_found("Join xmpp:room@conf.example.org?join!",
               "xmpp:room@conf.example.org?join");
  assert_found("mail mailto:me@example.org", "mailto:me@example.org");
  assert_found("www.example.org/page.", "https://www.example.org/page");
  assert_found("<https://a.b/c>", "https://a.b/c");
  assert_found("\"https://a.b/c\"", "https://a.b/c");
  assert_found("HTTPS://A.B/C", "HTTPS://A.B/C");
  assert_found("https://пример.рф/путь", "https://пример.рф/путь");
  // Not links
  assert_found("https:// xmpp: www. foo:bar notxmpp:x", "");
  // Across lines
  assert_found("a\nhttps://x.y/z\nb", "https://x.y/z");
}

static void test_labels(void)
{
  gchar **l;

  l = links_labels("asdf", 3);
  g_assert_cmpuint(g_strv_length(l), ==, 3);
  g_assert_cmpstr(l[0], ==, "a");
  g_assert_cmpstr(l[2], ==, "d");
  g_strfreev(l);

  // More targets than keys: two-key labels, no label is a prefix of another
  l = links_labels("abc", 5);
  g_assert_cmpuint(g_strv_length(l), ==, 5);
  {
    guint i, j;
    for (i = 0; l[i]; i++)
      for (j = 0; l[j]; j++)
        if (i != j)
          g_assert_false(g_str_has_prefix(l[j], l[i]));
  }
  g_assert_cmpuint(strlen(l[0]), ==, 1);    // The first ones are short
  g_assert_cmpuint(strlen(l[4]), ==, 2);
  g_strfreev(l);

  l = links_labels("ab", 100);
  g_assert_cmpuint(g_strv_length(l), ==, 100);
  g_strfreev(l);

  // UTF-8 keys, duplicates and spaces are ignored
  l = links_labels("фы фы", 2);
  g_assert_cmpstr(l[0], ==, "ф");
  g_assert_cmpstr(l[1], ==, "ы");
  g_strfreev(l);

  // Not enough keys
  l = links_labels("a", 2);
  g_assert_null(l[0]);
  g_strfreev(l);
  l = links_labels("a", 1);
  g_assert_cmpstr(l[0], ==, "a");
  g_strfreev(l);
  l = links_labels("abc", 0);
  g_assert_null(l[0]);
  g_strfreev(l);
}

static void test_normalize(void)
{
  gchar *s;

  g_assert_cmpuint(links_key_normalize('a'), ==, 'a');
  g_assert_cmpuint(links_key_normalize('A'), ==, 'a');
  g_assert_cmpuint(links_key_normalize(0x0444), ==, 'a');   // ф
  g_assert_cmpuint(links_key_normalize(0x0424), ==, 'a');   // Ф
  g_assert_cmpuint(links_key_normalize(0x044b), ==, 's');   // ы
  g_assert_cmpuint(links_key_normalize(0x0456), ==, 's');   // і
  g_assert_cmpuint(links_key_normalize('1'), ==, '1');
  s = links_normalize("ФыВа jk");
  g_assert_cmpstr(s, ==, "asdf jk");
  g_free(s);
  s = links_normalize("олдж");   // jkl;
  g_assert_cmpstr(s, ==, "jkl;");
  g_free(s);
}

int main(int argc, char **argv)
{
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/links/normalize", test_normalize);
  g_test_add_func("/links/find", test_find);
  g_test_add_func("/links/labels", test_labels);
  return g_test_run();
}
