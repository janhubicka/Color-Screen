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
			  slanted-austraia-hurley-lsf.txt \
			  ON_558_001_004_ISA-bottomlefth.tif \
			  ON_558_001_004_ISA-bottomleftv.tif \
			  ON_558_001_004_ISA-bottomrighth.tif \
			  ON_558_001_004_ISA-bottomrightv.tif \
			  ON_558_001_004_ISA-centerh.tif \
			  ON_558_001_004_ISA-centerv.tif \
			  ON_558_001_004_ISA-circles.tif \
			  ON_558_001_004_ISA-toplefth.tif \
			  ON_558_001_004_ISA-topleftv.tif \
			  ON_558_001_004_ISA-toprighth.tif \
			  ON_558_001_004_ISA-toprightv.tif \
			  ON_558_001_004_ISA-README.md \
			  nikon-razor-rgbi-README.md \
			  nikon-razor-sharp-rgbi.tif \
			  nikon-razor-defocus-rgbi.tif \
			  nikon-rgbi-mtf-test.C \
			  slanted-gaussian-vsharp.tif \
			  slanted-gaussian-vsharp-mtf.txt \
			  slanted-gaussian-sharp.tif \
			  slanted-gaussian-sharp-mtf.txt \
			  slanted-gaussian-medium.tif \
			  slanted-gaussian-medium-mtf.txt \
			  slanted-gaussian-soft.tif \
			  slanted-gaussian-soft-mtf.txt \
			  generate-slanted-edge-tests.sh \
			  validate-mtf-physical-model.py

TEST_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
			  $(top_srcdir)/build-aux/tap-driver.sh

TESTS_ENVIRONMENT       = top_builddir=$(top_builddir) top_srcdir=$(top_srcdir) \
                          EXEEXT='$(EXEEXT)' CXX='$(CXX)' \
                          CPPFLAGS='$(CPPFLAGS)' CXXFLAGS='$(CXXFLAGS)' \
                          LDFLAGS='$(LDFLAGS)' LIBS='$(LIBS)'

# Files produced directly by tests rather than by the Automake test driver.
# Declare them so clean/distclean and distcleancheck leave the build tree clean.
CLEANFILES              = slanted-edge-1.log \
                          slanted-edge-2.log \
                          slanted-edge-3.log \
                          slanted-edge-4.log \
                          slanted-edge-5.log \
                          slanted-edge-6.log \
                          slanted-edge-invalid-mtf.txt \
                          paget_ha_test.tiff \
                          paget_ahd_test.tiff \
                          paget_amaze_test.tiff \
                          paget_rcd_test.tiff \
                          paget_lmmse_test.tiff \
                          dufay_rcd_test.tiff

TESTS                   = unittests.test \
			  dufaycolor_nikon_coolsan9000ED_4000DPI_raw.test  \
			  dufaycolor_nikon_coolsan9000ED_4000DPI_raw-finetune.test \
			  dufaycolor_dt_captureone_export.test \
			  render-original.test \
			  slanted-edge.test \
			  nikon-rgbi-mtf.test

distclean-local:
	-rm -rf ./out-* ./out2-*
