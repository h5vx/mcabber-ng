bin_PROGRAMS = mcabber-ng

# Code that does not depend on ncurses, XMPP or the Lua state.
# It is linked into the program and into the unit tests (see ../tests).
noinst_LIBRARIES = libmcpure.a
libmcpure_a_SOURCES = utf8.c utf8.h keys.c keys.h

mcabber_ng_SOURCES = main.c main.h roster.c roster.h events.c events.h \
		  commands.c commands.h compl.c compl.h \
		  hbuf.c hbuf.h screen.c screen.h logprint.h \
		  settings.c settings.h hooks.c hooks.h \
		  histolog.c histolog.h utils.c utils.h pgp.c pgp.h \
		  xmpp.c xmpp.h xmpp_helper.c xmpp_helper.h xmpp_defines.h \
		  xmpp_iq.c xmpp_iq.h xmpp_iqrequest.c xmpp_iqrequest.h \
		  xmpp_muc.c xmpp_muc.h xmpp_s10n.c xmpp_s10n.h \
		  caps.c caps.h help.c help.h carbons.c carbons.h \
		  mlua.c mlua.h mlua_compat.h mlua_api.c
nodist_mcabber_ng_SOURCES = mlua_prelude.h
EXTRA_DIST = mlua_prelude.lua

# The Lua prelude is compiled into the program as a C string
mlua_prelude.h: $(srcdir)/mlua_prelude.lua
	$(AM_V_GEN)( echo 'static const char mlua_prelude[] ='; \
	  sed -e 's/\\/\\\\/g' -e 's/"/\\"/g' -e 's/^/  "/' -e 's/$$/\\n"/' \
	    $(srcdir)/mlua_prelude.lua; echo ';' ) > $@

if OTR
mcabber_ng_SOURCES += otr.c otr.h nohtml.c nohtml.h
endif

LDADD = libmcpure.a $(GLIB_LIBS) $(LOUDMOUTH_LIBS) $(GPGME_LIBS) \
				$(LIBOTR_LIBS) $(ENCHANT_LIBS) $(LIBIDN_LIBS) $(LUA_LIBS)

AM_CPPFLAGS = -I$(top_builddir) -I$(top_srcdir) \
				$(GLIB_CFLAGS) $(LOUDMOUTH_CFLAGS) \
				$(GPGME_CFLAGS) $(LIBOTR_CFLAGS) \
				$(ENCHANT_CFLAGS) $(LIBIDN_CFLAGS) $(LUA_CFLAGS)

CLEANFILES = hgcset.h mlua_prelude.h
BUILT_SOURCES = mlua_prelude.h

if HGCSET
BUILT_SOURCES += hgcset.h

hgcset.h: remake_hgcset
	$(top_srcdir)/hgcset.sh "$(top_srcdir)/mcabber" "$(top_builddir)/mcabber"

.PHONY: remake_hgcset
endif

if INSTALL_HEADERS
mcabber_ng_SOURCES += modules.c modules.h api.h
mcabberinclude_HEADERS = main.h roster.h events.h \
			 commands.h compl.h \
			 hbuf.h screen.h logprint.h \
			 settings.h hooks.h utf8.c utf8.h keys.h \
			 histolog.h utils.h pgp.h \
			 xmpp.h xmpp_helper.h xmpp_defines.h \
			 xmpp_iq.h xmpp_iqrequest.h \
			 xmpp_muc.h xmpp_s10n.h \
			 caps.h fifo.h help.h modules.h api.h \
			 $(top_builddir)/include/config.h

if OTR
mcabberinclude_HEADERS += otr.h nohtml.h
endif

if HGCSET
mcabberinclude_HEADERS += hgcset.h
endif

mcabberincludedir = $(includedir)/mcabber
else
mcabber_ng_SOURCES += fifo_internal.c fifo.h
endif

#SUBDIRS =
