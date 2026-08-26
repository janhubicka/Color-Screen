#ifndef LRU_CACHE_H
#define LRU_CACHE_H
#include <mutex>
#include <chrono>
#include <thread>
#include <atomic>
#include <memory>
#include <condition_variable>
#include <type_traits>
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
class abstract_lru_cache
{
  static const bool verbose = false;

protected:
  Entry *entries;
  int cache_size;
  std::mutex lock;
  std::condition_variable cond;
  const char *name;

  /* Initialize the cache with a NAME and BASE_SIZE.  */
  abstract_lru_cache (const char *n, int base_size)
      : entries (NULL), cache_size (base_size), name (n)
  {
  }

  virtual ~abstract_lru_cache ()
  {
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
    cond.notify_all ();

    if (id_out)
      *id_out = e->id;
    if (!ret_val)
      {
        for (Entry **e2 = &entries;; e2 = &(*e2)->next)
          if (*e2 == e)
            {
              *e2 = e->next;
              delete e;
              break;
            }
      }
    if (verbose && ret_val)
      fprintf (stderr, "Cache %s: added id %i size %i\n", name, (int)e->id, (int)size);
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
            delete (*e);
            (*e) = next;
          }
        else
          e = &(*e)->next;
      }
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
template <typename P, typename T, std::unique_ptr<T> get_new (P &, progress_info *progress), int base_cache_size>
class lru_cache : public abstract_lru_cache<P, T, lru_cache_entry<P, T>, lru_cache<P, T, get_new, base_cache_size>>
{
  using Entry = lru_cache_entry<P, T>;
  using Base = abstract_lru_cache<P, T, Entry, lru_cache<P, T, get_new, base_cache_size>>;

public:
  static constexpr int base_size_const = base_cache_size;
  /* Create an LRU cache named N.  */
  lru_cache (const char *n) : Base (n, base_cache_size) {}

  /* Fetch the value for parameters P or generate it.
     Use PROGRESS for task cancellation.
     ID will receive the unique identifier of the entry.  */
  std::shared_ptr<T>
  get (P &p, progress_info *progress, uint64_t *id = NULL,
       bool *cache_hit = NULL)
  {
    return this->get_internal (
        p, progress, id, cache_hit,
        [&](Entry *e) { return p == e->params; },
        [](Entry *) {},
        [&](Entry *e) { return get_new (e->params, progress); });
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
