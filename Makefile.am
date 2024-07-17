SUBDIRS = src examples
ACLOCAL_AMFLAGS = -I m4

.PHONY: examples

examples: src/colorscreen/colorscreen 
	cd examples/amcolony ; $(MAKE) $(AM_MAKEFLAGS) examples
