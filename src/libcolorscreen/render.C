#include "deconvolve.h"
#include "gaussian-blur.h"
#include "include/colorscreen.h"
#include "include/sensitivity.h"
#include "include/spectrum-to-xyz.h"
#include "include/stitch.h"
#include "lru-cache.h"
#include "mapalloc.h"
#include "render.h"
#include "sharpen.h"
#include "include/histogram.h"
#include <array>
#include <cassert>
#include <algorithm>
#include <fstream>
#include <limits>
#include <sstream>
#include <utility>

#if defined(_WIN32) || defined(WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <sys/sysctl.h>
#else
#include <unistd.h>
#endif

namespace colorscreen
{
class lru_caches lru_caches;
std::atomic_uint64_t lru_caches::time;

namespace
{
std::atomic<uint64_t> raw_cache_source_bytes {0};
std::atomic<uint64_t> test_cache_budget_override {0};

/* A conservative OS memory estimate. These APIs are deliberately implemented
   in libcolorscreen, rather than calling Qt's platform/system services. */
struct memory_reading
{
  uint64_t total = 0;
  uint64_t available = 0;
  /* Distinguish an actual zero-free-memory reading from OS APIs which do
     not provide an availability figure. A failed platform probe must not
     accidentally disable every core LRU cache. */
  bool available_known = false;
};

memory_reading
query_host_memory ()
{
  memory_reading result;
#if defined(_WIN32) || defined(WIN32)
  MEMORYSTATUSEX stat {};
  stat.dwLength = sizeof (stat);
  if (GlobalMemoryStatusEx (&stat))
    {
      result.total = stat.ullTotalPhys;
      result.available = stat.ullAvailPhys;
      result.available_known = true;
    }
#elif defined(__APPLE__)
  uint64_t physical = 0;
  size_t length = sizeof (physical);
  if (sysctlbyname ("hw.memsize", &physical, &length, nullptr, 0) == 0)
    result.total = physical;
  mach_port_t host = mach_host_self ();
  vm_statistics64_data_t vm {};
  mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
  vm_size_t page_size = 0;
  if (host_page_size (host, &page_size) == KERN_SUCCESS
      && host_statistics64 (host, HOST_VM_INFO64, (host_info64_t)&vm,
                            &count) == KERN_SUCCESS)
    {
      result.available
          = (uint64_t)(vm.free_count + vm.inactive_count
                       + vm.speculative_count) * (uint64_t)page_size;
      result.available_known = true;
    }
  mach_port_deallocate (mach_task_self (), host);
#elif defined(__linux__)
  std::ifstream input ("/proc/meminfo");
  std::string line;
  while (std::getline (input, line))
    {
      std::istringstream part (line);
      std::string key;
      uint64_t kb = 0;
      if (!(part >> key >> kb))
        continue;
      if (key == "MemTotal:")
        result.total = kb * UINT64_C (1024);
      else if (key == "MemAvailable:")
        {
          result.available = kb * UINT64_C (1024);
          result.available_known = true;
        }
      if (result.total && result.available_known)
        break;
    }

  /* A container may see host MemAvailable despite having a much smaller
     cgroup memory.max. Respect effective v2 memory pressure where exposed. */
  std::ifstream max_file ("/sys/fs/cgroup/memory.max");
  std::ifstream current_file ("/sys/fs/cgroup/memory.current");
  std::string max_text;
  uint64_t current = 0;
  if (max_file >> max_text && max_text != "max"
      && current_file >> current)
    {
      try
        {
          const uint64_t limit = std::stoull (max_text);
          if (limit)
            {
              if (!result.total || limit < result.total)
                result.total = limit;
              const uint64_t remaining = current >= limit ? 0
                                                         : limit - current;
              if (!result.available_known || remaining < result.available)
                result.available = remaining;
              result.available_known = true;
            }
        }
      catch (const std::exception &)
        {
          /* Unknown cgroup representation: retain physical-host figures. */
        }
    }
#else
  const long pages = sysconf (_SC_PHYS_PAGES);
  const long available = sysconf (_SC_AVPHYS_PAGES);
  const long page_size = sysconf (_SC_PAGESIZE);
  if (pages > 0 && page_size > 0)
    result.total = (uint64_t)pages * (uint64_t)page_size;
  if (available >= 0 && page_size > 0)
    {
      result.available = (uint64_t)available * (uint64_t)page_size;
      result.available_known = true;
    }
#endif
  if (result.total && result.available_known
      && result.available > result.total)
    result.available = result.total;
  return result;
}

uint64_t
saturating_add (uint64_t a, uint64_t b)
{
  const uint64_t limit = std::numeric_limits<uint64_t>::max ();
  return b > limit - a ? limit : a + b;
}

/* Use at most one third of installed RAM and one half of available memory
   plus memory already owned by caches/sensor reservations. Adding retained
   memory back to MemAvailable prevents a cache insertion from shrinking the
   budget simply because it allocated pages. No fixed 256 MiB cap prevents
   a 150+ megapixel scan from being cached on a well-equipped machine. */
uint64_t
effective_cache_budget (const memory_reading &mem, uint64_t accounted)
{
  const uint64_t test_limit
      = test_cache_budget_override.load (std::memory_order_relaxed);
  if (test_limit)
    return test_limit;
  const uint64_t normal = mem.total ? mem.total / 3
                                    : UINT64_C (2) * 1024 * 1024 * 1024;
  if (!mem.available_known)
    return normal / 2; /* Conservative fallback if the OS probe fails. */
  if (!mem.available)
    return 0; /* Verified memory exhaustion: retain no idle cache data. */
  return std::min (normal, saturating_add (mem.available, accounted) / 2);
}
} // anonymous namespace

lru_cache_registry &
lru_cache_registry::instance ()
{
  /* Deliberately immortal: static cache instances in other translation units
     may unregister during shutdown after render.C's own static destructors. */
  static lru_cache_registry *registry = new lru_cache_registry;
  return *registry;
}

void
lru_cache_registry::set_test_budget_bytes (uint64_t bytes)
{
  test_cache_budget_override.store (bytes, std::memory_order_relaxed);
}

void
lru_cache_registry::register_cache (tracked_lru_cache *cache)
{
  std::lock_guard<std::mutex> guard (registry_mutex);
  caches.push_back (cache);
}

void
lru_cache_registry::unregister_cache (tracked_lru_cache *cache)
{
  std::lock_guard<std::mutex> guard (registry_mutex);
  caches.erase (std::remove (caches.begin (), caches.end (), cache),
                caches.end ());
}

uint64_t
raw_source_cache_bytes ()
{
  return raw_cache_source_bytes.load (std::memory_order_relaxed);
}

void
release_raw_source_cache_bytes (uint64_t bytes)
{
  if (bytes)
    raw_cache_source_bytes.fetch_sub (bytes, std::memory_order_relaxed);
}

cache_memory_statistics
lru_cache_registry::snapshot ()
{
  cache_memory_statistics result;
  {
    std::lock_guard<std::mutex> guard (registry_mutex);
    for (tracked_lru_cache *cache : caches)
      {
        cache_entry_statistics entry = cache->cache_statistics (nullptr);
        result.cached_bytes
            = saturating_add (result.cached_bytes, entry.retained_bytes);
        result.caches.push_back (std::move (entry));
      }
    result.retained_raw_source_bytes = raw_source_cache_bytes ();
  }
  const memory_reading mem = query_host_memory ();
  result.total_memory_bytes = mem.total;
  result.available_memory_bytes = mem.available;
  result.cache_budget_bytes = effective_cache_budget (
      mem, saturating_add (result.cached_bytes,
                           result.retained_raw_source_bytes));
  std::sort (result.caches.begin (), result.caches.end (),
             [] (const cache_entry_statistics &a,
                 const cache_entry_statistics &b)
             { return a.name < b.name; });
  return result;
}

void
lru_cache_registry::enforce_budget (uint64_t extra_bytes)
{
  /* Hold the registry lock while selecting/calling a cache to prevent it
     being destroyed concurrently. Evicted shared_ptrs are destructed ONLY
     after releasing that lock: image_data destructors may prune other caches.
     No cache may call this method while holding its own entry mutex. */
  std::vector<std::shared_ptr<void>> retired;
  {
    std::lock_guard<std::mutex> registry_guard (registry_mutex);
    const memory_reading mem = query_host_memory ();
    const uint64_t raw_bytes = raw_source_cache_bytes ();
    uint64_t current = raw_bytes;
    for (tracked_lru_cache *cache : caches)
      current = saturating_add (
          current, cache->cache_statistics (nullptr).retained_bytes);
    const uint64_t budget = effective_cache_budget (mem, current);

    /* At most the number of completed entries can be detached. A zero-byte
       array entry is intentionally excluded from global byte-pressure
       eviction, since its actual backing allocation is not measurable. */
    while (saturating_add (current, extra_bytes) > budget)
      {
        tracked_lru_cache *oldest_cache = nullptr;
        uint64_t oldest = std::numeric_limits<uint64_t>::max ();
        for (tracked_lru_cache *cache : caches)
          {
            uint64_t age = oldest;
            const cache_entry_statistics st = cache->cache_statistics (&age);
            if (st.retained_bytes && age < oldest)
              {
                oldest = age;
                oldest_cache = cache;
              }
          }
        if (!oldest_cache)
          break;
        retired.push_back (oldest_cache->discard_oldest_cache_entry ());
        uint64_t new_total = raw_bytes;
        for (tracked_lru_cache *cache : caches)
          new_total = saturating_add (
              new_total, cache->cache_statistics (nullptr).retained_bytes);
        if (new_total >= current)
          break;
        current = new_total;
      }
  }
  /* 'retired' releases all cache ownership here, outside every mutex. */
}

bool
reserve_raw_source_cache_bytes (uint64_t bytes)
{
  if (!bytes)
    return false;
  /* Serialize admission checks, not cache hits. Source destruction is an
     atomic subtraction and cannot wait on this lock during LRU eviction. */
  static std::mutex reservation_mutex;
  std::lock_guard<std::mutex> admission (reservation_mutex);
  lru_cache_registry::instance ().enforce_budget (bytes);
  const cache_memory_statistics s = lru_cache_registry::instance ().snapshot ();
  if (bytes > s.cache_budget_bytes
      || saturating_add (saturating_add (s.cached_bytes,
                                        s.retained_raw_source_bytes), bytes)
             > s.cache_budget_bytes)
    return false;
  raw_cache_source_bytes.fetch_add (bytes, std::memory_order_relaxed);
  return true;
}

cache_memory_statistics
get_cache_memory_statistics ()
{
  return lru_cache_registry::instance ().snapshot ();
}

uint64_t
cache_memory_budget_bytes ()
{
  return get_cache_memory_statistics ().cache_budget_bytes;
}

/* A wrapper class around precomputed image data which handles allocation and
   deallocation. This is needed for the cache.  */
class sharpened_data
{
public:
  mem_luminosity_t *m_data = nullptr;
  size_t stored_pixels_bytes = 0;
  /* Initialize sharpened data with given WIDTH and HEIGHT.  */
  sharpened_data (int width, int height);
  ~sharpened_data ();
};

sharpened_data::sharpened_data (int width, int height)
{
  if (width > 0 && height > 0)
    stored_pixels_bytes = (size_t)width * (size_t)height
                          * sizeof (mem_luminosity_t);
  m_data = (mem_luminosity_t *)MapAlloc::Alloc (
      stored_pixels_bytes, "HDR data");
}

sharpened_data::~sharpened_data ()
{
  if (m_data)
    MapAlloc::Free (m_data);
  m_data = nullptr;
}

/* A wrapper around interleaved precomputed RGB data.  Keeping the persistent
   representation interleaved avoids three separate cache allocations while
   the deconvolution implementation remains free to use planar FFT scratch.  */
class sharpened_rgb_data
{
public:
  mem_rgbdata *m_data = nullptr;
  size_t stored_pixels_bytes = 0;

