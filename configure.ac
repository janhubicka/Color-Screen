AC_PREREQ([2.69])
AC_INIT([colorscreen], [1.1], [hubicka@ucw.cz])
AC_CONFIG_MACRO_DIRS([m4])
AC_CONFIG_AUX_DIR([build-aux])
AM_INIT_AUTOMAKE([-Wall subdir-objects])
AM_PROG_AR
LT_INIT(win32-dll)
AC_PROG_MAKE_SET
AC_PROG_CXX
AC_PROG_CC
AM_MAINTAINER_MODE([disable])
AC_REQUIRE_AUX_FILE([tap-driver.sh])
AC_LANG(C++)
PKG_PROG_PKG_CONFIG
#AX_CHECK_COMPILE_FLAG([-std=c++17])
#AX_CHECK_COMPILE_FLAG([-mfp16-format])
AX_CXX_COMPILE_STDCXX_17(ext,mandatory)
AC_OPENMP
CXXFLAGS="$CXXFLAGS $OPENMP_CXXFLAGS"
LDFLAGS="$LDFLAGS $OPENMP_CXXFLAGS"

# Check for malloc.h header and memalign function
AC_CHECK_HEADERS([malloc.h])
AC_CHECK_FUNCS([memalign])

AC_SEARCH_LIBS([sqrt], [m])
AC_CHECK_LIB([m],[cos])
AC_ARG_ENABLE([dependencies],AS_HELP_STRING([--disable-dependencies],
					    [do not search for libraries colorscreen depends on (a hack for MacOS static linking)]))
if test x"$enable_dependencies" != x"no"
then
AC_CHECK_LIB([gslcblas],[cblas_dgemm])
AC_CHECK_LIB([gsl],[gsl_blas_dgemm])


AC_ARG_WITH(libfftw3,
 [  --with-libfftw3=PATH      enter the PATH to fftw3 package],
 [ if test "$withval" != "no"; then
    LDFLAGS="-L$withval/lib $LDFLAGS";\
   fi ])
if test -n "$with_fftw3"; then echo "option: with_fftw3 $with_fftw3"; fi

if test "$with_fftw3" != "no"; then
AC_SEARCH_LIBS(fftw_plan_null_destroy,[fftw3],[check_fftw3_h="fftw3.h"],
 [ echo " * * * try option --with-fftw3=PATH"])
fi

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

AC_ARG_ENABLE([16bit-float],
	      AS_HELP_STRING([--disable-16bit-float],
			     [disable use of 16bit floats for temporary data]))
if test x"$enable_16bit_float" != x"no"
then
  AC_DEFINE([COLORSCREEN_16BIT_FLOAT], 1, "use 16bit floats for temporary data")
fi

AC_ARG_ENABLE([checking],
	      AS_HELP_STRING([--enable-checking],
			     [enable extra runtime checking]))
if test x"$enable_checking" == x"yes"
then
  AC_DEFINE([COLORSCREEN_CHECKING], 1, "enable additional checking")
fi

AC_ARG_ENABLE([gtkgui],AS_HELP_STRING([--enable-gtkgui],
				      [enable gtk2 based gui interface]))
if test x"$enable_gtkgui" == x"yes"; then
  PKG_CHECK_MODULES([GTKGUI_BIN], [
    glib-2.0
    gtk+-2.0
  ])
fi

AC_ARG_ENABLE([render-extra],AS_HELP_STRING([--enable-render-extra],
				      [enable additional render from external source tree]))
if test x"$enable_render_extra" == x"yes"
then
  AC_DEFINE([RENDER_EXTRA], 1, "include render extra from extenral source tree")
fi

AC_ARG_ENABLE([static-link],AS_HELP_STRING([--disable-static-link],
	      [do not link colorscreen statically into colorscreen and colorscreen-gtk binaries]))

AM_CONDITIONAL([static_link], [test "$enable_static_link" != "no"])
AM_CONDITIONAL([gtkgui], [test x"$enable_gtkgui" == x"yes"])
AM_CONDITIONAL([RENDER_EXTRA], [test x"$enable_render_extra" == x"yes"])

AC_ARG_ENABLE([qtgui],AS_HELP_STRING([--enable-qtgui],
				     [enable Qt6 based gui interface]))
if test x"$enable_qtgui" == x"yes"; then
  PKG_CHECK_MODULES([Qt6], [Qt6Core Qt6Gui Qt6Widgets Qt6Concurrent])
  
  # Search for moc in specific qt6 path first, then general names
  AC_PATH_PROGS(MOC, [moc-qt6 moc], [], [/usr/libexec/qt6:$PATH])
  
  if test -z "$MOC"; then
    AC_MSG_ERROR([moc not found. Please install Qt6 development tools.])
  fi
fi
AM_CONDITIONAL([qtgui], [test x"$enable_qtgui" == x"yes"])

AC_CONFIG_FILES([Makefile src/Makefile src/libcolorscreen/Makefile testsuite/Makefile src/gtkgui/Makefile src/qtgui/Makefile src/colorscreen/Makefile examples/Makefile examples/amcolony/Makefile images/Makefile])
AC_CONFIG_HEADERS([src/libcolorscreen/config.h src/gtkgui/config.h src/libcolorscreen/include/colorscreen-config.h])

AC_OUTPUT
