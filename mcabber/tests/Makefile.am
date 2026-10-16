# Unit tests for the code in ../mcabber/libmcpure.a
check_PROGRAMS = test_utf8 test_keys test_layout test_inputlayout test_paste test_fmt test_style
TESTS = $(check_PROGRAMS)

# Globals normally provided by the full program
check_LIBRARIES = libstubs.a
libstubs_a_SOURCES = stubs.c

AM_CPPFLAGS = -I$(top_builddir) -I$(top_srcdir) -I$(top_srcdir)/mcabber \
	      $(GLIB_CFLAGS)
LDADD = $(top_builddir)/mcabber/libmcpure.a libstubs.a $(GLIB_LIBS)

test_utf8_SOURCES = test_utf8.c
test_keys_SOURCES = test_keys.c
test_layout_SOURCES = test_layout.c
test_inputlayout_SOURCES = test_inputlayout.c
test_paste_SOURCES = test_paste.c
test_fmt_SOURCES = test_fmt.c
test_style_SOURCES = test_style.c
