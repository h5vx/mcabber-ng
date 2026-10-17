Name:           mcabber-ng
Version:        %{pkgversion}
Release:        1%{?dist}
Summary:        Jabber (XMPP) console client configured in Lua
License:        GPL-2.0-or-later
URL:            https://github.com/h5vx/mcabber-ng
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  gcc make autoconf automake libtool pkgconfig
BuildRequires:  pkgconfig(ncursesw)
BuildRequires:  pkgconfig(glib-2.0)
BuildRequires:  pkgconfig(loudmouth-1.0)
BuildRequires:  pkgconfig(luajit)
BuildRequires:  gpgme-devel
BuildRequires:  pkgconfig(libidn)
BuildRequires:  pkgconfig(enchant-2)
BuildRequires:  pkgconfig(libomemo-c)
# libomemo-c.pc links with protobuf-c
BuildRequires:  pkgconfig(libprotobuf-c)
BuildRequires:  pkgconfig(libgcrypt)
BuildRequires:  libotr-devel

%description
mcabber-ng is a fork of mcabber, a small Jabber (XMPP) client for the
console.  Its configuration is a Lua script: options, key bindings with
readable names, event handlers, message and interface formats, themes.

%prep
%autosetup

%build
autoreconf -fi -I macros
%configure --enable-enchant --enable-omemo=yes --enable-otr --disable-hgcset
%make_build

%check
make check

%install
%make_install
rm -rf %{buildroot}%{_includedir} %{buildroot}%{_libdir}/pkgconfig
find %{buildroot} -name '*.la' -delete

%files
%license COPYING
%doc README NEWS
%{_bindir}/mcabber-ng
%{_libdir}/mcabber-ng/
%{_datadir}/mcabber-ng/
%{_mandir}/man1/mcabber-ng.1*
