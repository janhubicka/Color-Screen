# Color-Screen Finetune Takeout: Deferred IR-Only Fitting Work

Handoff state after the scalar physical-defocus and fallback-blur work; prepared before switching focus to denoising.

## Purpose

This document freezes the current finetune state and records the unresolved IR-only fitting problem so work can resume later without reconstructing the reasoning from the development thread.

The immediate project priority is now denoising. The IR-only work described here is deliberately deferred.


## Current stable baseline

The current source baseline is revision 0033. Finetune behavior itself is essentially the state reached in revisions 0030–0032; 0033 primarily fixes unrelated ICC cleanup and GUI lifetime issues.

Scalar physical defocus is the preferred calibrated model when optical metadata are available. It uses a signed physical OTF, cached source spectra, cached invariant optical state, direct periodic transfer application, a nonlinear focus-node table, and exact final evaluation.

The metadata-free empirical fallback blur model is also fast now, but it is intentionally kept exact rather than interpolated. It starts from zero blur, restoring the historically stable basin behavior while retaining the fast direct analytical transfer and source-spectrum reuse.

Adaptive fitting records transient per-cell diagnostics: robust spread, accepted/total samples, and fitted contrast. These diagnostics are for analysis and validation; they are not stored in the scanner-blur correction parameter file, whose primary purpose is to drive adaptive color-loss correction.


## What is already working well

- Physical-defocus estimation works well on real RGB scans and is fast enough for dense adaptive analysis.
- The current focus range is bounded using the process-screen frequency and a configurable MTF threshold. The dense physical-defocus pass uses nonlinear focus nodes, with exact reconstruction and scoring of the final selected solution.
- Low-information local fits can be rejected using fitted screen contrast, and per-cell repeatability/support diagnostics are available for inspection.
- The GUI exposes individual finetune parameters, independent coarse/dense grids, strip-width controls, focus-cache settings, profiling, and measured monochrome/IR selection.
- Fallback blur is again usable and fast, but its deeper blur/color ambiguity remains a known model limitation rather than a cache problem.

## Deferred problem A: IR-only fitting needs image-content selection

The IR layer contains the actual underlying monochrome image. Therefore arbitrary local IR structure is not nuisance-free: scene edges, texture, grain, and local brightness changes can dominate or bias a fit intended to infer screen geometry or optical defocus.

Before relying on IR-only adaptive focus, the fitter needs a way to identify locally suitable image regions—roughly, areas where the underlying image/color content is sufficiently uniform that the screen/optical response can be inferred without confusing scene structure with defocus.

This should not be reduced to a single raw-gradient threshold. A useful detector should operate at scales separated from the screen period and should distinguish broad image variation from strong local structure at or near the frequencies used by the finetuner.


## Candidate solid-area detector for later evaluation

- Work on a low-pass representation of the measured IR image whose cutoff is safely below the screen fundamental. Use this representation to estimate underlying scene gradients and local variance.
- Reject neighborhoods containing strong edges, high local variance, clipping, non-finite values, or insufficient dynamic range. The selection scale should be substantially larger than one screen period so the detector does not mistake screen-scale residuals for scene texture.
- Optionally use a multi-scale structure measure (for example local gradient energy or a structure tensor) rather than only variance, since a smooth ramp may be usable while a sharp edge with comparable variance is not.
- Return a mask/weight, not only a Boolean decision. This will allow later confidence weighting and visualization of why a region was or was not used.
- Keep the first implementation diagnostic-only: visualize the selected areas on real RGB+IR scans before letting the mask change the fitted correction table.

## Deferred problem B: color freedom can exaggerate defocus

The current fitting architecture can fit simulated screen colors strongly enough that an over-saturated simulated color response compensates for excessive blur. This lets the optimizer exaggerate defocus while still reducing the raw residual.

