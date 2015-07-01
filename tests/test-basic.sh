#!/bin/bash
#
# Copyright (C) 2011 Colin Walters <walters@verbum.org>
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

set -e

echo "1..1"

. $(dirname $0)/libtest.sh

setup_test_repository
echo "ok setup"

cd ${test_tmpdir}
cat > editor.sh <<EOF
#!/bin/sh
echo Release 2015.1 > $$1
EOF
chmod a+x editor.sh
cd coolproject
env EDITOR=${test_tmpdir}/editor.sh git evtag -u 472CDAFA v2015.1
env EDITOR=${test_tmpdir}/editor.sh git evtag --verify v2015.1
echo "ok tag + verify"
