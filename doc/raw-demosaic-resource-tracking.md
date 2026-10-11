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

## Global RAM-aware libcolorscreen cache (follow-up to #536)

Cache policy is global to **all** libcolorscreen LRU instances, including
renderers, image processing and the demosaiced RAW variants. A core-only
registry gathers entry counts, active computations, externally pinned results
and retained byte-size estimates, and can evict the globally least-recently
used completed entries across cache types. The Qt GUI does not own a cache
manager; it simply requests an `image_data::demosaiced_variant()`.

The available-memory probe is platform-native: Linux uses
`/proc/meminfo` and respects a cgroup-v2 memory limit, macOS uses
Mach VM statistics / `sysctl`, and Windows uses `GlobalMemoryStatusEx`.
The **soft global cache target** is the smaller of one third of total
physical memory and half of effectively available memory plus pages
already retained by core caches and RAW source reservations. Consequently,
a machine with many gigabytes free can retain one or more full 150 MP
images, while memory-poor systems preferentially evict completed caches.

There is no fixed 256 MiB limit on an unpacked RAW CFA or a decoded variant.
Both sensor-memory reservations and decoded variants participate in the same
shared policy. Source admission is optional; if insufficient memory is
available, normal decoding remains valid without retaining a reusable CFA.
The existing generic LRU supports optional byte-size callbacks. RAW variants
report decoded pixel allocation sizes; other cache types currently report
either measured allocations, at least sizeof(T), or unknown for unbounded
array types, rather than claiming exact memory accounting for every legacy
cache.

Every decoded variant retains its own shared opaque sensor resource; no
source points back to its variants, so ownership is acyclic. Qt therefore
keeps only the currently displayed image and outgoing worker references,
not a separate cached original RAW. Explicit Reload and demosaic asks the
core to reuse that source; ordinary Open still performs independent file
loading, including for unsupported RAW formats.

**Important:** cache-owned bytes are not total process RSS. A renderer/view
may retain an image after its cached entry is evicted; LibRaw scratch buffers
and active decoding also require temporary memory. The global budget cannot
delete these live references. This is a memory-pressure policy, not a hard
per-process allocation ceiling.

For development, **Help → Cache Statistics…** presents the read-only
`get_cache_memory_statistics()` snapshot once per second: installed and
available RAM, global cache target, cache-owned bytes, RAW sensor bytes, and
per-LRU entry, in-progress, pinned and byte estimates with a precision flag.
The API uses only standard C++ types and is reusable by non-Qt frontends.

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
- `lru_global_memory` uses a deterministic tiny global budget to verify
  eviction across independent caches, pinned-value survival, RAW source
  reservation and unregister safety. The Qt workspace smoke opens/closes the
  read-only cache statistics dialog and checks that document state is intact.
- The synthetic Bayer fixture now exercises variant-source ownership after
  dropping the original image, and still checks independent pixel agreement.
  Extend testing to high-memory cache pressure across independent sources.
- Add Qt lifecycle smoke for rapid demosaic changes during loading, independent
  document views, failed reload recovery, and close with an old worker in flight.
  The existing generation gates are kept, but targeted tests are still needed.
- Compare monochromatic/Bayer-corrected results and relevant metadata with
  independent file decodes; validate half-size geometry and pixel pitch.
- Run full Ubuntu (including checking and distcheck), macOS, Windows and
  sanitizer CI on the global cache/GUI statistics revision; then field-test
  150+ MP captures under actual memory pressure before beta.

Keep the product at **2.0alpha** and continue persisting `demosaic` as a
capture-input choice; no cache resource belongs in persisted JSON, ZIP-v1 or
legacy .par parameters. New frontends must reuse the libcolorscreen cache.
