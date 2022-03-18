SUBDIRS = src docs

EXTRA_DIST = examples/Makefile          \
	     examples/test_img.ppm      \
             examples/lena.pgm          \
	     examples/test_file.c	\
	     examples/test-track.c	\
	     examples/test_scroll.c	\
	     examples/circle.c	        \
	     examples/cairo-circles.c   \
	     examples/agg-circles.cc    \
	     examples/agg-lion.cc    \
	     examples/mandelbrot.c      \
             examples/SConscript        \
             SConstruct

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA= gtkimageviewer-2.0.pc 

uninstall-local:
	rm -f $(DESTDIR)$(pkgconfigdir)/gtkimageviewer-2.0.pc

snapshot:
	$(MAKE) dist distdir=$(PACKAGE)-snap`date +"%Y%m%d"`


