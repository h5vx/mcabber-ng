# Unit tests for the code in ../mcabber/libmcpure.a
check_PROGRAMS = test_utf8
TESTS = $(check_PROGRAMS)

# Globals normally provided by the full program
check_LIBRARIES = libstubs.a
libstubs_a_SOURCES = stubs.c

AM_CPPFLAGS = -I$(top_builddir) -I$(top_srcdir) -I$(top_srcdir)/mcabber \
	      $(GLIB_CFLAGS)
LDADD = $(top_builddir)/mcabber/libmcpure.a libstubs.a $(GLIB_LIBS)

test_utf8_SOURCES = test_utf8.c