  /* Allocate WIDTH by HEIGHT interleaved RGB pixels.  */
  sharpened_rgb_data (int width, int height);
  ~sharpened_rgb_data ();
};

sharpened_rgb_data::sharpened_rgb_data (int width, int height)
{
  if (width > 0 && height > 0)
    stored_pixels_bytes = (size_t)width * (size_t)height
                          * sizeof (mem_rgbdata);
  m_data = (mem_rgbdata *)MapAlloc::Alloc (
      stored_pixels_bytes, "HDR RGB data");
}

sharpened_rgb_data::~sharpened_rgb_data ()
{
  if (m_data)
    MapAlloc::Free (m_data);
  m_data = nullptr;
}

namespace
{
/* Parameters for backlight correction cache.  */
struct backlight_correction_cache_params
{
  class backlight_correction_parameters *backlight_correction_params = nullptr;
  uint64_t backlight_correction_id = 0;
  int width = 0;
  int height = 0;
  luminosity_t backlight_correction_black = 0;
  bool grayscale_needed = false;

  /* Return true if this parameter set is equal to O.  */
  bool
  operator== (const backlight_correction_cache_params &o) const
  {
    return backlight_correction_id == o.backlight_correction_id
           && width == o.width && height == o.height
           && backlight_correction_black == o.backlight_correction_black
           && grayscale_needed == o.grayscale_needed;
  }
};

/* Parameters for input lookup table cache.  */
struct lookup_table_params
{
  int maxval = 0;
  luminosity_t gamma = 1;
  std::vector<luminosity_t> gamma_table = {};
  luminosity_t dark_point = 0, scan_exposure = 1;

