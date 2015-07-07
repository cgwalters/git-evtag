# Source library for shell script tests
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

SRCDIR=$(dirname $0)
test_tmpdir=$(pwd)

export G_DEBUG=fatal-warnings

export TEST_GPG_KEYID_1="472CDAFA"
export TEST_GPG_KEYID_2="CA950D41"
export TEST_GPG_KEYID_3="DF444D67"

# GPG when creating signatures demands a writable
# homedir in order to create lockfiles.  Work around
# this by copying locally.
cp -a ${SRCDIR}/gpghome ${test_tmpdir}
chmod 0700 ${test_tmpdir}/gpghome
export GNUPGHOME=${test_tmpdir}/gpghome

if test -n "${OT_TESTS_DEBUG}"; then
    set -x
fi

assert_not_reached () {
    echo $@ 1>&2; exit 1
}

assert_streq () {
    test "$1" = "$2" || (echo 1>&2 "$1 != $2"; exit 1)
}

assert_not_streq () {
    (! test "$1" = "$2") || (echo 1>&2 "$1 == $2"; exit 1)
}

assert_has_file () {
    test -f "$1" || (echo 1>&2 "Couldn't find '$1'"; exit 1)
}

assert_has_dir () {
    test -d "$1" || (echo 1>&2 "Couldn't find '$1'"; exit 1)
}

assert_not_has_file () {
    if test -f "$1"; then
	echo 1>&2 "File '$1' exists"; exit 1
    fi
}

assert_not_file_has_content () {
    if grep -q -e "$2" "$1"; then
	echo 1>&2 "File '$1' incorrectly matches regexp '$2'"; exit 1
    fi
}

assert_not_has_dir () {
    if test -d "$1"; then
	echo 1>&2 "Directory '$1' exists"; exit 1
    fi
}

assert_file_has_content () {
    if ! grep -q -e "$2" "$1"; then
	echo 1>&2 "File '$1' doesn't match regexp '$2'"; exit 1
    fi
}

assert_file_empty() {
    if test -s "$1"; then
	echo 1>&2 "File '$1' is not empty"; exit 1
    fi
}

gitcommit_reset_time() {
    TSCOUNTER=1436222301
}
gitcommit_inctime() {
    TSCOUNTER=$(($TSCOUNTER + 1))
    TSV="$TSCOUNTER +0000" 
    env GIT_AUTHOR_DATE="$TSV" GIT_COMMITTER_DATE="$TSV" git commit "$@"
}

setup_test_repository () {
    oldpwd=`pwd`

    cd ${test_tmpdir}
    mkdir coolproject
    cd coolproject
    git init
    gitcommit_reset_time
    echo 'So cool!' > README.md
    git add .
    gitcommit_inctime -a -m 'Initial commit'
    mkdir src
    echo 'printf("hello world")' > src/cool.c
    git add .
    gitcommit_inctime -a -m 'Add C source'

    cd ${test_tmpdir}
    mkdir -p repos/coolproject
    cd repos/coolproject && git init --bare
    cd ${test_tmpdir}/coolproject
    git remote add origin file://${test_tmpdir}/repos/coolproject
    git push --set-upstream origin master

    cd ${test_tmpdir}
    mkdir subproject
    cd subproject
    git init
    echo 'this is libsub.c' > libsub.c
    echo 'An example submodule' > README.md
    git add .
    git commit -a -m 'init'
    mkdir src
    mv libsub.c src
    echo 'an update to libsub.c, now in src/' > src/libsub.c
    git commit -a -m 'an update'

    cd ${test_tmpdir}
    mkdir -p repos/subproject
    cd repos/subproject && git init --bare
    cd ${test_tmpdir}/subproject
    git remote add origin file://${test_tmpdir}/repos/subproject
    git push --set-upstream origin master

    cd ${test_tmpdir}/coolproject
    git submodule add file://${test_tmpdir}/repos/subproject subproject 
    git add subproject
    echo '#include subproject/src/libsub.c' >> src/cool.c
    git commit -a -m 'Add libsub'

    cd ${test_tmpdir}
    rm coolproject -rf
    rm subproject -rf

    cd $oldpwd
}
