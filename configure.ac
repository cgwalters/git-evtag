AC_PREREQ([2.63])
AC_INIT([git-evtag], [2022.1], [walters@verbum.org])
AC_CONFIG_HEADER([config.h])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_AUX_DIR([build-aux])

AM_INIT_AUTOMAKE([1.11 -Wno-portability foreign no-define tar-ustar no-dist-gzip dist-xz subdir-objects])
AM_MAINTAINER_MODE([enable])
AM_SILENT_RULES([yes])
AC_USE_SYSTEM_EXTENSIONS
AC_SYS_LARGEFILE

# TAP test driver support
AC_PROG_AWK
AC_REQUIRE_AUX_FILE([tap-driver.sh])

AC_PROG_CC
AM_PROG_CC_C_O

changequote(,)dnl
if test "x$GCC" = "xyes"; then
  WARN_CFLAGS="-Wall -Wstrict-prototypes -Werror=missing-prototypes \
		-Werror=implicit-function-declaration \
		-Werror=pointer-arith -Werror=init-self -Werror=format=2 \
		-Werror=format-security \
		-Werror=missing-include-dirs -Werror=aggregate-return \
		-Werror=declaration-after-statement"
fi
changequote([,])dnl
AC_SUBST(WARN_CFLAGS)

# Initialize libtool
LT_PREREQ([2.2.4])
LT_INIT([disable-static])

PKG_PROG_PKG_CONFIG
PKG_CHECK_MODULES(BUILDDEP_LIBGIT_GLIB, [libgit2 gio-2.0])
save_LIBS=$LIBS
LIBS=$BUILDDEP_LIBGIT_GLIB_LIBS
AC_CHECK_FUNCS(git_libgit2_init)
LIBS=$save_LIBS

AC_ARG_ENABLE(man,
              [AS_HELP_STRING([--enable-man],
                              [generate man pages [default=auto]])],,
              enable_man=maybe)

AS_IF([test "$enable_man" != no], [
  AC_PATH_PROG([XSLTPROC], [xsltproc])
  AS_IF([test -z "$XSLTPROC"], [
    AS_IF([test "$enable_man" = yes], [
      AC_MSG_ERROR([xsltproc is required for --enable-man])
    ])
    enable_man=no
  ])
  enable_man=yes
])
AM_CONDITIONAL(ENABLE_MAN, test "$enable_man" != no)

AC_ARG_ENABLE(rust,
              [AS_HELP_STRING([--enable-rust],
                              [Compile Rust version [default=no]])],,
              enable_rust=no)
AS_IF([test "$enable_rust" != no], [
  AC_PATH_PROG([CARGO], [cargo])
  AS_IF([test -z "$CARGO"], [
    AS_IF([test "$enable_rust" = yes], [
      AC_MSG_ERROR([cargo is required for --enable-rust])
    ])
    enable_rust=no
  ])
  enable_rust=yes
])
AM_CONDITIONAL(ENABLE_RUST, test "$enable_rust" != no)

AC_ARG_ENABLE(installed_tests,
              AS_HELP_STRING([--enable-installed-tests],
                             [Install test programs (default: no)]),,
              [enable_installed_tests=no])
AM_CONDITIONAL(BUILDOPT_INSTALL_TESTS, test x$enable_installed_tests = xyes)

AC_ARG_WITH(default_editor,
              [AS_HELP_STRING([--with-default-editor=""],
                              [Default editor to use when the EDITOR environment variable is not set [default=vi]])],
              default_editor="$withval",
              default_editor="vi")
AC_SUBST([default_editor])

AC_CONFIG_FILES([
        Makefile
])
AC_OUTPUT

echo "
    $PACKAGE $VERSION

    man pages (xsltproc):                    $enable_man
    installed tests:                         $enable_installed_tests
    Rust implementation:                     $enable_rust
    Default editor:                          $default_editor
"
