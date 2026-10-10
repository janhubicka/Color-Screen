#ifndef LRU_CACHE_H
#define LRU_CACHE_H
#include <mutex>
#include <chrono>
#include <thread>
#include <atomic>
#include <memory>
#include <cstddef>
#include <condition_variable>
#include <type_traits>
#include <limits>
#include <vector>
#include "include/cache-stats.h"
#include "include/progress-info.h"
#include "include/dllpublic.h"

namespace colorscreen
{

/* Class used to generate unique identifiers for cached entries.  */
class lru_caches
{
public:
  constexpr
  lru_caches ()
  {
  }
  /* Return a new unique identifier.  */
  static uint64_t
  get ()
  {
    return std::atomic_fetch_add (&time, 1);
  }

private:
  DLL_PUBLIC static std::atomic_uint64_t time;
};
extern class lru_caches lru_caches;

/* The non-template registry coordinates eviction and read-only statistics
   across every simple and tiled LRU in libcolorscreen. Its implementation
   lives in the core library, never in a frontend. The registry lock is always
   acquired BEFORE a cache mutex; caches notify it only after unlocking their
   own mutex. Returned evicted references are destroyed after releasing the
   registry lock, so destructors cannot reenter cache pruning under a lock. */
class tracked_lru_cache
{
public:
  virtual ~tracked_lru_cache () = default;
  virtual cache_entry_statistics cache_statistics (uint64_t *oldest) = 0;
  virtual std::shared_ptr<void> discard_oldest_cache_entry () = 0;
};

class DLL_PUBLIC lru_cache_registry
{
public:
  static lru_cache_registry &instance ();
  void register_cache (tracked_lru_cache *);
  void unregister_cache (tracked_lru_cache *);
  cache_memory_statistics snapshot ();
  void enforce_budget (uint64_t extra_bytes = 0);

private:
  std::mutex registry_mutex;
  std::vector<tracked_lru_cache *> caches;
};

/* Reserve/release unpacked Bayer CFA memory against the same global cache
   policy. Reserving requires no Qt-specific memory service. Release is atomic
   so a retired image can die while another cache lock is still held. */
DLL_PUBLIC bool reserve_raw_source_cache_bytes (uint64_t bytes);
DLL_PUBLIC void release_raw_source_cache_bytes (uint64_t bytes);
DLL_PUBLIC uint64_t raw_source_cache_bytes ();

/* LRU cache used keep various data between invocations of renderers.
   P represents parameters which are used to produce T.
   get_new is a function computing T based on P. It is expected to
   allocate memory via std::unique_ptr and the cache will manage it.

   Template Architecture:
   The implementation uses a template-based design to support different
   types of keys and values while sharing common logic via abstract_lru_cache.
   - P: Parameter type (key)
   - T: Result type (value)
   - Entry: The struct type representing a cache entry
   - Derived: The final cache class (CRTP) used to fetch base configuration

   Synchronization Model:
   The cache uses a combination of a global lock (std::mutex) and per-entry
   state ("computing" flag) to ensure thread-safety while allowing expensive
   cache entries to be generated concurrently.

   1. Lock Granularity: The global "lock" protects the integrity of the linked list
      ("entries") and metadata (last_used, id). Cache metadata access is short and
      serialized; long-running value computation happens without holding the lock.

   2. Non-blocking Computation: To prevent the cache from stalling the entire
      application during a long "get_new" call, the implementation:
        a) Identifies/allocates the target hit/miss while locked.
        b) Sets "computing = true" on the entry.
        c) Unlocks the global lock.
        d) Executes "get_new" concurrently.
        e) Re-locks to store the result and unset "computing".
      Other threads can freely access, add, or release independent entries
      while "get_new" is running.

   3. Race Condition Prevention:
        - Redundant Computation: If a second thread requests the same parameters
          while an entry is already marked "computing", it waits on a
          condition variable ("cond") until the first thread finishes.
          It does NOT start a second "get_new" call.
        - Premature Reuse: The "computing" flag ensures that "prune()" or lookup
          logic for "longest_unused" will skip entries currently being generated
          or updated out-of-lock.
        - Task Cancellation: A cancellable caller polls while waiting for the
          global mutex or another thread's computation, so cancellation remains
          observable without timed pthread synchronization primitives.  */

/* Base structure for cache entries. 
   P is the parameter type.
   T is the value type.
   ENTRY is the pointer type for the linked list.  */
template <typename P, typename T, typename Entry>
struct lru_entry_base
{
  P params;
  std::shared_ptr<T> val;
  Entry *next;
  uint64_t id;
  uint64_t last_used;
  bool computing = false;
  /* Bytes retained by the cache's strong ownership (not external pins).
     Zero for an in-flight entry or when no byte budget is configured. */
  size_t cached_bytes = 0;
};

/* RAII guard to manage the computing flag and notifications. 
   F is the computing flag to be reset.
   M is the mutex used for synchronization.
   C is the condition variable to notify waiting threads.  */
struct computing_guard
{
  bool &flag;
  std::mutex &mutex;
  std::condition_variable &cond;
  bool active;
  computing_guard (bool &f, std::mutex &m, std::condition_variable &c)
      : flag (f), mutex (m), cond (c), active (true)
  {
  }
  ~computing_guard ()
  {
    if (active)
      {
        std::unique_lock<std::mutex> guard (mutex);
        flag = false;
        cond.notify_all ();
      }
  }
  /* Mark the computation as successfully finished so that the 
     automatic flag reset in the destructor does not trigger a 
     re-lock if not needed (optional optimization).  */
  void
  finished ()
  {
    active = false;
  }
};

/* Consolidated LRU cache implementation baseline. 
   P is the parameter type.
   T is the value type.
   ENTRY is the entry structure.
   DERIVED is the final class type.  */
template <typename P, typename T, typename Entry, typename Derived>
class abstract_lru_cache : public tracked_lru_cache
{
  static const bool verbose = false;

protected:
  Entry *entries;
  int cache_size;
  std::mutex lock;
  std::condition_variable cond;
  const char *name;
  /* An optional cache-owned byte budget; external shared_ptr owners are
     deliberately excluded from this accounting. Zero disables this limit. */
  const size_t max_cached_bytes;
  size_t cached_bytes = 0;
  size_t (*value_bytes) (const T &);

