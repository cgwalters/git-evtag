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
cd ${test_tmpdir}
create_editor_script 'Release 2015.1'
echo "ok setup"

git clone repos/coolproject
cd coolproject
git submodule update --init
with_editor_script git evtag -u 472CDAFA v2015.1
git show refs/tags/v2015.1 > tag.txt
assert_file_has_content tag.txt 'Git-EVTag-EXPERIMENTAL-v0-SHA512: 9a08bd77a94e3623f128a8e688aa39837e9a0d8d20ad1e6fff9954f3266efa64bc4ecbcc3caa196110dd14e37926190d860cbc7e16cec48d7d1d43e70c239690'
with_editor_script git evtag --verify v2015.1 | tee verify.out
assert_file_has_content verify.out 'Successfully verified: Git-EVTag-EXPERIMENTAL-v0-SHA512: 9a08bd77a94e3623f128a8e688aa39837e9a0d8d20ad1e6fff9954f3266efa64bc4ecbcc3caa196110dd14e37926190d860cbc7e16cec48d7d1d43e70c239690'
rm -f tag.txt
rm -f verify.out
echo "ok tag + verify"


cd ${test_tmpdir}
rm coolproject -rf
git clone repos/coolproject
cd coolproject
git submodule update --init
echo 'super cool' > src/cool.c
if with_editor_script git evtag -u 472CDAFA v2015.1 2>err.txt; then
    assert_not_reached "expected failure due to dirty tree"
fi
assert_file_has_content err.txt 'Attempting to tag or verify dirty tree'
git checkout src/cool.c
# But untracked files are ok
touch unknownfile
with_editor_script git evtag -u 472CDAFA v2015.1
git evtag --verify v2015.1
echo 'ok no tag on dirty tree'


cd ${test_tmpdir}
rm coolproject -rf
git clone repos/coolproject
cd coolproject
git submodule update --init
if with_editor_script git evtag -u 472CDAFA v2015.1 HEAD^ 2>err.txt; then
    assert_not_reached 'Expected failure due to non HEAD'
fi
assert_file_has_content err.txt "Target.*is not HEAD"
echo 'ok no tag on non-HEAD'


cd ${test_tmpdir}
rm coolproject -rf
git clone repos/coolproject
cd coolproject
git submodule update --init
with_editor_script git evtag -u 472CDAFA v2015.1
git checkout -q HEAD^
if git evtag --verify v2015.1 2>err.txt; then
    assert_not_reached 'Expected failure due to non HEAD'
fi
assert_file_has_content err.txt "Target.*is not HEAD"
echo 'ok no tag verify on non-HEAD'


