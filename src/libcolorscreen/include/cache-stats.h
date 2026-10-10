#ifndef COLORSCREEN_CACHE_STATS_H
#define COLORSCREEN_CACHE_STATS_H

#include "dllpublic.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace colorscreen
{

/* An instantaneous, read-only view of a core LRU. Retained bytes account for
   references owned by the cache, not independent renderer/view references.
   Caches without a custom size callback provide only a lower-bound estimate
   (sizeof(T) per entry); legacy unbounded-array caches have unknown size. */
struct cache_entry_statistics
{
  std::string name;
  size_t entries = 0;
  size_t in_progress = 0;
  size_t externally_pinned = 0;
  uint64_t retained_bytes = 0;
  bool precise_size = false;
};

/* Host memory availability and process-wide cache policy. Unknown system
   readings are zero. The global byte budget is a soft limit for cache-owned
   references, never a limit on the memory needed by a live photograph. */
struct cache_memory_statistics
{
  uint64_t total_memory_bytes = 0;
  uint64_t available_memory_bytes = 0;
  uint64_t cache_budget_bytes = 0;
  uint64_t cached_bytes = 0;
  uint64_t retained_raw_source_bytes = 0;
  std::vector<cache_entry_statistics> caches;
};

/* Thread-safe diagnostics shared by Qt, CLI and future frontends. No Qt
   types, QObjects or mutable frontend state enter the core cache registry. */
DLL_PUBLIC cache_memory_statistics get_cache_memory_statistics ();

/* Query the available-memory-based global cache budget. The budget adapts
   to installed RAM, current free/available memory and cache-held allocations. */
DLL_PUBLIC uint64_t cache_memory_budget_bytes ();

}

#endif