  /* Initialize the cache with count and optional byte budgets. */
  abstract_lru_cache (const char *n, int base_size,
                      size_t max_bytes = 0,
                      size_t (*weight) (const T &) = nullptr)
      : entries (NULL), cache_size (base_size), name (n),
        max_cached_bytes (max_bytes), value_bytes (weight)
  {
    lru_cache_registry::instance ().register_cache (this);
  }

  cache_entry_statistics
  cache_statistics (uint64_t *oldest) override
  {
    std::unique_lock<std::mutex> guard (lock);
    cache_entry_statistics stat;
    stat.name = name ? name : "unnamed";
    stat.retained_bytes = cached_bytes;
    stat.precise_size = value_bytes != nullptr;
    uint64_t least_recent = std::numeric_limits<uint64_t>::max ();
    for (Entry *e = entries; e; e = e->next)
      {
        ++stat.entries;
        if (e->computing)
          ++stat.in_progress;
        if (e->val && e->val.use_count () > 1)
          ++stat.externally_pinned;
        if (e->val && !e->computing && e->last_used < least_recent)
          least_recent = e->last_used;
      }
    if (oldest)
      *oldest = least_recent;
    return stat;
  }

  /* Detach the oldest owned result, even if a renderer independently pins
     it. The aliasing shared_ptr defers any image destruction until after
     global registry locks have been released by the caller. */
  std::shared_ptr<void>
  discard_oldest_cache_entry () override
  {
    std::unique_lock<std::mutex> guard (lock);
    Entry **victim = nullptr;
    for (Entry **slot = &entries; *slot; slot = &(*slot)->next)
      if ((*slot)->val && !(*slot)->computing
          && (!victim || (*slot)->last_used < (*victim)->last_used))
        victim = slot;
    if (!victim)
      return {};
    Entry *old = *victim;
    *victim = old->next;
    cached_bytes -= old->cached_bytes;
    std::shared_ptr<void> detached (old->val, nullptr);
    delete old;
    return detached;
  }

