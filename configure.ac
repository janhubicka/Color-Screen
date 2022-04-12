AC_PREREQ([2.61])
AC_INIT([colorscreen], [0.1], [hubicka@ucw.cz])
AM_INIT_AUTOMAKE
LT_INIT

AC_PROG_MAKE_SET
AC_PROG_CXX
AC_PROG_CC
AC_PROG_RANLIB
AC_LANG(C++)
PKG_PROG_PKG_CONFIG
CXXFLAGS="$CXXFLAGS -Ofast"
AC_OPENMP
CXXFLAGS="$CXXFLAGS $OPENMP_CXXFLAGS"
LDFLAGS="$LDFLAGS $OPENMP_CXXFLAGS"

LDFLAGS="-lm $LDFLAGS"

AC_ARG_WITH(libtiff,
 [  --with-libtiff=PATH      enter the PATH to libtiff package],
 [ if test "$withval" != "no"; then
    LDFLAGS="-L$withval/lib $LDFLAGS";\
   fi ])
if test -n "$with_libtiff"; then echo "option: with_libtiff $with_libtiff"; fi

if test "$with_libtiff" != "no"; then
AC_SEARCH_LIBS(TIFFOpen,[tiff],[check_libtiff_h="tiffio.h"],
 [ echo " * * * try option --with-libtiff=PATH"])
fi

AC_ARG_WITH(libjpeg,
 [  --with-libjpeg=PATH      enter the PATH to libjpeg package],
 [ if test "$withval" != "no"; then
    LDFLAGS="-L$withval/lib $LDFLAGS";\
   fi ])
if test -n "$with_libjpeg"; then echo "option: with_libjpeg $with_libjpeg"; fi

if test "$with_libjpeg" != "no"; then
AC_SEARCH_LIBS(tjInitDecompress,[turbojpeg],[check_libjpeg_h="turbojpeg.h"],
 [ echo " * * * try option --with-libjpeg=PATH"])
fi

do_gtkgui=no
AC_ARG_ENABLE(gtkgui,[--enable-gtkgui     enable gtk2 based gui interface],do_gtkgui=yes,do_gtkgui=no)
if test "$do_gtkgui" != "no"; then
  AC_CONFIG_HEADERS([src/gtkgui/config.h])
  PKG_CHECK_MODULES([BARVENI_BIN], [
    glib-2.0
    gtk+-2.0
  ])
fi
AM_CONDITIONAL([gtkgui], [test "$do_gtkgui" != "no"])
AC_CONFIG_FILES([Makefile src/Makefile src/libcolorscreen/Makefile src/gtkgui/Makefile src/colorscreen/Makefile examples/Makefile examples/amcolony/Makefile])

AC_OUTPUT
