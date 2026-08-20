# Nikon RGB+IR razor MTF fixtures

These are compact crops from a Nikon Super COOLSCAN LS-9000 ED focus sweep
provided for Color-Screen MTF testing.  The original scans were made with
VueScan 9.7.27 at 4000 DPI and exposure 0.8.  The full focus sweep is
intentionally not part of the repository.

Both fixtures are the same `48x96+634+100` crop around one clean part of the
slanted razor edge.  The crop excludes the visible blade nicks farther down the
original frame while retaining about 8 pixels of edge displacement from top
to bottom.  Keeping only the support needed by the slanted-edge solver makes
the real-data regression compact enough for the normal testsuite.

* `nikon-razor-sharp-rgbi.tif` comes from
  `nikon-4000DPI-expozice0.8-focus-0.05.tif`, near the sharp part of the focus
  sweep.
* `nikon-razor-defocus-rgbi.tif` comes from
  `nikon-4000DPI-expozice0.8-focus0.4.tif`, a deliberately defocused setting.

The crops retain all four 16-bit samples: red, green, blue, and the fourth
Nikon infrared sample.  They are stored with lossless Deflate compression and
4000-DPI resolution tags.  Color-Screen's TIFF loader maps the fourth sample of
a four-sample RGB TIFF to its grayscale/IR plane.

A separately exported `focus0-ir.tif` from the supplied data is not included:
it is pixel-identical to the fourth sample of the corresponding `focus0.tif`.
