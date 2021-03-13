AC_CANONICAL_TARGET
AC_CANONICAL_HOST
SAVED_CFLAGS="$CFLAGS"
AC_PROG_CC
CFLAGS="$SAVED_CFLAGS"
AC_PROG_CC_C_O
AC_PROG_CPP
SAVED_CXXFLAGS="$CXXFLAGS"
AC_PROG_CXX
AC_PROG_CXXCPP
CXXFLAGS="$SAVED_CXXFLAGS"
AC_PROG_MAKE_SET
AC_PROG_INSTALL
AC_CHECK_TOOL(RANLIB, ranlib, :)
AC_CHECK_TOOL(STRIP, strip, :)
AC_CHECK_TOOL(AR, ar, :)
AC_CHECK_TOOL(LD, ld, :)
AC_CHECK_TOOL(STRINGS, strings, :)
AC_CHECK_TOOL(OBJDUMP, objdump, :)
PKG_PROG_PKG_CONFIG

dnl Setting ARFLAGS
ARFLAGS=crS
AC_SUBST([ARFLAGS])
AC_ARG_VAR([ARFLAGS], [AR options [default: crS]])

dnl Setting RANLIB_FLAGS
case $host in
  *darwin*)
    RANLIB_FLAGS="-no_warning_for_no_symbols";;
  *)
    RANLIB_FLAGS="";;
esac
AC_SUBST([RANLIB_FLAGS])
AC_ARG_VAR([RANLIB_FLAGS], [RANLIB options [default: empty on linux, -no_warning_for_no_symbols on darwin]])

dnl Check for headers
AC_HEADER_STDC()
AC_CHECK_HEADERS([inttypes.h stdint.h sys/types.h sys/syscall.h stropts.h])
AC_CHECK_FUNCS([vsscanf syscall],,)