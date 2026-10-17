#include <glib.h>

#include "xmlsplit.h"

static void check_split(const char *in, const char **expected)
{
  gchar **parts = xml_split_stanzas(in);
  guint i;
  for (i = 0; expected[i]; i++)
    g_assert_cmpstr(parts[i], ==, expected[i]);
  g_assert_null(parts[i]);
  g_strfreev(parts);
}

static void test_split(void)
{
  const char *e1[] = { "<?xml version='1.0'?>",
                       "<stream:stream to='x' version=\"1.0\">", NULL };
  const char *e2[] = { "<message to='a'><body>hi > there</body></message>",
                       "<presence/>", "<iq type='get' id='1'><query/></iq>",
                       NULL };
  const char *e3[] = { "<r a='/>'/>", NULL };
  const char *e4[] = { "</stream:stream>", NULL };
  check_split("<?xml version='1.0'?><stream:stream to='x' version=\"1.0\">",
              e1);
  check_split("<message to='a'><body>hi > there</body></message>"
              "<presence/> <iq type='get' id='1'><query/></iq>", e2);
  check_split("<r a='/>'/>", e3);
  check_split("</stream:stream>", e4);
}

static void test_mask(void)
{
  gchar *m;
  m = xml_mask_secrets("<auth xmlns='sasl' mechanism='PLAIN'>c2VjcmV0</auth>");
  g_assert_cmpstr(m, ==, "<auth xmlns='sasl' mechanism='PLAIN'>***</auth>");
  g_free(m);
  m = xml_mask_secrets("<response xmlns='x'>abc</response><authors>x</authors>");
  g_assert_cmpstr(m, ==, "<response xmlns='x'>***</response><authors>x</authors>");
  g_free(m);
  m = xml_mask_secrets("<query><password>pw</password></query><auth/>");
  g_assert_cmpstr(m, ==, "<query><password>***</password></query><auth/>");
  g_free(m);
}

int main(int argc, char **argv)
{
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/xmlsplit/split", test_split);
  g_test_add_func("/xmlsplit/mask", test_mask);
  return g_test_run();
}
