AC_PREREQ([2.67])
AC_INIT([daq2spike2],[1.0.9],[dshuman@usf.edu])
AM_INIT_AUTOMAKE()
AC_CONFIG_SRCDIR(daq2spike2.cpp)
AC_CONFIG_HEADERS([config.h])
${CPPFLAGS=""}
${CXXFLAGS=""}
${CFLAGS=""}
AC_PROG_CXX
AC_PROG_F77
AC_HEADER_STDC
AC_PROG_LN_S
AC_CONFIG_MACRO_DIRS([m4])

AC_F77_LIBRARY_LDFLAGS

AC_CANONICAL_BUILD

PKG_PROG_PKG_CONFIG

AC_CHECK_PROGS([MXE_QMAKE],[x86_64-w64-mingw32.static-qmake-qt5])
if test -z "$MXE_QMAKE"; then
   AC_MSG_WARN([The MXE cross development environment is required to build the MS Windows version of usfsim packate (not fatal).  Consult the HOWTO_BUILD_SIM_FOR_WIN document included in this package.])
else
   AC_MSG_NOTICE([The MXE cross development environment is available.])
fi



AC_CONFIG_FILES([Makefile])
AC_OUTPUT
