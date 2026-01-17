SUBDIRS = src examples testsuite images
ACLOCAL_AMFLAGS = -I m4
EXTRA_DIST = os/windows/installer.nsi

.PHONY: examples

examples: src/colorscreen/colorscreen 
	cd examples/amcolony ; $(MAKE) $(AM_MAKEFLAGS) examples