  /* Release the least recently used completed entries until NEED bytes
     will fit. Even an externally pinned value can leave this cache; its
     shared_ptr remains alive for the owner, without counting towards the
     cache's retained-byte limit. Never remove an in-progress computation
     or the just-computed PROTECTED entry. Called with LOCK held. */
  void
  make_room_for_bytes (size_t need, Entry *protected_entry)
  {
    while (cached_bytes > max_cached_bytes - need)
      {
        Entry **victim = nullptr;
        for (Entry **slot = &entries; *slot; slot = &(*slot)->next)
          if ((*slot) != protected_entry && !(*slot)->computing
              && (*slot)->val
              && (!victim || (*slot)->last_used < (*victim)->last_used))
            victim = slot;
        if (!victim)
          break;
        Entry *old = *victim;
        *victim = old->next;
        cached_bytes -= old->cached_bytes;
        delete old;
      }
  }

  virtual ~abstract_lru_cache ()
  {
    lru_cache_registry::instance ().unregister_cache (this);
    prune ();
    if (entries)
      fprintf (stderr, "Claimed entries in cache %s. Leaking memory\n", name);
  }

  /* Internal lookup and management logic shared by all cache types. 
     P is the parameter set.
     PROGRESS is the progress info for task cancellation.
     ID_OUT will receive the unique ID of the entry.
     MATCH_FUNC is a predicate checking if an entry matches P.
     INIT_FUNC initializes a new entry for P.
     FETCH_FUNC produces the value T for P.  */
  template <typename Matcher, typename Init, typename Fetcher>
  std::shared_ptr<T>
  get_internal (P &p, progress_info *progress, uint64_t *id_out,
                bool *cache_hit, Matcher &&match_func, Init &&init_func,
                Fetcher &&fetch_func)
  {
    if (cache_hit)
      *cache_hit = false;
    uint64_t time = lru_caches::get ();
    Entry *longest_unused = NULL, *e;
    int size = 0;

    if (progress)
      progress->wait ("unlocking cache");

    std::unique_lock<std::mutex> guard (lock, std::defer_lock);
    if (!progress)
      guard.lock ();
    else
      {
        /* Do not use timed mutex acquisition here.  Some supported libstdc++
           versions implement it with pthread clock-lock APIs which LLVM TSan
           does not recognize as synchronization.  Cache metadata critical
           sections are short, so polling try_lock() preserves cancellation
           without relying on those timed primitives.  */
        while (!guard.try_lock ())
          {
            if (progress->cancel_requested ())
              return NULL;
            std::this_thread::sleep_for (std::chrono::milliseconds (1));
          }
      }
    time++;

  restart:
    size = 0;
    longest_unused = NULL;
    for (e = entries; e; e = e->next)
      {
        if (match_func (e))
          {
            while (e->computing)
              {
                uint64_t id_val = e->id;
                if (progress)
                  {
                    progress->wait ("waiting for other thread to finish computation");
                    /* Timed condition-variable waits have the same TSan issue
                       as timed mutex acquisition on older libstdc++.  Poll only
                       for cancellable callers; ordinary callers can wait on the
                       condition variable without a timeout.  */
                    guard.unlock ();
                    std::this_thread::sleep_for (std::chrono::milliseconds (1));
                    guard.lock ();
                    if (progress->cancel_requested ())
                      return NULL;
                  }
                else
                  cond.wait (guard);

                bool found = false;
                for (Entry *e2 = entries; e2; e2 = e2->next)
                  if (e2 == e && e2->id == id_val)
                    {
                      found = true;
                      break;
                    }
                if (!found)
                  goto restart;
              }
            e->last_used = time;
            if (verbose)
              fprintf (stderr, "Cache %s: hit id %i\n", name, (int)e->id);
            std::shared_ptr<T> ret = e->val;
            if (id_out)
              *id_out = e->id;
            if (cache_hit)
              *cache_hit = (bool)ret;
            return ret;
          }
        if (e->val.use_count () <= 1 && !e->computing
            && (!longest_unused || e->last_used < longest_unused->last_used))
          longest_unused = e;
        size++;
      }

    if (size >= cache_size && longest_unused)
      {
        e = longest_unused;
        if (verbose)
          fprintf (stderr, "Cache %s: deleting id %i\n", name, (int)e->id);
        cached_bytes -= e->cached_bytes;
        e->cached_bytes = 0;
        e->val = nullptr;
      }
    else
      {
        e = new Entry;
        e->next = entries;
        entries = e;
      }

    e->params = p;
    init_func (e);
    e->computing = true;
    e->id = time;
    e->last_used = time;

    guard.unlock ();
    std::shared_ptr<T> ret_val;
    {
      computing_guard cguard (e->computing, lock, cond);
      std::unique_ptr<T> val = fetch_func (e);
      guard.lock ();
      e->val = std::move (val);
      e->computing = false;
      ret_val = e->val;
      cguard.finished ();
    }
    /* Preserve the return value independently of cache ownership. A result
       larger than the byte budget is useful to its caller but is not cached.
       Releasing cache ownership of an externally pinned victim does not
       invalidate that owner's shared_ptr. */
    const uint64_t entry_id = e->id;
    if (ret_val)
      {
        /* Historical caches of float[] remain count-only: an unbounded
           array has neither sizeof(T) nor a generic per-value size. For
           every ordinary object, track at least its descriptor size, or use
           the supplied complete byte-size callback. This measurement is
           also visible in the global statistics panel. */
        size_t weight = 0;
        if constexpr (!std::is_array<T>::value)
          weight = value_bytes ? value_bytes (*ret_val) : sizeof (T);
        if (!max_cached_bytes || weight <= max_cached_bytes)
          {
            if (max_cached_bytes)
              make_room_for_bytes (weight, e);
            e->cached_bytes = weight;
            cached_bytes += weight;
          }
        else
          {
            /* The caller receives a completed oversized result even when
               the cache itself cannot retain it. */
            for (Entry **slot = &entries; *slot; slot = &(*slot)->next)
              if (*slot == e)
                {
                  *slot = e->next;
                  delete e;
                  break;
                }
          }
      }
    if (!ret_val)
      {
        for (Entry **slot = &entries; *slot; slot = &(*slot)->next)
          if (*slot == e)
            {
              *slot = e->next;
              delete e;
              break;
            }
      }
    cond.notify_all ();

    if (id_out)
      *id_out = entry_id;
    if (verbose && ret_val)
      fprintf (stderr, "Cache %s: added id %i size %i\n", name,
               (int)entry_id, (int)size);

    /* Global eviction may inspect *other* cache instances. Never take the
       registry lock while holding this cache's mutex. The returned image
       already has its own shared_ptr so even immediate eviction is safe. */
    guard.unlock ();
    if (ret_val)
      lru_cache_registry::instance ().enforce_budget ();
    return ret_val;
  }

public:
  /* Remove all unused entries from the cache.  */
  void
  prune ()
  {
    std::unique_lock<std::mutex> guard (lock);
    Entry **e;
    for (e = &entries; *e;)
      {
        if ((*e)->val.use_count () <= 1 && !(*e)->computing)
          {
            if (verbose)
              fprintf (stderr, "Cache %s: deleting id %i\n", name, (int)(*e)->id);
            Entry *next = (*e)->next;
            cached_bytes -= (*e)->cached_bytes;
            delete (*e);
            (*e) = next;
          }
        else
          e = &(*e)->next;
      }
  }