  /* Return true if this parameter set is equal to O.  */
  bool
  operator== (const lookup_table_params &o) const
  {
    return maxval == o.maxval && gamma == o.gamma
           && (gamma != 0 || gamma_table == o.gamma_table)
           && dark_point == o.dark_point && scan_exposure == o.scan_exposure;
  }
};

/* Parameters for grayscale data generation.  */
struct graydata_params
{
  uint64_t image_id = 0;
  const class image_data *img = nullptr;
  luminosity_t gamma = 1;
  std::vector<luminosity_t> gamma_table[3] = {};
  rgbdata dark = {0, 0, 0};
  luminosity_t red = 1, green = 1, blue = 1;
  class backlight_correction *backlight = nullptr;
  uint64_t backlight_correction_id = 0;
  bool ignore_infrared = false;

  /* Return true if this parameter set is equal to O.  */
  bool
  operator== (const graydata_params &o) const
  {
    return image_id == o.image_id && gamma == o.gamma
           && (gamma != 0
               || (gamma_table[0] == o.gamma_table[0]
                   && gamma_table[1] == o.gamma_table[1]
                   && gamma_table[2] == o.gamma_table[2]))
           && dark == o.dark && red == o.red
           && green == o.green && blue == o.blue
           && backlight_correction_id == o.backlight_correction_id
           && ignore_infrared == o.ignore_infrared;
  }
};

/* Parameters for grayscale and sharpened data cache.  */
struct gray_and_sharpen_params
{
  graydata_params gp = {};
  class sharpen_parameters sp = {};

  /* Return true if this parameter set is equal to O.  */
  bool
  operator== (const gray_and_sharpen_params &o) const
  {
    return gp == o.gp && sp == o.sp;
  }
};

/* Parameters for one-pass RGB sharpening.  GP describes the common linearized
   source and SP contains the effective sharpening state for R, G and B.  */
struct rgb_and_sharpen_params
{
  graydata_params gp = {};
  std::array<sharpen_parameters, 3> sp = {};

  /* Return true if this parameter set produces the same cached RGB output as
     O.  */
  bool
  operator== (const rgb_and_sharpen_params &o) const
  {
    return gp == o.gp && sp == o.sp;
  }
};

/* Parameters for image layer histogram cache.  */
struct image_layer_histogram_params
{
  uint64_t graydata_id = 0;
  int_image_area crop = {};
  render *r = nullptr;

