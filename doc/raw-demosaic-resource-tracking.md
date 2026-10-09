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

## Implemented ownership: PR #536

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

## Bounded on-demand variants and Qt reload path (PR #536)

The source resource now has a per-source LRU of up to two completed,
independently allocated image variants, with a separate process-wide 256 MiB
strong-cache budget. The key is the RAW demosaicing algorithm. An evicted
variant stays valid for external viewers. Output profile, gamut diagnostics
and reconstruction settings are not cache keys.

Qt retains the successfully loaded original RAW image as a source owner while
showing a derivative. An explicit **Reload and demosaic** on the same input
path asks for `demosaiced_variant()` on its background worker, preserving the
existing generation, cancellation, failure-restoration, parameter/Undo and
slanted-edge reference reload behavior. The `QFuture` owns the finished image
result; the worker never modifies `m_scan` or the output pointer of its GUI
callback. An ordinary Open deliberately rereads the file, while non-Bayer,
over-budget and EIP inputs use the established full-file decode path.

Because retaining the original owner also retains that original decoded pixel
buffer, the extra per-document resident-image cost needs measuring on large
RAW scans. The global mosaic and variant budgets do not count the source
owner's already-rendered RGB buffer. If this proves expensive, factor an opaque
source-only handle out of `image_data` rather than sharing a LibRaw processor
through a reference cycle.

Invariants for final validation:

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

## Verification state and remaining work

- `raw-demosaic-cache.test` creates a small CFA DNG without external assets.
  It compares AHD, PPG, half-size and linear requests against independent
  file decodes, tests repeated cache hits, original-pixel immutability,
  cancellation followed by a valid decode, concurrent same-method requests,
  and independence of separately opened RAW sources.
- `raw_source_retention` verifies that synthetic data and ordinary TIFFs
  cannot claim an unpacked sensor source. Extend the fixture to check early
  failure and mid-processing cancellation on real CFA data.
- Add dedicated cache-budget admission/eviction and cache lifetime tests,
  including an externally held variant surviving source destruction.
- Add Qt lifecycle smoke for rapid demosaic changes during loading, independent
  document views, failed reload recovery, and close with an old worker in flight.
  The existing generation gates are kept, but targeted tests are still needed.
- Compare monochromatic/Bayer-corrected results and relevant metadata with
  independent file decodes; validate half-size geometry and pixel pitch.
- Run the full Ubuntu (including checking and distcheck), macOS, Windows and
  sanitizer CI matrix before making this draft ready to merge.

Keep the product at **2.0alpha** and continue persisting `demosaic` as a
capture-input choice; none of these resources belongs in the YAML/archive
parameter serialization.
