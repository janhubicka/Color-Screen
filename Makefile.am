SUBDIRS = src examples testsuite images
ACLOCAL_AMFLAGS = -I m4
EXTRA_DIST = os/windows/installer.nsi os/windows/deploy_dlls.sh os/windows/smoke-test-screenshot.ps1 os/linux/control os/linux/package_deb.sh

.PHONY: examples

examples: src/colorscreen/colorscreen 
	cd examples/amcolony ; $(MAKE) $(AM_MAKEFLAGS) examples
