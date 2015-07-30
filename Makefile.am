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
	-DLOCALEDIR=\"$(datadir)/locale\"
AM_CFLAGS += $(WARN_CFLAGS)

include $(INTROSPECTION_MAKEFILE)

girdir = $(pkgdatadir)/gir-1.0
gir_DATA = $(INTROSPECTION_GIRS)
typelibdir = $(pkglibdir)/girepository-1.0
typelib_DATA = $(gir_DATA:.gir=.typelib)

include src/Makefile-git-evtag.am
include tests/Makefile-tests.am
