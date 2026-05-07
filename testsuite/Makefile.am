EXTRA_DIST              = defs.sh $(TESTS) \
			  dufaycolor_nikon_coolsan9000ED_4000DPI_raw.tif \
			  dufaycolor_nikon_coolsan9000ED_4000DPI_raw-nomesh.par \
			  dufaycolor_nikon_coolsan9000ED_4000DPI_raw_sky-nomesh.par \
			  dufaycolor_nikon_coolsan9000ED_4000DPI_raw_sky.tif \
			  dufaycolor_dt_captureone_export_tile1.jpg \
			  dufaycolor_dt_captureone_export_tile2.jpg \
			  empty.par \
			  checkerboard.tif \
			  slanted-austraia-hurley.tif \
			  slanted-austraia-hurley-mtf.txt \
			  slanted-austraia-hurley-esf.txt \
			  slanted-austraia-hurley-lsf.txt

TEST_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
			  $(top_srcdir)/build-aux/tap-driver.sh

TESTS_ENVIRONMENT       = top_builddir=$(top_builddir) top_srcdir=$(top_srcdir)

TESTS                   = unittests.test \
			  dufaycolor_nikon_coolsan9000ED_4000DPI_raw.test  \
			  dufaycolor_nikon_coolsan9000ED_4000DPI_raw-finetune.test \
			  dufaycolor_dt_captureone_export.test \
			  render-original.test \
			  slanted-edge.test

distclean-local:
	-rm -rf ./out-* ./out2-*
