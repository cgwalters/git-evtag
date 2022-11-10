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
# You should have received a copy of the GNU Lesser General
# Public License along with this library; if not, see <http://www.gnu.org/licenses/>.

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

trusted_git_submodule () {
    # Git forbids file:/// for submodules by default, to avoid untrusted
    # file:/// repositories being able to break a security boundary
    # (CVE-2022-39253).
    # In this test suite, all the repositories are under our control and
    # we trust them, so bypass that.
    git -c protocol.file.allow=always submodule "$@"
}

setup_test_repository () {
    oldpwd=`pwd`

    cd ${test_tmpdir}
    mkdir coolproject
    cd coolproject
    git init -b mybranch
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
    cd repos/coolproject && git init --bare -b mybranch
    cd ${test_tmpdir}/coolproject
    git remote add origin file://${test_tmpdir}/repos/coolproject
    git push --set-upstream origin mybranch

    cd ${test_tmpdir}
    mkdir subproject
    cd subproject
    git init -b mybranch
    echo 'this is libsub.c' > libsub.c
    echo 'An example submodule' > README.md
    git add .
    gitcommit_inctime -a -m 'init'
    mkdir src
    mv libsub.c src
    echo 'an update to libsub.c, now in src/' > src/libsub.c
    gitcommit_inctime -a -m 'an update'
    cd ${test_tmpdir}
    mkdir -p repos/subproject
    cd repos/subproject && git init --bare -b mybranch
    cd ${test_tmpdir}/subproject
    git remote add origin file://${test_tmpdir}/repos/subproject
    git push --set-upstream origin mybranch

    cd ${test_tmpdir}/coolproject
    trusted_git_submodule add ../subproject subproject
    git add subproject
    echo '#include subproject/src/libsub.c' >> src/cool.c
    gitcommit_inctime -a -m 'Add libsub'
    git push origin mybranch

    # Copy coolproject to create another version which has two submodules,
    # one which is nested deeper in the repository.
    cd ${test_tmpdir}
    cp -r repos/coolproject repos/coolproject2
    git clone file://${test_tmpdir}/repos/coolproject2
    cd coolproject2
    mkdir subprojects
    trusted_git_submodule add ../subproject subprojects/subproject
    git add subprojects/subproject
    gitcommit_inctime -a -m 'Add subprojects/subproject'
    git push origin mybranch

    cd ${test_tmpdir}
    rm coolproject -rf
    rm coolproject2 -rf
    rm subproject -rf

    cd $oldpwd
}

create_editor_script() {
    cat >${test_tmpdir}/editor.sh <<EOF
#!/bin/sh
buf=\$(cat \$1)
(echo $1 && echo "\$buf") > \$1
EOF
    chmod a+x editor.sh
}

with_editor_script() {
    env EDITOR=${test_tmpdir}/editor.sh "$@"
}
