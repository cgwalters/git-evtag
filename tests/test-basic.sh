#!/bin/bash
#
# Copyright (C) 2011,2015 Colin Walters <walters@verbum.org>
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
set -x
set -o pipefail

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
git clone repos/coolproject
cd coolproject
env EDITOR=${test_tmpdir}/editor.sh git evtag -u 472CDAFA v2015.1
git show refs/tags/v2015.1 > tag.txt
assert_file_has_content tag.txt 'Git-EVTag-v0-SHA512: 9218351b9b478c80ca8da6b187da82b10d041f5907731a5274fa46b7674d9d39f3ed81365966f2c5af09ef9d72079aea7c32c4442ee954febde00ac1e3faf26'
env EDITOR=${test_tmpdir}/editor.sh git evtag --verify v2015.1 | tee verify.out
assert_file_has_content verify.out 'Successfully verified: Git-EVTag-v0-SHA512: 9218351b9b478c80ca8da6b187da82b10d041f5907731a5274fa46b7674d9d39f3ed81365966f2c5af09ef9d72079aea7c32c4442ee954febde00ac1e3faf26'
echo "ok tag + verify"
rm -f tag.txt
rm -f verify.out
