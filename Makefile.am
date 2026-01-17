SUBDIRS = src examples testsuite images
ACLOCAL_AMFLAGS = -I m4

.PHONY: examples

examples: src/colorscreen/colorscreen 
	cd examples/amcolony ; $(MAKE) $(AM_MAKEFLAGS) examples