  /* Return true if this parameter set is equal to O.  */
  bool
  operator== (const image_layer_histogram_params &o) const
  {
    return graydata_id == o.graydata_id && crop == o.crop;
  }
};

/* Create new backlight correction instance using parameters P.
   Report progress to PROGRESS.  */
std::unique_ptr<backlight_correction>
get_new_backlight_correction (backlight_correction_cache_params &p,
                              progress_info *progress)
{
  auto c = std::make_unique<backlight_correction> (
      *p.backlight_correction_params, p.width, p.height,
      p.backlight_correction_black, p.grayscale_needed, progress);
  if (!c->initialized_p ())
    return nullptr;
  return c;
}

/* Prototype for histogram generation.  */
std::unique_ptr<histogram>
get_new_image_layer_histogram (image_layer_histogram_params &p, progress_info *);

/* Create new image layer histogram using parameters P.  Report progress
   to PROGRESS.  */
std::unique_ptr<histogram>
get_new_image_layer_histogram (image_layer_histogram_params &p,
                               progress_info *)
{
  histogram hist;

  /* First determine the global range of values.  */
#pragma omp parallel for reduction(histogram_range : hist)
  for (int y = p.crop.y; y < p.crop.y + p.crop.height; y++)
    for (int x = p.crop.x; x < p.crop.x + p.crop.width; x++)
      hist.pre_account (p.r->get_unadjusted_data ({x, y}));

  hist.finalize_range (65536);

  /* Now account values into the finalized range.  */
#pragma omp parallel for reduction(histogram_entries : hist)
  for (int y = p.crop.y; y < p.crop.y + p.crop.height; y++)
    for (int x = p.crop.x; x < p.crop.x + p.crop.width; x++)
      hist.account (p.r->get_unadjusted_data ({x, y}));

  hist.finalize ();
  return std::make_unique<histogram> (std::move (hist));
}

/* Create new input lookup table using parameters P.  Report progress
   to PROGRESS.  */
std::unique_ptr<luminosity_t[]>
get_new_lookup_table (lookup_table_params &p, progress_info *)
{
  bool use_table = (p.gamma == 0) && !p.gamma_table.empty ();
  /* Use some sane data if table is missing.  */
  if (p.gamma == 0 && !use_table)
    p.gamma = 1;
  auto lookup_table = std::make_unique<luminosity_t[]> (p.maxval + 1);
  luminosity_t gamma = p.gamma;
  if (gamma != -1)
    gamma = std::clamp (gamma, (luminosity_t)0.0001, (luminosity_t)100.0);
  luminosity_t mul = 1 / (luminosity_t)(p.maxval);

  luminosity_t dark_point = p.dark_point;
  luminosity_t scan_exposure = p.scan_exposure;

  if (!use_table)
    for (int i = 0; i <= p.maxval; i++)
      lookup_table[i] = (apply_gamma (i * mul, gamma) - dark_point)
			* scan_exposure;
  else
    for (int i = 0; i <= p.maxval; i++)
      lookup_table[i] = (p.gamma_table[i] - dark_point) * scan_exposure;
  return lookup_table;
}

/* Prototypes for data generation.  */
std::unique_ptr<sharpened_data>
get_new_gray_sharpened_data (gray_and_sharpen_params &p, progress_info *progress);
std::unique_ptr<sharpened_rgb_data>
get_new_rgb_sharpened_data (rgb_and_sharpen_params &p,
                            progress_info *progress);

/* Large precomputed sharpened images dominate memory use for 150+ MP scans.
   Report their real allocated pixel-buffer sizes, not merely sizeof(pointer).
   The global LRU budget can then evict across sharpened and RAW image caches. */
static size_t
sharpened_gray_cached_bytes (const sharpened_data &image)
{
  return sizeof (image) + image.stored_pixels_bytes;
}

static size_t
sharpened_rgb_cached_bytes (const sharpened_rgb_data &image)
{
  return sizeof (image) + image.stored_pixels_bytes;
}

/* Static cache instances.  */
static lru_cache<backlight_correction_cache_params, backlight_correction,
                 get_new_backlight_correction, 10>
    backlight_correction_cache ("backlight corrections");

static lru_cache<image_layer_histogram_params, histogram,
                 get_new_image_layer_histogram, 10>
    image_layer_histogram_cache ("image layer histograms");

static lru_cache<lookup_table_params, luminosity_t[], get_new_lookup_table, 4>
    lookup_table_cache ("in lookup tables");

static lru_cache<gray_and_sharpen_params, sharpened_data,
                 get_new_gray_sharpened_data, 2>
    gray_and_sharpened_data_cache (
        "gray and sharpened data", 0, sharpened_gray_cached_bytes);

static lru_cache<rgb_and_sharpen_params, sharpened_rgb_data,
                 get_new_rgb_sharpened_data, 2>
    rgb_and_sharpened_data_cache (
        "RGB and sharpened data", 0, sharpened_rgb_cached_bytes);

/* Tables used during gray data computation.  */
struct gray_data_tables
{
  std::shared_ptr<luminosity_t[]> rtable = nullptr;
  std::shared_ptr<luminosity_t[]> gtable = nullptr;
  std::shared_ptr<luminosity_t[]> btable = nullptr;
  rgbdata dark = {0, 0, 0};
  luminosity_t red = 1, green = 1, blue = 1;
  backlight_correction *correction = nullptr;
};

/* Compute lookup tables for grayscale conversion using parameters P.
   If CORRECTION is true, backlight correction is enabled.  Report progress
   to PROGRESS.  */
inline gray_data_tables
compute_gray_data_tables (graydata_params &p, bool correction,
                          progress_info *progress)
{
  gray_data_tables ret = {};
  luminosity_t red = p.red;
  luminosity_t green = p.green;
  luminosity_t blue = p.blue;
  rgbdata dark = p.dark;

  ret.red = red;
  ret.green = green;
  ret.blue = blue;
  ret.dark = dark;

  lookup_table_params par;
  par.gamma = p.gamma;
  par.maxval = p.img->maxval;
  par.scan_exposure = correction ? 1 : red;
  par.dark_point = correction ? 0 : dark.red;
  par.gamma_table = p.gamma_table[0];
  ret.rtable = lookup_table_cache.get (par, progress);
  if (!ret.rtable)
    return ret;
  par.scan_exposure = correction ? 1 : green;
  par.dark_point = correction ? 0 : dark.green;
  par.gamma_table = p.gamma_table[1];
  ret.gtable = lookup_table_cache.get (par, progress);
  if (!ret.gtable)
    {
      ret.rtable = nullptr;
      return ret;
    }
  par.scan_exposure = correction ? 1 : blue;
  par.dark_point = correction ? 0 : dark.blue;
  par.gamma_table = p.gamma_table[2];
  ret.btable = lookup_table_cache.get (par, progress);
  if (!ret.btable)
    {
      ret.rtable = nullptr;
      ret.gtable = nullptr;
      return ret;
    }
  return ret;
}

/* Compute grayscale value for source pixel R, G, B at index X, Y
   using tables T.  */
inline luminosity_t
compute_gray_data (gray_data_tables &t, int x, int y, int r, int g, int b)
{
  luminosity_t l1 = t.rtable[r];
  luminosity_t l2 = t.gtable[g];
  luminosity_t l3 = t.btable[b];
  if (t.correction)
    {
      l1 = (t.correction->apply (l1, x, y,
                                 backlight_correction_parameters::red)
            - t.dark.red)
           * t.red;
      l2 = (t.correction->apply (l2, x, y,
                                 backlight_correction_parameters::green)
            - t.dark.green)
           * t.green;
      l3 = (t.correction->apply (l3, x, y,
                                 backlight_correction_parameters::blue)
            - t.dark.blue)
           * t.blue;
    }
  return l1 + l2 + l3;
}

/* Compute the three independently corrected source channels at X, Y.  T uses
   the same lookup/backlight machinery as grayscale conversion; channel weights
   and dark values remain available so this helper exactly mirrors the old
   three scalar passes when they are set to unit/zero values.  */
inline rgbdata
compute_rgb_data (gray_data_tables &t, int x, int y, int r, int g, int b)
{
  rgbdata ret = { t.rtable[r], t.gtable[g], t.btable[b] };
  if (t.correction)
    {
      ret.red = (t.correction->apply (ret.red, x, y,
                                      backlight_correction_parameters::red)
                 - t.dark.red)
                * t.red;
      ret.green
          = (t.correction->apply (ret.green, x, y,
                                  backlight_correction_parameters::green)
             - t.dark.green)
            * t.green;
      ret.blue = (t.correction->apply (ret.blue, x, y,
                                       backlight_correction_parameters::blue)
                  - t.dark.blue)
                 * t.blue;
    }
  return ret;
}

/* Helper parameters for grayscale data fetching.  */
struct getdata_params
{
  std::shared_ptr<luminosity_t[]> table = nullptr;
  backlight_correction *correction = nullptr;
  int width = 0, height = 0;
};

/* Helper for sharpening template for images with gray data with no correction.
   Fetch pixel from GRAYDATA at X, Y using parameters D.  */
inline luminosity_t
getdata_helper_no_correction (unsigned short *graydata, int_point_t p, int,
                              getdata_params &d)
{
  if (colorscreen_checking)
    assert (p.x >= 0 && p.x < d.width && p.y >= 0 && p.y < d.height);
  return d.table[*(graydata+p.y * (uint64_t)d.width + p.x)];
}

/* Helper for sharpening template for images with gray data with correction.
   Fetch pixel from GRAYDATA at X, Y using parameters D.  */
inline luminosity_t
getdata_helper_correction (uint16_t *graydata, int_point_t p, int,
                           getdata_params &d)
{
  if (colorscreen_checking)
    assert (p.x >= 0 && p.x < d.width && p.y >= 0 && p.y < d.height);
  luminosity_t v = d.table[*(graydata+p.y * (uint64_t)d.width + p.x)];
  v = d.correction->apply (v, p.x, p.y, backlight_correction_parameters::ir);
  return v;
}

/* Helper for sharpening template for images with RGB data only.
   Fetch pixel from IMG at X, Y using tables T.  */
inline luminosity_t
getdata_helper2 (const image_data *img, int_point_t p, int, gray_data_tables &t)
{
  image_data::pixel pxl = img->get_rgb_pixel (p.x, p.y);
  return compute_gray_data (
      t, p.x, p.y, pxl.r,
      pxl.g, pxl.b);
}

/* Fetch one linearized/backlight-corrected native RGB pixel from IMG.  */
inline rgbdata
getrgbdata_helper (const image_data *img, int_point_t p, int,
                   gray_data_tables &t)
{
  image_data::pixel pxl = img->get_rgb_pixel (p.x, p.y);
  return compute_rgb_data (t, p.x, p.y, pxl.r, pxl.g, pxl.b);
}

/* Create new grayscale and sharpened data using parameters P.
   Report progress to PROGRESS.  */
std::unique_ptr<sharpened_data>
get_new_gray_sharpened_data (gray_and_sharpen_params &p,
                             progress_info *progress)
{
  auto ret = std::make_unique<sharpened_data> (p.gp.img->width, p.gp.img->height);
  if (!ret || !ret->m_data)
      return nullptr;
  mem_luminosity_t *out = ret->m_data;

  bool ok;
  if (p.gp.img->has_grayscale_or_ir () && !p.gp.ignore_infrared)
    {
      lookup_table_params par;
      getdata_params d;
      par.maxval = p.gp.img->maxval;
      par.gamma = p.gp.gamma;
      d.table = lookup_table_cache.get (par, progress);
      d.correction = p.gp.backlight;
      d.width = p.gp.img->width;
      d.height = p.gp.img->height;
      if (!d.table)
          return nullptr;
      if (d.correction)
        {
          if (p.sp.deconvolution_p ())
            {
              ok = deconvolve<luminosity_t, mem_luminosity_t,
                                uint16_t *, getdata_params &,
                                getdata_helper_correction> (
                  out, (uint16_t *)p.gp.img->get_data_ptr (), d, p.gp.img->width, p.gp.img->height,
		  p.sp, progress, true);
            }
          else
            ok = sharpen<luminosity_t, mem_luminosity_t, uint16_t *,
                         getdata_params &, getdata_helper_correction> (
                out, (uint16_t *)p.gp.img->get_data_ptr (), d, p.gp.img->width, p.gp.img->height,
                p.sp.get_mode () == sharpen_parameters::none ? 0 : p.sp.usm_radius,
	       	p.sp.usm_amount, progress);
        }
      else if (p.sp.deconvolution_p ())
        {
          ok = deconvolve<luminosity_t, mem_luminosity_t, uint16_t *,
                           getdata_params &, getdata_helper_no_correction> (
              out, p.gp.img->get_data_ptr (), d, p.gp.img->width, p.gp.img->height,
	      p.sp, progress, true);
        }
      else
        ok = sharpen<luminosity_t, mem_luminosity_t, uint16_t *,
                     getdata_params &, getdata_helper_no_correction> (
            out, p.gp.img->get_data_ptr (), d, p.gp.img->width, p.gp.img->height,
            p.sp.get_mode () == sharpen_parameters::none ? 0 : p.sp.usm_radius,
            p.sp.usm_amount, progress);
    }
  else
    {
      gray_data_tables t
          = compute_gray_data_tables (p.gp, p.gp.backlight != nullptr, progress);
      if (!t.rtable)
        ok = false;
      else
        {
          t.correction = p.gp.backlight;
          if (p.sp.deconvolution_p ())
            {
              ok = deconvolve<luminosity_t, mem_luminosity_t,
                                const image_data *, gray_data_tables &,
                                getdata_helper2> (
                  out, p.gp.img, t, p.gp.img->width, p.gp.img->height,
		  p.sp, progress, true);
            }
          else
            ok = sharpen<luminosity_t, mem_luminosity_t, const image_data *,
                         gray_data_tables &, getdata_helper2> (
                out, p.gp.img, t, p.gp.img->width, p.gp.img->height,
		p.sp.get_mode () == sharpen_parameters::none ? 0 : p.sp.usm_radius,
                p.sp.usm_amount, progress);
        }
    }
  if (!ok)
      return nullptr;
  return ret;
}

/* Create one interleaved, scanner-sharpened native RGB image.  Source pixels
   are fetched once per pass.  Deconvolution uses independent effective MTF
   parameters for the three scanner channels while unsharp masking naturally
   operates on the RGB vector with the common radius and amount.  */
std::unique_ptr<sharpened_rgb_data>
get_new_rgb_sharpened_data (rgb_and_sharpen_params &p,
                            progress_info *progress)
{
  auto ret = std::make_unique<sharpened_rgb_data> (p.gp.img->width,
                                                   p.gp.img->height);
  if (!ret || !ret->m_data)
    return nullptr;

  gray_data_tables t
      = compute_gray_data_tables (p.gp, p.gp.backlight != nullptr, progress);
  if (!t.rtable)
    return nullptr;
  t.correction = p.gp.backlight;

  bool ok;
  if (p.sp[0].deconvolution_p ())
    ok = deconvolve_rgb<rgbdata, mem_rgbdata, const image_data *,
                        gray_data_tables &, getrgbdata_helper> (
        ret->m_data, p.gp.img, t, p.gp.img->width, p.gp.img->height, p.sp,
        progress, true);
  else
    ok = sharpen<rgbdata, mem_rgbdata, const image_data *, gray_data_tables &,
                 getrgbdata_helper> (
        ret->m_data, p.gp.img, t, p.gp.img->width, p.gp.img->height,
        p.sp[0].get_mode () == sharpen_parameters::none ? 0
                                                        : p.sp[0].usm_radius,
        p.sp[0].usm_amount, progress, true);
  if (!ok)
    return nullptr;
  return ret;
}
} // anonymous namespace

/* Prune render cache.  We need to do this so destruction order of MapAlloc and
   the cache does not yield a segfault.  */
void
prune_render_caches ()
{
  gray_and_sharpened_data_cache.prune ();
  rgb_and_sharpened_data_cache.prune ();
  lookup_table_cache.prune ();
  backlight_correction_cache.prune ();
  image_layer_histogram_cache.prune ();
}

/*****************************************************************************/
/*                             render implementation                         */
/*****************************************************************************/

/* Precompute data selected by FLAGS.  PATCH_PROPORTIONS controls output-color
   setup and PROGRESS reports work and cancellation.  */
bool
render::precompute_all (int flags, rgbdata patch_proportions,
                        progress_info *progress)
{
  const bool image_layer_needed = flags & PRECOMPUTE_IMAGE_LAYER;
  const bool rgb_image_needed = flags & PRECOMPUTE_RGB_IMAGE;
  const bool normalized_patches = flags & NORMALIZED_PATCHES;
  if (rgb_image_needed && !m_img.has_rgb ())
    return false;

  if (m_params.backlight_correction)
    {
      backlight_correction_cache_params p
          = { m_params.backlight_correction.get (),
              m_params.backlight_correction->id,
              m_img.width,
              m_img.height,
              m_params.backlight_correction_black,
              true };
      m_backlight_correction = backlight_correction_cache.get (
          p, progress, &m_backlight_correction_id);
      if (!m_backlight_correction)
        return false;
    }
  if (m_img.has_rgb ())
    {
      lookup_table_params par;
      par.maxval = m_img.maxval;
      par.gamma = m_params.gamma;
      if (par.gamma == 0)
        par.gamma_table = m_img.to_linear[0];
      m_rgb_lookup_table[0] = lookup_table_cache.get (par, progress);
      if (!m_rgb_lookup_table[0])
        return false;
      if (par.gamma == 0)
        {
          par.gamma_table = m_img.to_linear[1];
          m_rgb_lookup_table[1] = lookup_table_cache.get (par, progress);
          if (!m_rgb_lookup_table[1])
            return false;
          par.gamma_table = m_img.to_linear[2];
          m_rgb_lookup_table[2] = lookup_table_cache.get (par, progress);
          if (!m_rgb_lookup_table[2])
            return false;
        }
      else
        {
          m_rgb_lookup_table[1] = lookup_table_cache.get (par, progress);
          m_rgb_lookup_table[2] = lookup_table_cache.get (par, progress);
          if (!m_rgb_lookup_table[1] || !m_rgb_lookup_table[2])
            return false;
        }
    }

  const bool ir_simulation
      = !m_img.has_grayscale_or_ir ()
        || (m_img.has_rgb () && m_params.ignore_infrared);
  const bool scanner_sharpening
      = m_img.has_rgb ()
        && m_params.sharpen.get_mode () != sharpen_parameters::none;
  const int image_layer_native_channel
      = image_layer_needed && ir_simulation
            ? m_params.get_image_layer_native_channel (&m_img)
            : -1;
  const bool one_channel_rgb_image_layer
      = image_layer_native_channel >= 0 && image_layer_native_channel < 3;

  /* Scanner sharpening belongs to native capture channels, before any RGB
     mixture is formed.  A genuine RGB mixture needs all three channels, but an
     exact one-channel image-layer mix can avoid that work unless RGB output is
     itself requested.  If the identical full RGB result already exists, reuse
     it without starting another computation.  */
  if (scanner_sharpening)
    {
      rgb_and_sharpen_params p
          = { { m_img.id,
                &m_img,
                m_params.gamma,
                { m_img.to_linear[0], m_img.to_linear[1],
                  m_img.to_linear[2] },
                rgbdata{0, 0, 0},
                1.0f,
                1.0f,
                1.0f,
                m_backlight_correction.get (),
                m_backlight_correction_id,
                true },
              { m_params.get_sharpen_parameters_for_channel (0),
                m_params.get_sharpen_parameters_for_channel (1),
                m_params.get_sharpen_parameters_for_channel (2) } };
      const bool require_full_rgb
          = rgb_image_needed
            || (image_layer_needed && ir_simulation
                && !one_channel_rgb_image_layer);
      if (require_full_rgb)
        m_rgb_image_holder = rgb_and_sharpened_data_cache.get (p, progress);
      else if (image_layer_needed && ir_simulation
               && one_channel_rgb_image_layer)
        m_rgb_image_holder = rgb_and_sharpened_data_cache.peek (p);

      if (m_rgb_image_holder)
        {
          m_rgb_image = m_rgb_image_holder->m_data;
          if (colorscreen_checking)
            assert (m_rgb_image);
        }
      else if (require_full_rgb)
        return false;
    }

  if (image_layer_needed)
    {
      if (ir_simulation && m_rgb_image)
        {
          /* RGB-derived image layers are mixed from already-sharpened native
             channels.  Mixing first and deconvolving afterwards would impose
             one transfer function on three spectrally different channels.  */
          m_image_layer_holder
              = std::make_shared<sharpened_data> (m_img.width, m_img.height);
          if (!m_image_layer_holder->m_data)
            return false;
          m_image_layer = m_image_layer_holder->m_data;
          const size_t size = (size_t)m_img.width * m_img.height;
#pragma omp parallel for
          for (size_t i = 0; i < size; ++i)
            {
              const rgbdata pixel = m_rgb_image[i];
              m_image_layer[i]
                  = (pixel.red - m_params.mix_dark.red) * m_params.mix_red
                    + (pixel.green - m_params.mix_dark.green)
                          * m_params.mix_green
                    + (pixel.blue - m_params.mix_dark.blue)
                          * m_params.mix_blue;
            }
          m_image_layer_id = lru_caches::get ();
        }
      else if (ir_simulation && scanner_sharpening
               && one_channel_rgb_image_layer)
        {
          /* No full RGB result was requested or cached.  Sharpen only the
             native channel which actually contributes to the image layer.
             Keep mix-dark and gain after sharpening so this is equivalent to
             the full RGB sharpen-then-mix path even for Wiener/RL filters.  */
          const int channel = image_layer_native_channel;
          gray_and_sharpen_params p
              = { { m_img.id,
                    &m_img,
                    m_params.gamma,
                    { m_img.to_linear[0], m_img.to_linear[1],
                      m_img.to_linear[2] },
                    rgbdata{0, 0, 0},
                    channel == 0 ? 1.0f : 0.0f,
                    channel == 1 ? 1.0f : 0.0f,
                    channel == 2 ? 1.0f : 0.0f,
                    m_backlight_correction.get (),
                    m_backlight_correction_id,
                    true },
                  m_params.get_sharpen_parameters_for_channel (channel) };
          uint64_t channel_id = 0;
          std::shared_ptr<sharpened_data> channel_holder
              = gray_and_sharpened_data_cache.get (p, progress, &channel_id);
          if (!channel_holder)
            return false;

          const luminosity_t weight
              = channel == 0 ? m_params.mix_red
                : channel == 1 ? m_params.mix_green
                               : m_params.mix_blue;
          const luminosity_t dark
              = channel == 0 ? m_params.mix_dark.red
                : channel == 1 ? m_params.mix_dark.green
                               : m_params.mix_dark.blue;
          if (weight == 1 && dark == 0)
            {
              m_image_layer_holder = channel_holder;
              m_image_layer = channel_holder->m_data;
              m_image_layer_id = channel_id;
            }
          else
            {
              m_image_layer_holder
                  = std::make_shared<sharpened_data> (m_img.width, m_img.height);
              if (!m_image_layer_holder->m_data)
                return false;
              m_image_layer = m_image_layer_holder->m_data;
              const size_t size = (size_t)m_img.width * m_img.height;
#pragma omp parallel for
              for (size_t i = 0; i < size; ++i)
                m_image_layer[i]
                    = ((luminosity_t)channel_holder->m_data[i] - dark) * weight;
              m_image_layer_id = lru_caches::get ();
            }
        }
      else
        {
          sharpen_parameters image_layer_sharpen = m_params.sharpen;
          /* A native scalar plane uses scanner slot 3.  With RGB present this
             is infrared; standalone grayscale instead uses the visible-light
             fallback wavelength.  */
          if (!ir_simulation)
            image_layer_sharpen = m_params.get_sharpen_parameters_for_channel (
                3, m_img.has_rgb ());

          gray_and_sharpen_params p
              = { { m_img.id,
                    &m_img,
                    m_params.gamma,
                    { m_img.to_linear[0], m_img.to_linear[1],
                      m_img.to_linear[2] },
                    ir_simulation ? m_params.mix_dark
                                  : rgbdata{1.0f, 1.0f, 1.0f},
                    ir_simulation ? m_params.mix_red : 1.0f,
                    ir_simulation ? m_params.mix_green : 1.0f,
                    ir_simulation ? m_params.mix_blue : 1.0f,
                    m_backlight_correction.get (),
                    m_backlight_correction_id,
                    ir_simulation ? m_params.ignore_infrared : false },
                  image_layer_sharpen };
          m_image_layer_holder
              = gray_and_sharpened_data_cache.get (p, progress,
                                                    &m_image_layer_id);
          if (!m_image_layer_holder)
            return false;
          m_image_layer = m_image_layer_holder->m_data;
        }
    }

  if (m_params.contact_copy.simulate)
    {
      m_sensitivity_hd_curve = std::make_unique <richards_hd_curve> (
          100, m_params.contact_copy.emulsion_characteristic_curve);
      m_sensitivity = std::make_unique <film_sensitivity> (
          m_sensitivity_hd_curve.get (), m_params.contact_copy.preflash,
          m_params.contact_copy.exposure, m_params.contact_copy.boost);
      m_sensitivity->precompute ();
    }
  if (m_sensitivity)
    {
      luminosity_t lmin=-0.1;
      luminosity_t lmax=2;
      const int steps = 65536;
      luminosity_t yvals[steps];
      for (int i = 0; i < steps; i++)
        yvals[i] = adjust_luminosity_ir (
            i * (lmax - lmin) / (steps - 1) + lmin);
      m_adjust_luminosity
          = std::make_unique<precomputed_function<luminosity_t>> (
              lmin, lmax, yvals, steps);
    }
  return out_color.precompute (m_params, m_output, &m_img, normalized_patches,
                               patch_proportions, progress);
}

#if 0
/* Precompute data for given AREA.  */
bool
render::precompute_img_range (int_image_area area, progress_info *progress)
{
  (void)area;
  return precompute_all (PRECOMPUTE_IMAGE_LAYER, {1.0/3, 1.0/3, 1.0/3}, progress);
}
#endif

/* Compute lookup table converting image_data to range 0..1 with GAMMA.  */
bool
render::get_lookup_tables (std::shared_ptr<luminosity_t[]> *ret,
                           luminosity_t gamma, const image_data *img,
                           progress_info *progress)
{
  lookup_table_params par;
  par.gamma = gamma;
  par.maxval = img->maxval;

  if (par.gamma == 0)
    par.gamma_table = img->to_linear[0];
  ret[0] = lookup_table_cache.get (par, progress);
  if (!ret[0])
    return false;
  if (par.gamma != 0)
  {
    ret[1] = lookup_table_cache.get (par, progress);
    ret[2] = lookup_table_cache.get (par, progress);
  }
  else
    {
      par.gamma_table = img->to_linear[1];
      ret[1] = lookup_table_cache.get (par, progress);
      if (!ret[1])
        {
          ret[0] = nullptr;
          return false;
        }
      par.gamma_table = img->to_linear[2];
      ret[2] = lookup_table_cache.get (par, progress);
      if (!ret[2])
        {
          ret[0] = nullptr;
          ret[1] = nullptr;
          return false;
        }
    }
  return true;
}

/* Compute grayscale data for downscaled region at X, Y with WIDTH,
   HEIGHT and PIXELSIZE.  Store result in DATA.  Report progress
   to PROGRESS.  Return false on failure or cancellation.  */
bool
render::get_gray_data (luminosity_t *data, point_t p, int width,
                       int height, coord_t pixelsize, progress_info *progress)
{
  return downscale<render, luminosity_t, &render::get_data> (
      data, p, width, height, pixelsize, progress);
}

/* Compute color data for downscaled region at X, Y with WIDTH, HEIGHT
   and PIXELSIZE.  Store result in DATA.  Report progress
   to PROGRESS.  Return false on failure or cancellation.  */
bool
render::get_color_data (rgbdata *data, point_t p, int width,
                        int height, coord_t pixelsize, progress_info *progress)
{
  return downscale<render, rgbdata, &render::get_rgb_pixel> (
      data, p, width, height, pixelsize, progress);
}

/* Sample square patch with center C and corner offsets P1, P2.  */
luminosity_t
render::sample_img_square (point_t c, point_t p1, point_t p2) const
{
  luminosity_t acc = 0, weights = 0;
  coord_t x_min_val = std::min ({c.x - p1.x, c.x + p1.x, c.x - p2.x, c.x + p2.x});
  coord_t x_max_val = std::max ({c.x - p1.x, c.x + p1.x, c.x - p2.x, c.x + p2.x});
  int xmin = std::max ((int)(x_min_val - (coord_t)0.5), 0);
  int xmax = std::min ((int)ceil (x_max_val + (coord_t)0.5), m_img.width - 1);

  /* If the resolution is too small, just sample given point.  */
  if (xmax - xmin < 2)
    return get_img_pixel ({c.x, c.y});

  /* For bigger resolution we can sample few points in the square.  */
  if (xmax - xmin < 6)
    {
      int samples = (int)my_floor (my_sqrt (p1.x * p1.x + p1.y * p1.y) + (coord_t)0.5) * 2;
      if (!samples)
        return get_img_pixel ({c.x, c.y});
      luminosity_t rec = (luminosity_t)1.0 / samples;
      for (int y = -samples; y <= samples; y++)
        for (int x = -samples; x <= samples; x++)
          {
            int w = 1 + (samples - abs (x) - abs (y));
            if (w < 0)
              continue;
            acc += (luminosity_t)w * get_img_pixel ({c.x + (p1.x * x + p2.x * y) * rec,
                                                    c.y + (p1.y * x + p2.y * y) * rec});
            weights += (luminosity_t)w;
          }
    }
  /* Faster version of the above which does not need multiple calls to get_img_pixel.
     It however may suffer from banding when spots are too small.  */
  else
    {
      coord_t y_min_val = std::min ({c.y - p1.y, c.y + p1.y, c.y - p2.y, c.y + p2.y});
      coord_t y_max_val = std::max ({c.y - p1.y, c.y + p1.y, c.y - p2.y, c.y + p2.y});
      int ymin = std::max ((int)(y_min_val - (coord_t)0.5), 0);
      int ymax = std::min ((int)ceil (y_max_val + (coord_t)0.5), m_img.height - 1);
      matrix2x2<coord_t> base (p1.x, p2.x, p1.y, p2.y);
      matrix2x2<coord_t> inv = base.invert ();
      for (int y = ymin; y <= ymax; y++)
        {
          for (int x = xmin; x <= xmax; x++)
            {
              coord_t cx = (coord_t)x + (coord_t)0.5 - c.x;
              coord_t cy = (coord_t)y + (coord_t)0.5 - c.y;
              coord_t ccx, ccy;
              inv.apply_to_vector (cx, cy, &ccx, &ccy);
              luminosity_t w = (luminosity_t)(fabs (ccx) + fabs (ccy));

              if (w < 1)
                {
                  w = (luminosity_t)1.0 - w;
                  acc += w * get_data ({x, y});
                  weights += w;
                }
            }
        }
    }
  if (weights > 0)
    return acc / weights;
  return 0;
}

/* Increase capacity of stitch related caches to N.  */
void
render_increase_lru_cache_sizes_for_stitch_projects (int n)
{
  gray_and_sharpened_data_cache.increase_capacity (n);
  rgb_and_sharpened_data_cache.increase_capacity (n);
}

/* Get linearized pixel at XX, YY with given RANGE.  */
rgbdata
get_linearized_pixel (const image_data &img, render_parameters &rparam, int xx,
                      int yy, int range, progress_info *progress)
{
  rgbdata color = { 0, 0, 0 };
  int n = 0;
  const image_data *imgp = &img;
  if (img.stitch)
    {
      int tx, ty;
      point_t scr = img.stitch->common_scr_to_img.final_to_scr (
          { (coord_t)(xx + img.xmin), (coord_t)(yy + img.ymin) });
      if (!img.stitch->tile_for_scr (nullptr, scr.x, scr.y, &tx, &ty, true))
        return color;
      point_t p = img.stitch->images[ty][tx].common_scr_to_img (scr);
      xx = nearest_int (p.x);
      yy = nearest_int (p.y);
      imgp = img.stitch->images[ty][tx].img.get ();
    }
  render r (*imgp, rparam, 255);
  const int flags
      = imgp->has_rgb () ? PRECOMPUTE_RGB_IMAGE : PRECOMPUTE_IMAGE_LAYER;
  if (!r.precompute_all (flags,
                         { 1.0f / 3.0f, 1.0f / 3.0f, 1.0f / 3.0f },
                         progress))
    return rgbdata(0, 0, 0);
  for (int y = yy - range; y < yy + range; y++)
    for (int x = xx - range; x < xx + range; x++)
      if (x >= 0 && x < img.width && y >= 0 && y < img.height)
        {
          if (img.has_rgb ())
            color += r.get_linearized_rgb_pixel ({x, y});
          else
            {
              rgbdata color2 = { 1, 1, 1 };
              color += color2 * r.get_unadjusted_data ({x, y});
            }
          n++;
        }
  return n ? color / (float)n : color;
}

/* Convert HD Y values to RGB.  */
std::vector <rgbdata>
hd_y_to_rgb (render_parameters &rparam, int steps, luminosity_t miny, luminosity_t maxy, rgbdata patch_proportions, hd_axis_type axis_type)
{
  out_color_adjustments a (256);
  render_output_parameters output;
  if (!a.precompute (rparam, output, nullptr, false, patch_proportions,
                     nullptr))
    return {};
  std::vector <rgbdata> data (steps);
  for (int i = 0 ; i < steps; i++)
  {
    luminosity_t y = i * (maxy - miny) / (steps - 1) + miny;
    y = hd_axis_y_to_linear (y, rparam.contact_copy.boost, axis_type);
    int_rgbdata out = a.final_color ({y, y, y});
    data[i].red = out.red;
    data[i].green = out.green;
    data[i].blue = out.blue;
  }
  return data;
}

/* Fetch histogram for the current scan area.  */
std::shared_ptr<histogram>
render::get_image_layer_histogram (progress_info *progress)
{
  image_layer_histogram_params p = {m_image_layer_id, m_params.get_scan_crop (m_img.width, m_img.height), this};
  return image_layer_histogram_cache.get (p, progress);
}

/* Compute histogram for HD X axis.  */
std::vector<uint64_t>
hd_x_histogram (render_parameters &rparam, image_data &img, int steps, luminosity_t minx, luminosity_t maxx, hd_axis_type axis_type, progress_info *progress)
{
  /* TODO: Handle stitched projects.  */
  if (img.stitch)
    return {};
  render r (img, rparam, 256);
  if (!r.precompute_all (PRECOMPUTE_IMAGE_LAYER, {1, 1, 1}, progress))
    return {};
  auto hist = r.get_image_layer_histogram (progress);
  if (!hist)
    return {};
  std::vector<uint64_t> data (steps);
  for (size_t i = 0 ; i < hist->n_entries (); i++)
    {
      luminosity_t xv = hist->index_to_val (i);
      xv = (xv - rparam.dark_point) * rparam.scan_exposure;
      luminosity_t x = hd_linear_to_axis_x (xv, axis_type, rparam.contact_copy.preflash, rparam.contact_copy.exposure);
      if (x < minx || x > maxx)
	continue;
      int idx = nearest_int ((x - minx) * (steps-1) / (maxx - minx));
      if (idx == steps)
	idx = steps-1;
      data[idx] += hist->entry (i);
    }
  return data;
}
}
