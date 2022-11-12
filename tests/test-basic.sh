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
# You should have received a copy of the GNU Lesser General
# Public License along with this library; if not, see <http://www.gnu.org/licenses/>.

set -e
set -x
set -o pipefail

echo "1..7"

. $(dirname $0)/libtest.sh

setup_test_repository
cd ${test_tmpdir}
create_editor_script 'Release 2015.1'
echo "ok setup"

git clone repos/coolproject >&2
cd coolproject
trusted_git_submodule update --init >&2
with_editor_script git evtag sign -u 472CDAFA v2015.1 >&2
git show refs/tags/v2015.1 > tag.txt
sed -e 's/^/#tag.txt /' < tag.txt
${SRCDIR}/git-evtag-compute-py HEAD > tag-py.txt
sed -e 's/^/#tag-py.txt /' < tag-py.txt
TAG='Git-EVTag-v0-SHA512: 58e9834248c054f844f00148a030876f77eb85daa3caa15a20f3061f181403bae7b7e497fca199d25833b984c60f3202b16ebe0ed3a36e6b82f33618d75c569d'
assert_file_has_content tag.txt "${TAG}"
with_editor_script git evtag verify v2015.1 | tee verify.out >&2
assert_file_has_content verify.out "Successfully verified: ${TAG}"
# Also test subdirectory
(cd src && with_editor_script git evtag verify v2015.1 | tee ../verify2.out) >&2
assert_file_has_content verify2.out "Successfully verified: ${TAG}"
assert_file_has_content tag-py.txt "${TAG}"

rm -f tag.txt
rm -f verify.out
echo "ok tag + verify"

cd ${test_tmpdir}
rm coolproject -rf
git clone repos/coolproject >&2
cd coolproject
trusted_git_submodule update --init >&2
echo 'super cool' > src/cool.c
if with_editor_script git evtag sign -u 472CDAFA v2015.1 >&2 2>err.txt; then
    assert_not_reached "expected failure due to dirty tree"
fi
assert_file_has_content err.txt 'Attempting to tag or verify dirty tree'
git checkout src/cool.c >&2
# But untracked files are ok
touch unknownfile
with_editor_script git evtag sign -u 472CDAFA v2015.1 >&2
git evtag verify v2015.1 >&2
echo 'ok no tag on dirty tree'


cd ${test_tmpdir}
rm coolproject -rf
git clone repos/coolproject >&2
cd coolproject
trusted_git_submodule update --init >&2
with_editor_script git evtag sign -u 472CDAFA v2015.1 >&2
git checkout -q HEAD^ >&2
if git evtag verify v2015.1 >&2 2>err.txt; then
    assert_not_reached 'Expected failure due to non HEAD'
fi
assert_file_has_content err.txt "Target.*is not HEAD"
echo 'ok no tag verify on non-HEAD'

cd ${test_tmpdir}
rm coolproject -rf
git clone repos/coolproject >&2
cd coolproject
trusted_git_submodule update --init >&2
with_editor_script git evtag sign --with-legacy-archive-tag -u 472CDAFA v2015.1 >&2
git show refs/tags/v2015.1 > tag.txt
assert_file_has_content tag.txt 'ExtendedVerify-SHA256-archive-tar: 83991ee23a027d97ad1e06432ad87c6685e02eac38706e7fbfe6e5e781939dab'
gitversion=$(git --version)
assert_file_has_content tag.txt "ExtendedVerify-git-version: ${gitversion}"
with_editor_script git evtag verify v2015.1 | tee verify.out >&2
assert_file_has_content verify.out 'Successfully verified: Git-EVTag-v0-SHA512: 58e9834248c054f844f00148a030876f77eb85daa3caa15a20f3061f181403bae7b7e497fca199d25833b984c60f3202b16ebe0ed3a36e6b82f33618d75c569d'
rm -f tag.txt
rm -f verify.out
echo "ok tag + verify legacy"

cd ${test_tmpdir}
rm coolproject -rf
git clone repos/coolproject >&2
cd coolproject
trusted_git_submodule update --init >&2
with_editor_script git evtag sign --no-signature v2015.1 >&2
git show refs/tags/v2015.1 > tag.txt
assert_file_has_content tag.txt 'Git-EVTag-v0-SHA512: 58e9834248c054f844f00148a030876f77eb85daa3caa15a20f3061f181403bae7b7e497fca199d25833b984c60f3202b16ebe0ed3a36e6b82f33618d75c569d'
assert_not_file_has_content tag.txt '-----BEGIN PGP SIGNATURE-----'
if git evtag verify v2015.1 2>err.txt; then
    assert_not_reached 'Expected failure due to no GPG signature'
fi
assert_file_has_content err.txt "no signature found"
git evtag verify --no-signature v2015.1 >&2
rm -f tag.txt
rm -f verify.out
echo "ok checksum-only tag + verify"

cd ${test_tmpdir}
rm coolproject -rf
git clone repos/coolproject2 >&2
cd coolproject2
trusted_git_submodule update --init >&2
with_editor_script git evtag sign -u 472CDAFA v2015.1 >&2
git show refs/tags/v2015.1 > tag.txt
TAG='Git-EVTag-v0-SHA512: 8ef922041663821b8208d6e1037adbd51e0b19cc4dd3314436b3078bdae4073a616e6e289891fa5ad9f798630962a33350f6035fffec6ca3c499bc01f07c3d0a'
assert_file_has_content tag.txt "${TAG}"
with_editor_script git evtag verify v2015.1 | tee verify.out >&2
assert_file_has_content verify.out "Successfully verified: ${TAG}"
# Also test subdirectory
(cd src && with_editor_script git evtag verify v2015.1 | tee ../verify2.out) >&2
assert_file_has_content verify2.out "Successfully verified: ${TAG}"
${SRCDIR}/git-evtag-compute-py HEAD > tag-py.txt
assert_file_has_content tag-py.txt "${TAG}"

rm -f tag.txt
rm -f verify.out
echo "ok tag + verify with nested submodules"
