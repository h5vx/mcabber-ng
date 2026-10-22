SUBDIRS = mcabber doc modules tests
ACLOCAL_AMFLAGS = -I macros
if INSTALL_HEADERS
pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = mcabber.pc
endif

# Sample configuration file
dist_pkgdata_DATA = init.lua.example

# Lua modules: themes
# Type annotations (LuaCATS) of the Lua API, for lua-language-server
typesdir = $(pkgdatadir)/types
dist_types_DATA = types/mcabber-ng.lua

luathemedir = $(pkgdatadir)/lua/mcabber/themes
dist_luatheme_DATA = lua/mcabber/themes/classic.lua \
		     lua/mcabber/themes/modern.lua \
		     lua/mcabber/themes/colorcli.lua \
		     lua/mcabber/themes/light.lua \
		     lua/mcabber/themes/light2.lua \
		     lua/mcabber/themes/mikael-256.lua \
		     lua/mcabber/themes/oliver.lua
