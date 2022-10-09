dist_pkgdata_DATA = gtkgui.glade

AM_CFLAGS = $(GTKGUI_BIN_CFLAGS) -Wno-implicit-function-declaration
AM_CXXFLAGS = $(GTKGUI_BIN_CFLAGS)  -D DATADIR='"$(datadir)"' $(RDYNAMIC)
bin_PROGRAMS=colorscreen-gtk
AM_LDFLAGS = -static
LDADD = ../libcolorscreen/libcolorscreen.la $(GTKGUI_BIN_LIBS)
colorscreen_gtk_SOURCES=gtkgui.C gtk-image-viewer.c