  /* Current cache-owned memory footprint. This excludes externally
     retained values which are never invalidated by eviction. */
  size_t
  retained_bytes ()
  {
    std::unique_lock<std::mutex> guard (lock);
    return cached_bytes;
  }

  /* Configured byte budget; zero denotes entry-count-only caching. */
  size_t
  byte_capacity () const
  {
    return max_cached_bytes;
  }

  /* Increase the capacity of the cache to N times the base size.  */
  void
  increase_capacity (int n)
  {
    std::unique_lock<std::mutex> guard (lock);
    cache_size = n * Derived::base_size_const;
  }
};

/* Standard cache entry for simple parameter mapping.  */
template <typename P, typename T>
struct lru_cache_entry : lru_entry_base<P, T, lru_cache_entry<P, T>> {};

/* Simple LRU cache for 1-to-1 parameter mappings.
   P is the parameter type.
   T is the result type.
   GET_NEW is the generator function.
   BASE_CACHE_SIZE is the default size.  */
template <typename P, typename T,
          std::unique_ptr<T> (*get_new) (P &, progress_info *) = nullptr,
          int base_cache_size = 4>
class lru_cache : public abstract_lru_cache<P, T, lru_cache_entry<P, T>, lru_cache<P, T, get_new, base_cache_size>>
{
  using Entry = lru_cache_entry<P, T>;
  using Base = abstract_lru_cache<P, T, Entry, lru_cache<P, T, get_new, base_cache_size>>;

public:
  static constexpr int base_size_const = base_cache_size;
  /* Create an LRU cache. MAX_BYTES=0 keeps the old count-only policy.
     VALUE_BYTES measures a completed value including its owned allocations;
     if omitted, sizeof(T) is used. */
  lru_cache (const char *n, size_t max_bytes = 0,
             size_t (*value_bytes) (const T &) = nullptr)
      : Base (n, base_cache_size, max_bytes, value_bytes) {}

