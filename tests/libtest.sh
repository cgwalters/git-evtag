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

setup_test_repository () {
    oldpwd=`pwd`

    cd ${test_tmpdir}
    mkdir coolproject
    cd coolproject
    git init
    echo 'So cool!' > README.md
    git add README.md
    git commit -a -m 'Initial commit'
    mkdir src
    echo 'printf("hello world")' > src/cool.c
    git add src
    git commit -a -m 'Add C source'

    cd $oldpwd
}
