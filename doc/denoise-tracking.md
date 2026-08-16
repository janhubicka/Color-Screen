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
| DN-013 | Partial | High | NLM patch distance has an optional signal-dependent variance normalization `variance = floor + slope * signal`; a zero floor preserves historical output exactly.  A read-only robust estimator now measures per-separation floor/slope from support-filtered, geometry-valid second differences and exposes fit quality.  Synthetic scalar/RGB/Paget regressions recover known models, but real Hurley/Paget and Dufay scans show substantial fit residuals and channel disagreement, so automatic application remains intentionally deferred. |
| DN-014 | Open | High | Design and evaluate a guided screen-lattice NLM, preferably IR-guided for registered RGB+IR scans. |
| DN-015 | Resolved | Medium | Split reconstruction-domain denoising into independent `screen_denoise` (before demosaicing) and `demosaiced_denoise` (after materialized Paget/Dufay demosaicing) parameters and GUI sections, with complete project-file persistence. |
| DN-021 | Resolved | High | Post-demosaic bilateral/NLM uses a common RGB-vector distance and applies one neighbour weight to all channels; reference and fast vector NLM are regression-tested. |
| DN-016 | Open | Medium | Build a quality corpus with edges, texture, Dufay/Paget geometry, RGB-vector chromaticity, confidence variation, and real scan crops. |
| DN-017 | Open | Medium | Decide whether filtering is best in linear intensity, density/log, or variance-stabilized domain for scanner/film noise. |
| DN-018 | Open | Low | Precompute bilateral spatial weights and profile before optimizing the brute-force bilateral implementation. |
| DN-019 | Open | Low | Rework the fast NLM integral image to use an explicit `(w+1)*(h+1)` zero border; current indexing is correct under validated radii but unnecessarily subtle. |
| DN-020 | Open | Low | Consider exposing command-line denoise controls once algorithm semantics are stable; currently settings are GUI/project-file oriented. |

## Recommended next implementation order

1. Extend the DN-013 diagnostic estimator to a second physical spacing and use
   the scale dependence to separate independent measurement noise from smooth
   image/emulsion curvature or texture.  Compare the spacing-1/spacing-2
   diagnostics on the Hurley Paget and near-Nyquist Dufay scans.
2. Only if the estimator becomes stable across spatial scales and channels,
   map its result into rendering coefficients and check whether one normalized
   NLM strength has comparable meaning across both processes.
3. If substantial signal dependence remains after the refined noise model,
   evaluate linear intensity against density/log or a variance-stabilized
   domain (DN-017).
4. Add IR-guided similarity for registered RGB+IR scans and compare it with
   the calibrated RGB guide (DN-014).
5. Grow the quality corpus with stable real-scan crops and quantitative
   texture/edge/chromaticity/confidence checks (DN-016).
6. Only after quality is established, optimize the geometry-aware
   implementation (DN-018/DN-019).
