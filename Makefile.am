SUBDIRS = src examples testsuite images
ACLOCAL_AMFLAGS = -I m4
EXTRA_DIST = os/windows/installer.nsi os/windows/deploy_dlls.sh os/windows/smoke-test-screenshot.ps1 os/linux/control os/linux/package_deb.sh doc/qtgui-internal-cleanup.md doc/qtgui-workflow-roadmap.md doc/mtf-fitting-workflow.md

.PHONY: examples doxygen

examples: src/colorscreen/colorscreen 
	cd examples/amcolony ; $(MAKE) $(AM_MAKEFLAGS) examples

doxygen: Doxyfile
	doxygen Doxyfile

clean-local:
	rm -rf doc/html
