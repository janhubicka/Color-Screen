# Denoising issue tracker

This register tracks the screen-patch denoising review.  "Resolved" means the
implementation and a regression are present in the current review branch;
"open" items are intentionally deferred rather than silently approximated.

| ID | Status | Priority | Issue |
|---|---|---|---|
| DN-001 | Resolved | Critical | In-place tiled denoising allowed later/parallel tiles to read already-denoised border samples.  Snapshot the input before writes. |
| DN-002 | Resolved | High | `nl_means` and `nl_fast` used different patch-distance normalization and therefore different strength semantics. |
| DN-003 | Resolved | High | Image-border reflection was asymmetric and failed for borders wider than small images. |
| DN-004 | Resolved | High | Bilateral tile border must equal its three-sigma spatial support. |
| DN-005 | Resolved | High | `screen_tile_rgb_color()` interchanged width/height scale loops on non-square channel lattices. |
| DN-006 | Resolved | Medium | `screen_denoise` GUI settings were not persisted in project parameter files. |
| DN-007 | Resolved | Medium | Invalid/non-finite denoise parameters could reach tile-size/index calculations. |
| DN-008 | Resolved | Low | `nl_fast` allocated large weight/output vectors for every tile.  Reuse per-thread scratch buffers. |
| DN-009 | Resolved | Low | Analysis cache keys considered inactive denoise fields and caused needless invalidation. |
| DN-010 | Open | High | Spatial radii are measured in channel-array indices rather than common screen coordinates; Dufay/Paget channels therefore use physically different neighbourhoods. |
| DN-011 | Open | High | `precise_rgb` denoises R/G/B components independently.  Use one vector/guide-derived neighbour weight for all components. |
| DN-012 | Open | High | Analyzer collection confidence/sample support is discarded before denoising.  Preserve and use it as measurement uncertainty. |
| DN-013 | Open | High | NLM strength has no calibrated noise model and patch distance does not account for expected noise variance or signal dependence. |
| DN-014 | Open | High | Design and evaluate a guided screen-lattice NLM, preferably IR-guided for registered RGB+IR scans. |
| DN-015 | Open | Medium | Distinguish patch-domain `screen_denoise` from the dormant post-demosaic/output `render_parameters::denoise` stage in API/UI and decide whether the latter should exist. |
| DN-016 | Open | Medium | Build a quality corpus with edges, texture, Dufay/Paget geometry, RGB-vector chromaticity, confidence variation, and real scan crops. |
| DN-017 | Open | Medium | Decide whether filtering is best in linear intensity, density/log, or variance-stabilized domain for scanner/film noise. |
| DN-018 | Open | Low | Precompute bilateral spatial weights and profile before optimizing the brute-force bilateral implementation. |
| DN-019 | Open | Low | Rework the fast NLM integral image to use an explicit `(w+1)*(h+1)` zero border; current indexing is correct under validated radii but unnecessarily subtle. |
| DN-020 | Open | Low | Consider exposing command-line denoise controls once algorithm semantics are stable; currently settings are GUI/project-file oriented. |

## Recommended next implementation order

1. Land the resolved correctness fixes and run native Qt plus sanitizer builds.
2. Preserve per-screen-sample collection confidence through `analyze_base`.
3. Introduce geometry-aware screen-coordinate neighbourhoods.
4. Prototype common-weight RGB/vector NLM without IR.
5. Add IR-guided similarity for registered RGB+IR scans and compare it with the RGB guide.
6. Calibrate strength from an explicit noise/variance estimate.
7. Only after quality is established, optimize the geometry-aware implementation.
