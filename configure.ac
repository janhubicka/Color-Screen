AC_PREREQ([2.61])
AC_INIT([colorscreen], [0.1], [hubicka@ucw.cz])
AM_INIT_AUTOMAKE
LT_INIT(win32-dll)

AC_PROG_MAKE_SET
AC_PROG_CXX
AC_PROG_CC
AC_PROG_RANLIB
AM_MAINTAINER_MODE([disable])
AC_LANG(C++)
PKG_PROG_PKG_CONFIG
CXXFLAGS="$CXXFLAGS -Wall -Ofast  -march=native -std=c++11 -DLIBCOLORSCREEN"
#AX_CHECK_COMPILE_FLAG([-mfp16-format])
AC_OPENMP
CXXFLAGS="$CXXFLAGS $OPENMP_CXXFLAGS"
LDFLAGS="$LDFLAGS $OPENMP_CXXFLAGS"

AC_SEARCH_LIBS([sqrt], [m])
AC_CHECK_LIB([m],[cos])
AC_CHECK_LIB([gslcblas],[cblas_dgemm])
AC_CHECK_LIB([gsl],[gsl_blas_dgemm])


# The following may be useful if we put javaglue to the library.
# TODO add necessary ' so JAVA_HOME can contain spaces.  It does no Windows machines


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

AC_ARG_WITH(libraw,
 [  --with-libraw=PATH      enter the PATH to libraw package],
 [ if test "$withval" != "no"; then
    LDFLAGS="-L$withval/lib $LDFLAGS";\
   fi ])
if test -n "$with_libraw"; then echo "option: with_libraw $with_libraw"; fi

if test "$with_libraw" != "no"; then
AC_SEARCH_LIBS(libraw_init,[raw],[check_libraw_h="libraw.h"],
 [ echo " * * * try option --with-libraw=PATH"])
fi

AC_ARG_WITH(libzip,
 [  --with-libzip=PATH      enter the PATH to libzip package],
 [ if test "$withval" != "no"; then
    LDFLAGS="-L$withval/lib $LDFLAGS";\
   fi ])
if test -n "$with_libzip"; then echo "option: with_libzip $with_libzip"; fi

if test "$with_libzip" != "no"; then
AC_SEARCH_LIBS(zip_open,[zip],[check_libzip_h="zip.h"],
 [ echo " * * * try option --with-libzip=PATH"])
fi

AC_ARG_WITH(liblcms2,
 [  --with-liblcms2=PATH      enter the PATH to liblcms2 package],
 [ if test "$withval" != "no"; then
    LDFLAGS="-L$withval/lib $LDFLAGS";\
   fi ])
if test -n "$with_liblcms2"; then echo "option: with_liblcms2 $with_liblcms2"; fi

if test "$with_liblcms2" != "no"; then
AC_SEARCH_LIBS(cmsCreateRGBProfile,[lcms2],[check_liblcms2_h="lcms2.h"],
 [ echo " * * * try option --with-liblcms2=PATH"])
fi

# Windows DLL require -no-undefined libtool argument.  Using it on other
# systems makes using of libcolorscreen more complicated since -lcolorscreen
# must then be done together with libraries colorscreen depends on (libtiff and
# jpeg-turbo).  Use -no-undefined on Windows only.
AC_MSG_CHECKING([for Win32])
case "$host" in
     *-*-mingw*)
       WIN_NO_UNDEFINED="-no-undefined"
       ;;
     *-*-cygwin*)
       WIN_NO_UNDEFINED="-no-undefined"
       ;;
     *)
       WIN_NO_UNDEFINED=""
       ;;
esac
AC_SUBST([WIN_NO_UNDEFINED])


# Needed only for gtkgui
AC_DEFUN([AC_LINK_EXPORT_DYNAMIC],
[AC_CACHE_CHECK([for compiler -rdynamic option],
                [RDYNAMIC],
[ AC_LANG_PUSH([C])
  oldcflags="$CFLAGS"
  CFLAGS="$CFLAGS -Wl,-export-dynamic"
  CFLAGS="$CFLAGS -rdynamic"
  AC_LINK_IFELSE([AC_LANG_PROGRAM([[]],[[]])],
    [RDYNAMIC="-rdynamic"],[RDYNAMIC=""])
  CFLAGS="$oldcflags"
  AC_LANG_POP([C])
])
])
AC_SUBST([RDYNAMIC])

AC_LINK_EXPORT_DYNAMIC


do_gtkgui=no
AC_ARG_ENABLE(gtkgui,[--enable-gtkgui     enable gtk2 based gui interface],do_gtkgui=yes,do_gtkgui=no)
if test "$do_gtkgui" != "no"; then
  PKG_CHECK_MODULES([GTKGUI_BIN], [
    glib-2.0
    gtk+-2.0
  ])
fi

do_render_extra=no
AC_ARG_ENABLE(render-extra,[--enable-render-extra     enable additional render],do_render_extra=yes,do_render_extra=no)
if test "$do_render_extra" != "no"
then
  AC_DEFINE([RENDER_EXTRA], 1, "include render extra from extenral source tree")
fi

AM_CONDITIONAL([gtkgui], [test "$do_gtkgui" != "no"])
AM_CONDITIONAL([RENDER_EXTRA], [test "$do_render_extra" != "no"])
AC_CONFIG_FILES([Makefile src/Makefile src/libcolorscreen/Makefile src/gtkgui/Makefile src/colorscreen/Makefile examples/Makefile examples/amcolony/Makefile])
AC_CONFIG_HEADERS([src/libcolorscreen/config.h src/gtkgui/config.h])

AC_OUTPUT
