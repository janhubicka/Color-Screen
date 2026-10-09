# RAW demosaicing resource cache (2.0alpha)

## Motivation

An `image_data` is currently one fully decoded image. A RAW file is unpacked
by `raw_image_data_loader::init_loader()` and processed by
`LibRaw::dcraw_process()`. Its RGB/grayscale pixels are copied into the
`image_data` buffers by `load_part()`. Historically, completing that load
destroyed the loader and its LibRaw processor, including the original unpacked
CFA samples. Switching the persisted `render_parameters::demosaic` selection
through the Qt image loader therefore reopened and unpacked the same file.

LibRaw permits repeated processing of already unpacked data with different
processing parameters. This should be used for on-demand demosaicing rather
than caching only the finished RGB image.

## Ownership stage: PR #536

- `image_data` now has a private, opaque `unpacked_raw_source` handle.
  Only successful RAW loading can publish the handle.
- Its LibRaw object retains the original *unpacked sensor data*. The
  temporary `imgdata.image` postprocessing buffer is freed after a
  completed output has been copied into `image_data`, so the active image
  remains unchanged and independent.
- The initial retained-source scope is **conventional Bayer CFA only**.
  Unaccounted X-Trans/multicomponent RAW formats and EIP `open_buffer`
  sources keep the established release-on-completion behavior until their
  memory consumption and backing-input lifetimes can be measured.
- A **256 MiB process-wide reservation** limits retained Bayer buffers.
  Large images still load normally, but `has_unpacked_raw_source()` reports
  false if caching would exceed the budget. This is a cache admission decision,
  not a restriction on supported images.
- The LibRaw processor is destroyed before any borrowed EIP input buffer.
  Both the retained buffer and its quota reservation are released with their
  `image_data` owner. Cancellations and failures never publish a source.
- Existing TIFF/JPEG/PNG/JP2/stitch and synthetic image loading retains no
  RAW context. The active decoded pixel buffers are always owned
  independently of the LibRaw postprocessing arrays.

## Next implementation: bounded on-demand variants

The source resource will eventually own a small LRU of completed,
independently allocated image variants keyed by an explicit demosaicing
request (algorithm and any relevant future decoder parameters). Do not
include presentation/output profile, gamut diagnostics or screen
reconstruction controls in this key.

The following invariants must be tested before exposing this API to Qt:

1. **Exclusive processing:** serialize `dcraw_process()` on a shared LibRaw
   source. It overwrites its working `imgdata.image` and is not safe to
   run concurrently on the same processor. Finished `image_data` variants
   may be read concurrently because they own separate immutable pixels.
2. **Complete publication:** allocate and populate a new result privately;
   publish it in the cache only after every row, metadata and optional ICC
   processing succeeds. A failed/cancelled request must not poison an existing
   good variant or the retained CFA data.
3. **Pixel geometry:** `half`, monochromatic, Bayer-corrected and standard
   demosaicing can differ in dimensions, channel availability, pitch and
   selected sharpening domain. Each variant must carry metadata matching
   its own output without modifying the source's accepted metadata.
4. **Bounded memory:** a small LRU/budget counts both CFA and decoded
   variants. Eviction must not destroy a variant still referenced by a
   renderer or view. Avoid strong ownership cycles between the source and
   variants; cache entries can use weak references with a separately bounded
   pool of strong entries.
5. **Version and cancellation:** the Qt document loads a RAW source once,
   then requests variants through generation-gated asynchronous work. A
   completion belonging to an old image, stale algorithm request or closed
   document must not replace the current scan or Undo history.
6. **Fallback:** a source whose raw buffer was not retained (over quota,
   unsupported layout, EIP) falls back to the existing on-disk loader with
   identical semantics and a clear cache-miss path. Raw/unpacked ownership
   is an optimization, not a new file-format requirement.
7. **Persisted choice:** `render_parameters::demosaic` remains a saved
   capture/decode selection. A cache hit must not silently change the
   parameter file, saved working crop or the accepted source identity.

## Verification to add

- A small real CFA RAW/DNG fixture: prove
  `has_unpacked_raw_source()` on completion and false on early failure,
  cancellation and non-RAW paths.
- Compare two algorithms with repeated `dcraw_process()` against two fresh
  file decodes pixel for pixel (including monochrome/Bayer compensation and
  half-size mode), with proper metadata comparison.
- Concurrent same-source variant requests, cache admission/eviction, and
  failed/cancelled decode regression tests.
- Qt smoke: rapid demosaic selection during loading, two independent
  views, undo/save/restore, and released image while an old worker is in
  flight.

This note describes the agreed architecture; PR #536 supplies only the
retained-source ownership stage, not the full public variant API.
