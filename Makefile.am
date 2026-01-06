# Copyright (C) 2014 Colin Walters <walters@verbum.org>
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, write to the
# Free Software Foundation, Inc., 59 Temple Place - Suite 330,
# Boston, MA 02111-1307, USA.

include Makefile-decls.am

privdatadir=$(pkglibdir)

ACLOCAL_AMFLAGS += -I m4 ${ACLOCAL_FLAGS}
AM_CPPFLAGS += -DDATADIR='"$(datadir)"' \
	-DLIBEXECDIR='"$(libexecdir)"' \
	-DLOCALEDIR=\"$(datadir)/locale\" \
	-DDEFAULT_EDITOR=\"$(default_editor)\"
AM_CFLAGS += $(WARN_CFLAGS)

EXTRA_DIST += meson.build
EXTRA_DIST += meson_options.txt
EXTRA_DIST += packaging/meson-compat-install-symlink.sh

GITIGNOREFILES += build-aux/ m4/ gtk-doc.make config.h.in aclocal.m4
GITIGNOREFILES += git-evtag-*.tar.*

include src/Makefile-git-evtag.am
include man/Makefile-man.am
include rust/Makefile-inc.am
include tests/Makefile-tests.am

-include $(top_srcdir)/git.mk

$(srcdir)/.gitignore: src/Makefile-git-evtag.am
$(srcdir)/.gitignore: man/Makefile-man.am
$(srcdir)/.gitignore: rust/Makefile-inc.am
$(srcdir)/.gitignore: tests/Makefile-tests.am

distcheck-hook: distcheck-hook-meson
distcheck-hook-meson:
	set -e; if command -v meson > /dev/null; then \
		cd $(distdir); \
		meson setup _build/meson; \
		meson compile -C _build/meson -v; \
		meson test -C _build/meson -v; \
		rm -fr _build/meson; \
	fi
