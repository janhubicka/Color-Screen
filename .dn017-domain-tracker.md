# Denoising issue tracker

This register tracks the screen-patch denoising review.  "Resolved" means the
implementation and a regression are present in the current review branch;
"Partial" means a correctness-preserving implementation slice is present but
the remaining calibration/evaluation work is still intentionally open.

| ID | Status | Priority | Issue |
|---|---|---|---|
| DN-001 | Resolved | Critical | In-place tiled denoising allowed later/parallel tiles to read already-denoised border samples.  Snapshot the input before writes. |
| DN-002 | Resolved | High | `nl_means` and `nl_fast` used different patch-distance normalization and therefore different strength semantics. |
| DN-003 | Resolved | High | Image-border reflection was asymmetric and failed for borders wider than small images. |
| DN-004 | Resolved | High | Bilateral tile border must equal its three-sigma spatial support. |
| DN-005 | Resolved | Medium | `screen_tile_rgb_color()` interchanged width/height scale loops on non-square channel lattices. |
| DN-006 | Resolved | Medium | `screen_denoise` GUI settings were not persisted in project parameter files. |
| DN-007 | Resolved | Medium | Invalid/non-finite denoise parameters could reach tile-size/index calculations. |
| DN-008 | Resolved | Low | `nl_fast` allocated large weight/output vectors for every tile.  Reuse per-thread scratch buffers. |
| DN-009 | Resolved | Low | Analysis cache keys considered inactive denoise fields and caused needless invalidation. |
| DN-010 | Resolved | High | Pre-demosaic spatial neighbourhoods are measured in common physical screen coordinates using the existing per-channel geometry mappings.  Paget packed phases and reflected negative coordinates are regression-tested; geometry-aware `nl_fast` deliberately uses the exact reference path. |
| DN-011 | Resolved | High | `precise_rgb` pre-demosaic filtering uses one scanner-RGB vector similarity weight for all three components, retaining common screen geometry and collection support.  Chromaticity preservation and fast/reference semantics are regression-tested. |
| DN-012 | Resolved | High | Preserve analyzer collection support through `analyze_base` and use it in pre-demosaic bilateral/NLM similarity and candidate weighting.  Unit support reproduces historical filtering; weak/sub-pixel samples have proportionally less authority. |
| DN-013 | Partial | High | NLM patch distance has an optional signal-dependent variance normalization `variance = floor + slope * signal`; a zero floor preserves historical output exactly.  The read-only estimator compares identical support-qualified, geometry-valid observations at physical spacings 1, 2 and 3, and can fit an empirical `V(h) = N + A h^p` law with free exponent when the scale-dependent excess is monotone and physically admissible.  Real Paget/Dufay fits are not stable across channels, so automatic application remains intentionally deferred. |
| DN-014 | Open | High | Design and evaluate a guided screen-lattice NLM, preferably IR-guided for registered RGB+IR scans. |
| DN-015 | Resolved | Medium | Split reconstruction-domain denoising into independent `screen_denoise` (before demosaicing) and `demosaiced_denoise` (after materialized Paget/Dufay demosaicing) parameters and GUI sections, with complete project-file persistence. |
| DN-021 | Resolved | High | Post-demosaic bilateral/NLM uses a common RGB-vector distance and applies one neighbour weight to all channels; reference and fast vector NLM are regression-tested. |
| DN-016 | Open | Medium | Build a quality corpus with edges, texture, Dufay/Paget geometry, RGB-vector chromaticity, confidence variation, and real scan crops. |
| DN-017 | Resolved | Medium | Compare the same three-scale statistic in linear intensity, density/log, and a generalized square-root variance-stabilized domain.  Density strongly fixes synthetic multiplicative/log-normal noise and modestly reduces Hurley/Paget scale dependence, but worsens Dufay fit quality and scale ratios; the VST flattens synthetic floor+slope noise but gives no consistent real-scan improvement.  Keep production denoising in linear sample space rather than choosing a process-dependent transform. |
| DN-018 | Open | Low | Precompute bilateral spatial weights and profile before optimizing the brute-force bilateral implementation. |
| DN-019 | Open | Low | Rework the fast NLM integral image to use an explicit `(w+1)*(h+1)` zero border; current indexing is correct under validated radii but unnecessarily subtle. |
| DN-020 | Open | Low | Consider exposing command-line denoise controls once algorithm semantics are stable; currently settings are GUI/project-file oriented. |

