# Screen-patch denoising review

This document describes the denoising code in `denoise.h` and `denoise.C`,
how it is used by the interpolated screen renderer, the correctness fixes made
during the review, and the intended direction for a replacement algorithm.

## Where denoising happens

The reconstruction denoiser is not a final rendered-image filter.
`render_interpolate` first collects one value for every colour-screen sample
in `analyze_base` and can apply `screen_denoise` to those collected sample
arrays.  After materialized Paget/Dufay screen demosaicing, an independent
`demosaiced_denoise` stage can filter the complete colour field before it is
resampled and combined with the high-resolution B&W detail.  Either stage,
both, or neither may therefore be selected independently.

For `precise_rgb` collection each red, green and blue screen sample is itself
an RGB scanner measurement.  The current implementation denoises the three
components of each such array independently.

Ordinary final-image denoising after the reconstructed colour has been
combined with B&W detail remains a separate image-processing problem and is
not represented by either reconstruction-domain control.  It can be handled
by the ordinary rendering/post-processing pipeline later if needed.

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

The post-demosaic RGB implementation uses mean squared RGB distance for its
range and patch metrics.  Thus the numerical strength/sigma scale remains
comparable to one scalar channel, while one resulting neighbour weight is
used for all three components.

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

### Denoising parameters are persistent

Both reconstruction-domain parameter sets are serialized completely:
`screen_denoise` for collected samples before demosaicing and
`demosaiced_denoise` for the complete colour field afterwards.  Mode, NLM
strength/radii and bilateral sigmas are all round-tripped even when some are
inactive in the selected mode, so GUI state is preserved exactly.

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

### Collection confidence is now retained and used

The analyzer accumulates every precise/color screen sample together with a
collection weight describing how much real scanner data contributed to it.
Those weights are now retained in `analyze_base` instead of being discarded
after normalization and are consumed by the pre-demosaic bilateral/NLM
filters.

For NLM, a patch difference is weighted by the harmonic mean of the supports
of the two compared samples.  This is an inverse-variance-like rule if noise
variance is approximately inversely proportional to contributing scanner
area.  Candidate accumulation is additionally weighted by the candidate's
own support.  Bilateral filtering applies the same principle to its range
distance and output accumulation.

The normalization is intentionally anchored at support one: if all samples
have support one, confidence-aware filtering is exactly the historical
filter.  A sub-pixel sample has weaker range/patch authority and contributes
less to neighbours, while a zero-support fallback sample can be reconstructed
from reliable neighbouring measurements rather than acting as an observation
itself.  The raw support arrays are kept unchanged after denoising; we do not
yet invent a propagated statistical confidence for filtered values.

Fast collection has no area/support estimate and therefore continues to use
the unweighted filter.

### Pre- and post-demosaic denoising are distinct stages

The stages now have independent parameter blocks and GUI sections.  The
pre-demosaic stage operates on measured screen-element colours and uses
collection support when available.  The post-demosaic stage operates on the
regular complete colour field and computes one RGB-vector similarity weight
which is applied to all three channels, avoiding channel-dependent neighbour
choices and incidental chromaticity shifts.

The post stage currently applies to the materialized advanced Paget/Dufay
demosaicing path.  Simpler on-demand nearest/linear/bicubic interpolation and
vertical-strip renderers do not yet materialize a complete colour field and
therefore leave the post-demosaic controls disabled.

Denoising the final rendered image after B&W detail has been recombined is a
separate ordinary image-processing operation and need not share these
controls.

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
2. Use the retained collection support and, where possible, extend it with an
   estimate of sample variance.
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
