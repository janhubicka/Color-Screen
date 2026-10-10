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

## Size-aware libcolorscreen cache (post-PR #536)

All RAW decoded-variant cache ownership lives in `libcolorscreen`, using
the existing generic `lru_cache` rather than a parallel per-source list or
anything in Qt. The cache has an optional byte budget and measures the
estimated owned pixel buffers (width × height × active channel pixel size,
plus the image descriptor). The RAW variant cache has a **256 MiB budget for
cache-owned decoded images**. Its key is the unique *unpacked sensor source
generation* plus the demosaicing method; output profile, gamut diagnostics,
Qt view state and rendering adjustments are not part of this key.

When inserting a result, the shared LRU releases oldest cache-owned values
until the new result fits. An oversized result still returns successfully
without being retained. Even if a renderer/view holds a previously cached
image, eviction only releases the cache's strong reference; that external
`shared_ptr` remains valid and its memory is no longer charged to the
cache. Identical concurrent requests coalesce; requests for different
algorithms on the same LibRaw resource still serialize `dcraw_process()`
through the source's mutex.

Each completed image variant now retains the unpacked sensor source in an
opaque `image_data` handle. The source does not own the cache or variants,
so **there is no ownership cycle**. Consequently a derivative can request
another demosaicing method even after the original image is destroyed.
The separately enforced **256 MiB mosaic-reservation budget** still limits
how many unpacked RAW sources are retained by live images. Under pressure
the library first prunes idle decoded variants to free source reservations.

Qt holds only the currently displayed image (and a short-lived outgoing image
reference while a worker runs); it does **not** cache original RAW images,
method variants, or cache-size metadata. Explicit **Reload and demosaic**
hands the existing `image_data` to its generation-gated asynchronous worker.
An ordinary Open still rereads the file, and non-Bayer/over-budget/EIP inputs
use the established independent file-loader path. Qt keeps the same
cancellation, failure rollback, Undo and slanted-edge-reference behaviour.

The size budget measures *cache-owned* buffers, not total resident memory.
Live images held by frontends, renderers or workers, the original displayed
decoded image and LibRaw's mutable scratch buffers can use additional memory.
A hard process RSS cap would require a separate resource manager; do not
misrepresent the LRU's byte limit as that guarantee.

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
4. **Bounded cache ownership:** libcolorscreen's source quota and weighted
   decoded-variant LRU have separate budgets. Eviction drops only cache-held
   `shared_ptr` references; independently displayed images and their source
   mosaics remain valid. Variants own sources; sources own no variants.
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
- `lru_cache_byte_budget` exercises weighted eviction, oversized uncached
  results, pinned-value survival and single-flight callbacks.
- The synthetic Bayer fixture now exercises variant-source ownership after
  dropping the original image, and still checks independent pixel agreement.
  Extend testing to high-memory cache pressure across independent sources.
- Add Qt lifecycle smoke for rapid demosaic changes during loading, independent
  document views, failed reload recovery, and close with an old worker in flight.
  The existing generation gates are kept, but targeted tests are still needed.
- Compare monochromatic/Bayer-corrected results and relevant metadata with
  independent file decodes; validate half-size geometry and pixel pitch.
- Run the full Ubuntu (including checking and distcheck), macOS, Windows and
  sanitizer CI matrix on the new sized-cache PR before merging.

Keep the product at **2.0alpha** and continue persisting `demosaic` as a
capture-input choice; no cache resource belongs in persisted JSON, ZIP-v1 or
legacy .par parameters. New frontends must reuse the libcolorscreen cache.
