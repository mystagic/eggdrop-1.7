dnl configure.ac: this file is processed by autoconf to produce ./configure.
dnl
dnl $Id: configure.ac,v 1.4 2006/11/20 13:53:38 tothwolf Exp $

AC_PREREQ(2.59)

sinclude(../eggmod.m4)

AC_INIT([Eggdrop DNS Module],[1.7.0],[bugs@eggheads.org])

AC_CONFIG_SRCDIR(coredns.c)
AC_CONFIG_AUX_DIR(../../../misc)

AC_COPYRIGHT([Copyright (C) 1999 - 2006 Eggheads Development Team])
AC_REVISION($Revision: 1.4 $)

dns_reslib_avail="true"

AC_CHECK_FUNC(res_init, [],
[
  AC_CHECK_LIB(resolv, res_init,
  [
    RESLIB="-lresolv"
  ], [
    AC_CHECK_LIB(bind, res_init,
    [
      RESLIB="-lbind"
    ], [
      dns_reslib_avail="false"
    ])
  ])
])

if test "${dns_reslib_avail}" = "false"; then
  dns_reslib_avail="true"

  AC_CHECK_FUNC(__res_init, [],
  [
    AC_CHECK_LIB(resolv, __res_init,
    [
      RESLIB="-lresolv"
    ], [
      AC_CHECK_LIB(bind, __res_init,
      [
        RESLIB="-lbind"
      ], [
        dns_reslib_avail="false"
      ])
    ])
  ])
fi

AC_CHECK_FUNC(res_mkquery, [],
[
  AC_CHECK_LIB(resolv, res_mkquery,
  [
    if test "x${RESLIB}" != "x-lresolv"; then
      RESLIB="${RESLIB} -lresolv"
    fi
  ], [
    AC_CHECK_LIB(bind, res_mkquery,
    [
      if test "x${RESLIB}" != "x-lbind"; then
        RESLIB="${RESLIB} -lbind"
      fi
    ], [
      dns_reslib_avail="false"
    ])
  ])
])

if test "${dns_reslib_avail}" = "false"; then
  dns_reslib_avail="true"

  AC_CHECK_FUNC(__res_mkquery, [],
  [
    AC_CHECK_LIB(resolv, __res_mkquery,
    [
      if test "x${RESLIB}" != "x-lresolv"; then
        RESLIB="${RESLIB} -lresolv"
      fi
    ], [
      AC_CHECK_LIB(bind, __res_mkquery,
      [
        if test "x${RESLIB}" != "x-lbind"; then
          RESLIB="${RESLIB} -lbind"
        fi
      ], [
        dns_reslib_avail="false";
      ])
    ])
  ])
fi

# Cygwin checks.
AC_CANONICAL_HOST
if test "${dns_reslib_avail}" = "false"; then
  case $host_os in
    *cygwin* )
      AC_MSG_CHECKING([for /usr/local/bind/lib/libbind.a])
      if test -r /usr/local/bind/lib/libbind.a; then
        AC_MSG_RESULT([yes])
        RESLIB="${RESLIB} -L/usr/local/bind/lib -lbind"
        RESINCLUDE="-I/usr/local/bind/include"
        dns_reslib_avail="true"
      else
        AC_MSG_RESULT([no])
      fi
    ;;
  esac
fi

if test "${dns_reslib_avail}" = "false"; then
  cat >&2 <<EOF
configure: warning:

  Your system provides no functional resolver library. The DNS
  module will therefore be disabled.

EOF
  EGG_REMOVE_MOD(dns)
fi

AC_SUBST(RESLIB)
AC_SUBST(RESINCLUDE)

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
