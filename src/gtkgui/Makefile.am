dist_pkgdata_DATA = gtkgui.glade

AM_CFLAGS = $(GTKGUI_BIN_CFLAGS) -Wno-implicit-function-declaration
AM_CXXFLAGS = $(GTKGUI_BIN_CFLAGS)  -D DATADIR='"$(datadir)"' $(RDYNAMIC)
bin_PROGRAMS=colorscreen-gtk
if static_link
  AM_LDFLAGS = -static
  AM_CXXFLAGS += -DLIBCOLORSCREEN -static
endif
LDADD = ../libcolorscreen/libcolorscreen.la $(GTKGUI_BIN_LIBS)
EXTRA_DIST = gtk-image-viewer.h gtk-image-viewer.gob gtk-image-viewer-private.h regenerate.sh viewer-patch
colorscreen_gtk_SOURCES=gtkgui.C gtk-image-viewer.c
