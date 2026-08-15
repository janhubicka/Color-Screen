# Screen-patch denoising review

This document describes the denoising code in `denoise.h` and `denoise.C`,
how it is used by the interpolated screen renderer, the correctness fixes made
during the review, and the intended direction for a replacement algorithm.

## Where denoising happens

The active denoising stage is not a final-image filter.  `render_interpolate`
first collects one value for every colour-screen sample in `analyze_base` and
then applies `screen_denoise` to those collected sample arrays.  Interpolation
or screen demosaicing happens afterwards.

For `precise_rgb` collection each red, green and blue screen sample is itself
an RGB scanner measurement.  The current implementation denoises the three
components of each such array independently.

There is also a `render_parameters::denoise` member intended for output-image
denoising.  No active renderer path currently applies it.  The post-demosaic
calls in `demosaic.h` are disabled with `#if 0`.  The two stages should remain
separate: patch-domain denoising can exploit screen geometry and collection
confidence, while ordinary output-image denoising has different goals and
noise statistics.

## Algorithms in the current implementation

`bilateral` is a conventional scalar bilateral filter.  Its spatial kernel is
truncated at three standard deviations.

`nl_means` is the direct/reference implementation of non-local means.  For
every output sample it compares a square patch with every candidate patch in
a square search window.

`nl_fast` computes the same patch squared-error using an integral image for
each search displacement.  After this review both NLM implementations divide
the patch squared-error by patch area before applying `strength`, so the same
parameters have the same meaning in both modes.

The filters operate on tiles for parallelism and bounded scratch storage.
Every tile has a border large enough for the selected filter support.

## Correctness problems found and fixed

### Mutable tile borders

All real `analyze_base` call sites denoise in place: the getter and setter
refer to the same sample array.  The old tile wrappers loaded each tile border
from that live array after earlier tiles might already have overwritten it.
Parallel execution therefore had a data race, and sequential output depended
on tile order.  Neighbouring tiles could see different mixtures of noisy and
denoised input.

The wrappers now snapshot the complete input before any tile writes output.
All tile borders are populated from that immutable snapshot.  A regression
compares in-place and separate-output operation on a multi-tile image.

### Reference and fast NLM used different strength scales

`nl_fast` normalized patch SSD by patch area while `nl_means` did not.  With
the default patch radius of one this changes the exponent by a factor of nine;
the disagreement becomes still larger for bigger patches.  The two menu
choices therefore implemented substantially different filters.

The reference implementation now uses mean squared patch distance too.
Synthetic tests verify close agreement between the two implementations.

### Asymmetric image reflection

The old left/top image extension mapped `-1` to `+1`, while the right/bottom
extension repeated the edge sample.  It also performed only one reflection
and then clamped, so a filter border wider than a small image behaved
inconsistently.

A common repeated symmetric-reflection helper is now used by all scalar and
RGB wrappers.  Horizontal flip symmetry is tested explicitly.

### Bilateral border size

The bilateral kernel uses a radius of `ceil(3*sigma_s)`.  Earlier code could
allocate a border derived from the NLM radii instead, causing an out-of-bounds
read for ordinary bilateral parameters.  The support calculation is now
shared between allocation and filtering.  This was independently exposed by
the AMD64 sanitizer build before this review.

### Precise-RGB screen-cell averaging

`screen_tile_rgb_color()` interchanged width and height scale factors while
iterating the per-channel sample lattices.  This is wrong for non-square
lattices such as Dufay red samples and Paget red/green samples, and can read
the wrong samples.  The loops now use height scale vertically and width scale
horizontally.  A Dufay regression checks the expected two-red-sample average.

### Parameters were not persistent

The Qt controls edit `render_parameters::screen_denoise`, but those values
were not saved to or restored from the project parameter file.  The selected
filter consequently disappeared on reload.  The screen-patch denoising mode
and all its parameters are now serialized.  Older files remain valid because
missing fields keep their defaults.

### Invalid parameter handling

The filter entry points now reject invalid effective configurations such as
non-finite strengths/sigmas, negative patch radii, or an empty NLM search
radius.  Selecting an NLM mode now has a useful default strength while the
default mode remains `none`, so existing projects are unaffected.

### Cache invalidation

Renderer analysis caches now compare effective denoising parameters.  Changes
to parameters inactive for the selected mode no longer force an otherwise
identical expensive analysis to be rebuilt.

## Why the corrected generic filters are still not the desired solution

The collected arrays are not ordinary uniformly sampled photographs.

### Channel lattice geometry differs

For example, Dufay red samples have twice the horizontal sampling density of
green and blue.  Paget has different anisotropic lattices again.  A patch or
search radius expressed simply in array indices therefore represents a
different physical neighbourhood for different colours and processes.

A replacement should express offsets in common screen coordinates and derive
the actual per-channel neighbours from the screen geometry.

### Precise-RGB filtering should use common vector weights

In `precise_rgb`, one screen sample is an RGB scanner vector.  Running three
independent scalar NLM filters lets red, green and blue components choose
different neighbours and can change chromaticity.  Similarity should instead
be computed from a common guide or RGB-vector patch, and one weight should be
applied to all components of the sample.

### Collection confidence is currently discarded

The analyzer accumulates each screen sample with a collection weight and only
later divides by that weight.  A sample assembled from many strong source
pixels is therefore treated identically by denoising to a weak or even
fallback-filled sample.  The collection weights, and ideally local variance
or sample count, should be retained as measurement confidence.

### `strength` is not tied to a noise model

The current NLM `strength` is an absolute value in normalized sample units.
There is no estimate of scanner/film noise, no correction for the noise term
present in patch SSD, and no allowance for signal-dependent noise.  This
makes useful settings image- and scan-dependent.

## Recommended replacement: guided screen-lattice NLM

The natural next experiment is a patch-domain guided NLM rather than a generic
image denoiser.

1. Represent every collected sample by its position in common screen
   coordinates.
2. Retain collection confidence and, where possible, an estimate of sample
   variance.
3. Build a guide used only for neighbour similarity.  For RGB+IR scans the
   registered IR/monochrome layer is an attractive guide because it describes
   scene structure without screen colour.  Without IR, use a common robust
   luminance or RGB-vector guide.
4. Compare guide patches in screen coordinates and normalize distances by
   expected measurement variance.
5. Calculate one neighbour weight and apply it to all components belonging to
   the screen sample.  This preserves scanner RGB chromaticity in
   `precise_rgb` mode.
6. Treat low-confidence or synthesized/missing samples differently from
   directly measured samples rather than hiding that distinction before
   denoising.

This design is still local enough to use the existing analyzer grids and can
be implemented incrementally.  The current integral-image `nl_fast` remains a
useful scalar reference, but its square-array acceleration does not directly
carry over to arbitrary screen geometry.

## Validation strategy

The previous test used only a smooth gradient plus one-sided noise and checked
whether MSE decreased.  That can reward excessive blur and cannot expose tile
or colour errors.

A useful validation set should contain:

- zero-mean synthetic noise on smooth regions;
- sharp steps, fine texture, and repeated texture;
- Dufay and Paget anisotropic sample lattices;
- multi-tile images to expose seams and races;
- `precise_rgb` vectors with fixed chromaticity;
- deliberately heterogeneous sample confidence;
- synthetic RGB+IR data for guided filtering;
- real crop comparisons evaluated both before and after screen demosaicing.

Metrics should include noise reduction, edge/texture bias, chromaticity error,
tile invariance, fast/reference agreement where applicable, and final
reconstruction error.  A denoiser should not be accepted solely because it
reduces sample-domain MSE.