## DN-013 real-scan scale diagnostics

The three-scale diagnostic uses the same seven collected samples for all
spacings, so the ratios below are not caused by changing support or lattice
directions between scales.  Values are robust second-difference variance
ratios relative to spacing 1.

| Scan / separation | spacing 2 | spacing 3 | empirical scale fit |
|---|---:|---:|---|
| Hurley/Paget red | 2.68 | 4.72 | no non-negative scale-invariant component |
| Hurley/Paget green | 2.64 | 4.81 | `p = 1.55`, invariant fraction 0.15 |
| Hurley/Paget blue | 1.58 | 2.82 | `p = 2.50`, invariant fraction 0.88 |
| Dufay red | 1.14 | 1.27 | `p = 0.88`, invariant fraction 0.84 |
| Dufay green | 1.20 | 1.33 | no physically admissible fit |
| Dufay blue | 1.15 | 1.24 | no physically admissible fit |

The extra scale confirms that the simple spatial decomposition is not common
across processes or even across colour separations.  The fitted exponent is
therefore a diagnostic of structure/noise mixing, not a calibration parameter.

## DN-017 domain diagnostics

The domain comparison applies the same support/geometry-qualified seven-sample
three-scale statistic in linear intensity, density (`-log(x)`; non-positive
samples are rejected), and a generalized square-root transform obtained by
integrating `1/sqrt(floor + slope*x)` using the spacing-1 linear noise fit.  It
is read-only and never changes rendering parameters.

The table gives spacing-2/spacing-1, spacing-3/spacing-1, and the dimensionless
spacing-1 floor+slope fit residual.

| Scan / separation | linear | density | variance-stabilized |
|---|---|---|---|
| Hurley/Paget red | `2.68 / 4.72 / 0.738` | `2.08 / 3.30 / 0.732` | `2.69 / 4.83 / 0.740` |
| Hurley/Paget green | `2.64 / 4.81 / 0.734` | `2.08 / 3.44 / 0.685` | `2.66 / 4.93 / 0.732` |
| Hurley/Paget blue | `1.58 / 2.82 / 0.729` | `1.49 / 2.26 / 0.572` | `1.53 / 2.68 / 0.736` |
| Dufay red | `1.14 / 1.27 / 0.776` | `1.14 / 1.28 / 0.878` | `1.14 / 1.27 / 0.776` |
| Dufay green | `1.20 / 1.33 / 0.340` | `1.24 / 1.42 / 0.842` | `1.20 / 1.33 / 0.426` |
| Dufay blue | `1.15 / 1.24 / 0.799` | `1.20 / 1.30 / 0.838` | `1.15 / 1.24 / 0.799` |

Density is clearly the right representation for the synthetic multiplicative
case and it reduces Hurley/Paget scale dependence, especially in blue.  It is
not a universal improvement: all Dufay separations have worse fit residuals,
and green/blue become less scale-invariant.  The model-derived VST behaves as
expected on synthetic floor+slope noise but does not improve the real scans.
Consequently there is no evidence for changing the production denoising domain
from linear sample values; domain choice would otherwise become process- or
channel-dependent.

## Recommended next implementation order

1. Add IR-guided similarity for registered RGB+IR scans (DN-014).  Keep the
   current RGB similarity and linear-domain filtering as the baseline so the
   guide comparison is not confounded by a process-dependent domain choice.
2. Grow the quality corpus with stable real-scan crops and quantitative
   texture/edge/chromaticity/confidence checks (DN-016), including cases where
   IR guidance should help or hurt.
3. Revisit automatic DN-013 coefficient estimation only if guide/corpus work
   produces a robust way to identify genuinely homogeneous screen regions;
   otherwise keep the explicit variance model opt-in.
4. Only after quality is established, optimize the geometry-aware
   implementation (DN-018/DN-019).
