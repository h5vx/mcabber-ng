SUBDIRS = mcabber doc modules tests
ACLOCAL_AMFLAGS = -I macros
if INSTALL_HEADERS
pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = mcabber.pc
endif

# Sample configuration file
dist_pkgdata_DATA = init.lua.example

# Lua modules: themes
luathemedir = $(pkgdatadir)/lua/mcabber/themes
dist_luatheme_DATA = lua/mcabber/themes/classic.lua \
		     lua/mcabber/themes/modern.lua
