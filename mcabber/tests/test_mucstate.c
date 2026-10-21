#include <glib.h>
#include <glib/gstdio.h>

#include "mucstate.h"

static void test_state(void)
{
  gchar *dir = g_dir_make_tmp("mcng-mucstate-XXXXXX", NULL);
  gchar *path = g_build_filename(dir, "state", "rooms.ini", NULL);
  gchar *k1 = mucstate_key("abc", "nick", "text");
  gchar *k2 = mucstate_key(NULL, "nick", "text");
  gchar *k3 = mucstate_key(NULL, "nick", "other");
  gchar *k4 = mucstate_key("", "nick", "text");
  int i;

  g_assert_cmpstr(k1, ==, "id:abc");
  g_assert_true(g_str_has_prefix(k2, "h:"));
  g_assert_cmpstr(k2, !=, k3);
  g_assert_cmpstr(k2, ==, k4);    // An empty id is ignored

  mucstate_open(path);
  g_assert_cmpint(mucstate_last("room@conf.example.org"), ==, 0);
  g_assert_false(mucstate_seen("room@conf.example.org", k1));
  g_assert_false(mucstate_dirty());

  mucstate_add("room@conf.example.org", k1, 1000);
  mucstate_add("Room@Conf.Example.org", k2, 900);   // Older; same room
  g_assert_true(mucstate_dirty());
  g_assert_cmpint(mucstate_last("room@conf.example.org"), ==, 1000);
  g_assert_true(mucstate_seen("room@conf.example.org", k1));
  g_assert_true(mucstate_seen("room@conf.example.org", k2));
  g_assert_false(mucstate_seen("room@conf.example.org", k3));
  g_assert_false(mucstate_seen("other@conf.example.org", k1));
  g_assert_null(mucstate_topic("room@conf.example.org"));
  mucstate_set_topic("room@conf.example.org", "Topic [with] = signs");
  g_assert_true(mucstate_save());
  g_assert_false(mucstate_dirty());

  // Reloaded from the file
  mucstate_open(path);
  g_assert_cmpint(mucstate_last("room@conf.example.org"), ==, 1000);
  g_assert_true(mucstate_seen("room@conf.example.org", k2));
  g_assert_cmpstr(mucstate_topic("room@conf.example.org"), ==,
                  "Topic [with] = signs");

  // Only the last keys are kept
  for (i = 0; i < 250; i++) {
    gchar *k = g_strdup_printf("id:%d", i);
    mucstate_add("room@conf.example.org", k, 2000 + i);
    g_free(k);
  }
  g_assert_false(mucstate_seen("room@conf.example.org", k1));
  g_assert_false(mucstate_seen("room@conf.example.org", "id:0"));
  g_assert_true(mucstate_seen("room@conf.example.org", "id:249"));
  g_assert_true(mucstate_seen("room@conf.example.org", "id:100"));
  g_assert_cmpint(mucstate_last("room@conf.example.org"), ==, 2249);
  mucstate_close();

  g_unlink(path);
  g_free(path);
  path = g_build_filename(dir, "state", NULL);
  g_rmdir(path);
  g_rmdir(dir);
  g_free(path);
  g_free(dir);
  g_free(k1);
  g_free(k2);
  g_free(k3);
  g_free(k4);
}

int main(int argc, char **argv)
{
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/mucstate/state", test_state);
  return g_test_run();
}
