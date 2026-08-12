# Phase One real-MTF reproducibility fixture

The `ON_558_001_004_ISA-*` TIFF files are cropped regions from one
high-quality Phase One capture of a resolution/solid-colour test target.  They
exercise the real slanted-edge measurement and physical MTF fitter rather than
a synthetic PSF.

Capture metadata used by the regression:

- scan resolution: 2089 PPI;
- marked aperture: f/8;
- wavelength: 750 nm infrared;
- sensor pixel pitch: 3.760 micrometres;
- sensor fill factor: 1 in the current first-cut physical model.

There are horizontal and vertical edge crops at five field positions:
`bottomleft`, `bottomright`, `center`, `topleft`, and `topright`.  The
`circles` crop contains several curved solid-colour transitions and is a
negative control: it must not be accepted as one slanted edge.

`test_real_mtf_reproducibility()` in `src/libcolorscreen/unittests.C` measures
each edge with the normal slanted-edge code, including its per-frequency
uncertainty estimate, and independently fits the physical diffraction model.
The fixed capture metadata above is not optimized.  Residual Gaussian sigma,
defocus, halo fraction, and halo radius are free.

The test deliberately checks reproducibility statistics instead of exact
parameter values.  Different field positions and edge orientations contain
real optical/target variation, and future improvements to the halo model may
move the absolute fit.  The regression instead requires that all ten ROIs are
qualified, that the fitted values stay in one physically reasonable basin,
that horizontal/vertical pairs broadly agree, and that very different solver
initial guesses converge to the same solution on a representative edge.

Two vertical crops (`bottomleftv` and `centerv`) also guard a subtle ROI
qualification bug: a distant plateau artifact can have a larger local ESF
derivative than the geometrically fitted edge.  Validation must search for the
LSF peak near the already fitted edge position rather than replacing the edge
with an unrelated distant derivative peak.
