# Epson V850 defocused razor-edge regression

`epson-v850-3200dpi-defocused-edge.tif` is a 48x48 lossless crop of the green
scanner channel from an Epson V850 scan at 3200 DPI.  The scanner was focused
on the film-holder plane while the razor blade was placed directly on the glass
bed, deliberately producing a seriously defocused edge.

The crop corresponds to source-image coordinates `x=1376..1423`,
`y=665..712`.  It is stored as single-channel 16-bit grayscale, preserving the
native green scanner samples from the 16-bit four-sample source TIFF.  The crop
also retains 3200-DPI resolution metadata.

Current Color-Screen measurement is approximately:

- edge angle: 6.88 degrees;
- line-fit RMS: 0.089 pixel;
- phase coverage: 99%;
- MTF50: 0.059 cycles/pixel, about 7.4 lp/mm at 3200 DPI.

This fixture is a measurement/geometry regression.  Direct Wiener sharpening
with the measured curve currently produces visually strong ringing around the
edge; that behavior is documented in `doc/mtf-slanted-edge-robustness.md`
rather than baked into this MTF acceptance test.