The same qualitative degeneracy was observed in the empirical fallback model: extra blur and more extreme fitted screen primaries can form a second, sometimes numerically better, basin. Restoring zero-blur initialization fixed the regression but did not remove the underlying ambiguity.

IR-only work must therefore constrain color degrees of freedom before using blur/defocus as a trustworthy physical estimate.


## Candidate color constraints for later evaluation

- Estimate robust screen-primary chromaticities from well-focused/high-confidence regions and reuse them during local defocus fitting. Allow only overall gain/brightness or a small, physically motivated set of color adjustments locally.
- Alternatively add a weak regularization term penalizing excessive departure from robust primary colors or implausibly high simulated saturation. Any regularization must be calibrated so genuine spatial color-response changes are not suppressed.
- A staged solve may be preferable: first estimate colors from sharp/solid regions, then fit focus with those colors fixed or tightly constrained, then optionally perform a small joint refinement.
- Conditioning of the local 3x3 color-loss matrix should be monitored. Very blurred areas can remain geometrically identifiable while color reconstruction becomes ill-conditioned because higher screen harmonics have disappeared.

## Important distinction for future diagnostics

Finetune identifiability asks: can the local optical blur/defocus be estimated reliably?

Color recoverability asks: given that blur, is enough chromatic screen information still present to reconstruct color stably?

These are not the same condition. The large corner stress sample demonstrates that focus can remain measurable even where visual color recovery is already poor.


## External validation assets (do not add to the testsuite)

sample.tif + sample.par: large corner crop of a Dufaycolor scan. Useful for physical-defocus stress testing and for comparing focus confidence against color recoverability. The worst bottom-right region is substantially out of focus and is a good real-world failure case.

sample-fallback.par: metadata-free fallback configuration for the same crop. This exposed the fallback blur/color multimodality and the 0015 warm-start regression. Keep it for external/manual regression runs.

These files are intentionally too large for the normal testsuite. Small synthetic tests should capture algorithmic contracts; the real crop should remain an external integration/stress fixture.


## Experiments to run when IR-only work resumes

- 1. Produce an IR solid-area mask on one or more real RGB+IR scans and inspect it visually before changing fitting behavior.
- 2. Compare local focus from normalized RGB and IR on regions classified as solid/high-confidence. Record cell-by-cell differences, repeatability spread, and rejection counts.
- 3. On synthetic data with known defocus, add scene edges/texture to the IR image and measure when the IR-only estimator becomes biased.
- 4. Fit the same data with free colors versus constrained colors. Plot objective versus defocus and verify that the high-blur/saturation basin is removed rather than merely made harder for one optimizer start to find.
- 5. Compare estimated colors/defocus against the physical model and the large corner stress sample. Ensure that genuinely severe blur is still recoverable and not artificially forced toward zero.
- 6. Only after these checks add an Auto/RGB/IR focus-source policy to both GUI and command line.

## Explicitly deferred items

- Dufay strip-width discretization/caching remains experimental and should not be mixed into the IR-only work.
- Per-channel adaptive defocus should wait until the representation and optical wavelength semantics are settled.
- Generic Nelder-Mead corrections, GSL cleanup, and geometry-coordinate refactors are separate technical-debt tasks.
- Do not persist repeatability/confidence diagnostics in scanner_blur_correction_parameters unless a future consumer actually needs them for rendering. CSV/GUI diagnostics are sufficient for analysis.

## Resume-here checklist

- Start from the current post-0033 source, preserving the physical-defocus and fallback behavior already field-tested.
- Implement diagnostic-only IR solid-region scoring/masking first.
- Add an explicit constrained-color experiment; do not rely on optimizer initialization as the solution to blur/color degeneracy.
- Use the large corner crop as an external stress benchmark and synthetic small fixtures for automated tests.
- Keep physical defocus, fallback blur, and Dufay strip-width experiments separated so a regression can be attributed to one model.
- After IR-only reliability is demonstrated, add Auto/RGB/IR source selection and decide how confidence/recoverability information should be presented to users.
