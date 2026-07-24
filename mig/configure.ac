dnl configure script for GNU MIG.

AC_PREREQ([2.53])

AC_INIT([GNU MIG], [1.8], [bug-hurd@gnu.org])
AC_CONFIG_SRCDIR([migcom.c])
AC_CONFIG_AUX_DIR([build-aux])

AC_CANONICAL_HOST
AC_CANONICAL_TARGET

AC_ARG_PROGRAM

AM_INIT_AUTOMAKE(
  [gnu]
  [dist-xz]
dnl Don't define `PACKAGE' and `VERSION'.
  [no-define]
)

m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([no])],
                            [AC_SUBST([AM_DEFAULT_VERBOSITY], [1])])

dnl Checks for programs.
AC_PROG_CC
AC_PROG_CPP
AC_PROG_YACC

yacc_is_bison=`$YACC --version | head -n 1 | cut '-d ' -f1`
[if [ x"$yacc_is_bison" != x"bison" ]; then ]
	AC_MSG_ERROR([yacc is not provided by bison, check your alternative conf or your packages])
[fi]

AM_PROG_LEX
AC_PROG_AWK
AC_PROG_INSTALL

dnl Checks for header files.
AC_HEADER_STDC

[if [ "$target" = "$host" ]; then
  TARGET_CC='${CC}'
else]
  AC_CHECK_PROG([TARGET_CC], [$target_alias-gcc], [$target_alias-gcc], [none])
  [if [ x"$TARGET_CC" = xnone ]; then]
    AC_MSG_ERROR([could not find a compiler for the target you requested])
  [fi
fi]
dnl These two are just for the user's convenience.
AC_SUBST([TARGET_CPPFLAGS])
AC_SUBST([TARGET_CFLAGS])

[MIGCOM=`echo migcom | sed "$program_transform_name"`]
AC_SUBST([MIGCOM])
[MIG=`echo mig | sed "$program_transform_name"`]
AC_SUBST([MIG])

AC_OUTPUT([Makefile mig tests/Makefile \
           tests/good/Makefile \
           tests/generate-only/Makefile \
           tests/bad/Makefile])