  /* Fetch the value for parameters P or generate it using GET_NEW. */
  std::shared_ptr<T>
  get (P &p, progress_info *progress, uint64_t *id = NULL,
       bool *cache_hit = NULL)
  {
    static_assert (get_new != nullptr,
                   "a generator-less cache must use get_or_compute");
    return get_or_compute (p, progress,
                           [](P &key, progress_info *task) {
                             return get_new (key, task);
                           }, id, cache_hit);
  }

  /* Fetch using an operation-specific generator, with the same per-key
     coalescing and cancellation as get(). Useful when the generator needs
     an enclosing image/resource without exposing it in the cache key. */
  template <typename Fetcher>
  std::shared_ptr<T>
  get_or_compute (P &p, progress_info *progress, Fetcher &&fetch,
                  uint64_t *id = NULL, bool *cache_hit = NULL)
  {
    return this->get_internal (
        p, progress, id, cache_hit,
        [&](Entry *e) { return p == e->params; },
        [](Entry *) {},
        [&](Entry *e) { return fetch (e->params, progress); });
  }

  /* Return a completed cached value for P without generating or waiting for
     one.  A matching entry which is still being computed is deliberately
     treated as a miss.  The returned shared pointer pins the cached value
     against eviction while the caller uses it.  */
  std::shared_ptr<T>
  peek (const P &p, uint64_t *id = NULL)
  {
    std::unique_lock<std::mutex> guard (this->lock);
    for (Entry *e = this->entries; e; e = e->next)
      if (!e->computing && e->val && p == e->params)
        {
          e->last_used = lru_caches::get ();
          if (id)
            *id = e->id;
          return e->val;
        }
    return nullptr;
  }
};

/* Cache entry for tile-based data.  */
template <typename P, typename T>
struct tile_cache_entry : lru_entry_base<P, T, tile_cache_entry<P, T>>
{
  int_image_area area;
};

/* LRU cache for tile-based data associated with parameters. 
   P is the parameter type.
   T is the result type.
   GET_NEW is the generator function.
   BASE_CACHE_SIZE is the default size.  */
template <typename P, typename T,
          std::unique_ptr<T> get_new (P &, int_image_area area, progress_info *progress),
          int base_cache_size>
class lru_tile_cache : public abstract_lru_cache<P, T, tile_cache_entry<P, T>, lru_tile_cache<P, T, get_new, base_cache_size>>
{
  using Entry = tile_cache_entry<P, T>;
  using Base = abstract_lru_cache<P, T, Entry, lru_tile_cache<P, T, get_new, base_cache_size>>;

public:
  static constexpr int base_size_const = base_cache_size;
  /* Create an LRU tile cache named N.  */
  lru_tile_cache (const char *n) : Base (n, base_cache_size) {}

  /* Fetch the value for parameters P and given tile coordinates or generate it.
     AREA defines the tile geometry.
     Use PROGRESS for task cancellation.
     ID will receive the unique identifier of the entry.  */
  std::shared_ptr<T>
  get (P &p, int_image_area area, progress_info *progress,
       uint64_t *id = NULL, bool *cache_hit = NULL)
  {
    return this->get_internal (
        p, progress, id, cache_hit,
        [&](Entry *e) {
          return e->area.contains_p (area) && p == e->params;
        },
        [&](Entry *e) {
          e->area = area;
        },
        [&](Entry *e) { return get_new (e->params, e->area, progress); });
  }
};

extern void render_increase_lru_cache_sizes_for_stitch_projects (int n);
extern void render_interpolated_increase_lru_cache_sizes_for_stitch_projects (int n);
/* Increase the capacity of all stitch-related caches by N times.  */
inline void
increase_lru_cache_sizes_for_stitch_projects (int n)
{
  render_increase_lru_cache_sizes_for_stitch_projects (n);
  render_interpolated_increase_lru_cache_sizes_for_stitch_projects (n);
}
}
#endif
