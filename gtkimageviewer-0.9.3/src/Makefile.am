lib_LTLIBRARIES = libgtkimageviewer-2.0.la

libgtkimageviewer_2_0_la_SOURCES = gtk-image-viewer-fixed.c

libgtkimageviewer_2_0_la_inc_HEADERS = gtk-image-viewer.h gtk-image-viewer-private.h

libgtkimageviewer_2_0_la_incdir = $(includedir)/gtkimageviewer-2.0
libgtkimageviewer_2_0_la_LIBADD = 

# set the include path found by configure
INCLUDES= $(all_includes)

AM_CPPFLAGS = \
	$(GTK_CFLAGS) 

EXTRA_DIST = gtk-image-viewer.gob SConscript

# How do I make this happen at autoconf/automake time and not at compile time?
%.gob.stamp: %.gob
	gob2 $<
	@touch $@ 
