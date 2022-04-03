AC_PREREQ([2.61])
AC_INIT([colorscreen], [0.1], [hubicka@ucw.cz])
AM_INIT_AUTOMAKE

AC_PROG_MAKE_SET
AC_PROG_CXX
AC_PROG_CC
AC_PROG_RANLIB
AC_LANG(C++)
PKG_PROG_PKG_CONFIG
PKG_CHECK_MODULES([BARVENI_BIN], [
    glib-2.0
    gtk+-2.0
])
AC_ARG_WITH(netpbm,
 [  --with-netpbm=PATH      enter the PATH to netpbm package],
 [ if test "$withval" != "no"; then
    LDFLAGS="-L$withval/lib $LDFLAGS";\
   fi ])
if test -n "$with_netpbm"; then echo "option: with_netpbm $with_netpbm"; fi

dnl Checks for libraries.
if test "$with_netpbm" != "no"; then
# netpbm-10.26 + SuSE-10.0: netpbm needs mathlib -lm
# netpbm-10    : libnetpbm + pam.h (+ p[bgpn]m.h, libp[bgpn]m.so as links)
# netpbm-9     : libpnm    + pnm.h (+ p[bgp]m.h)
# pnm_readpaminit (netpbm-10)
# pnm_readpnminit (netpbm-9,10)
# ToDo: how to check that -lm is needed?
LDFLAGS="-lm $LDFLAGS"
AC_SEARCH_LIBS(pnm_readpnminit,[netpbm pnm],[check_netpbm_h="pam.h pnm.h"],
 [ echo " * * * try option --with-netpbm=PATH"])
fi
#AC_SEARCH_LIBS(gtk_image_viewer_redraw,[gtk_image_viewer],[check_image_viewer_h="gtkimageviewer-2.0/gtk-image-viewer.h"],
 #[ echo "gtkimageviewer needed"])
AC_CONFIG_HEADERS([src/gtkgui/config.h])
#AC_DEFINE_UNQUOTED([DATADIR], ["$datadir"], [Data directory.])

CXXFLAGS="-Ofast"

AC_CONFIG_MACRO_DIR([m4])

AC_CONFIG_FILES([Makefile src/Makefile src/libcolorscreen/Makefile src/gtkgui/Makefile])

AC_OUTPUT
