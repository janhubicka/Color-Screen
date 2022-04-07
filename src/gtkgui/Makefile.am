bin_SCRIPTS=barveni barvenirgb
CLEANFILES = $(bin_SCRIPTS)
EXTRA_DIST = barveni.in barvenirgb.in
do_subst = sed -e 's,[@]bindir[@],$(bindir),g'
barveni: barveni.in Makefile
	$(do_subst) < $(srcdir)/barveni.in > barveni
	chmod +x barveni
barvenirgb: barvenirgb.in Makefile
	$(do_subst) < $(srcdir)/barvenirgb.in > barvenirgb
	chmod +x barvenirgb
dist_pkgdata_DATA = barveni.glade

AM_CFLAGS = $(BARVENI_BIN_CFLAGS) -Wno-implicit-function-declaration
AM_CXXFLAGS = $(BARVENI_BIN_CFLAGS)  -D DATADIR='"$(datadir)"' -rdynamic
bin_PROGRAMS=barveni-bin
AM_LDFLAGS = -static
LDADD = ../libcolorscreen/libcolorscreen.la $(BARVENI_BIN_LIBS)
barveni_bin_SOURCES=barveni.C gtk-image-viewer.c
