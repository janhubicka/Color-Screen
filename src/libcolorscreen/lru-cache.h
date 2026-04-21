#ifndef LRU_CACHE_H
#define LRU_CACHE_H
#include <mutex>
#include <chrono>
#include <atomic>
#include <memory>
#include <condition_variable>
#include <type_traits>
#include <include/progress-info.h>
namespace colorscreen
{
class lru_caches
{
public:
  constexpr
  lru_caches ()
  {
  }
  static uint64_t
  get ()
  {
    return std::atomic_fetch_add (&time, 1);
  }

private:
  static std::atomic_uint64_t time;
};
extern class lru_caches lru_caches;

/* LRU cache used keep various data between invocations of renderers.
   P represents parameters which are used to produce T.
   get_new is a function computing T based on P.  It is expected to
   allocate memory via new and lru_cache will eventually delete it.

   Synchronization Model:
   The cache uses a combination of a global lock (std::timed_mutex) and per-entry
   state ("computing" flag) to ensure thread-safety while maximizing concurrency.

   1. Lock Granularity: The global "lock" protects the integrity of the linked list
      ("entries") and metadata (nuses, last_used, id).

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
        - Task Cancellation: All wait loops (both for the global lock and the
          condition variable) periodically wake up (333ms) to check if the
          caller has requested task cancellation via the "progress" object.  */
template <typename P, typename T,
          std::unique_ptr<T> get_new (P &, progress_info *progress), int base_cache_size>
class lru_cache
{
  static const bool debug = false;
  static const bool verbose = false;

public:
  struct cache_entry
  {
    P params;
    std::shared_ptr<T> val;
    cache_entry *next;
    uint64_t id;
    uint64_t last_used;
    bool computing = false;
  } *entries;

  lru_cache (const char *n)
      : entries (NULL), cache_size (base_cache_size), name (n)
  {
  }

  void
  prune ()
  {
    std::lock_guard<std::timed_mutex> guard (lock);
    struct cache_entry **e;
    for (e = &entries; *e;)
      {
        // If use_count == 1, only the cache entry holds a reference to it.
        // It means no other part of the application is using it.
        if ((*e)->val.use_count() <= 1 && !(*e)->computing)
          {
            if (verbose)
              fprintf (stderr, "Cache %s: deleting id %i\n", name,
                       (int)(*e)->id);
            struct cache_entry *next = (*e)->next;
            delete (*e);
            (*e) = next;
          }
        else
          e = &(*e)->next;
      }
  }

  ~lru_cache ()
  {
    prune ();
    if (entries)
      fprintf (stderr, "Claimed entries in cache %s. Leaking memory\n", name);
  }
  void
  increase_capacity (int n)
  {
    std::lock_guard<std::timed_mutex> guard (lock);
    cache_size = n * base_cache_size;
  }

  /* Get T for parameters P; do caching.
     If ID is non-NULL initialize it to the unique identifier of the cached
     data.  */
  std::shared_ptr<T>
  get (P &p, progress_info *progress, uint64_t *id = NULL)
  {
    int size = 0;
    uint64_t time = lru_caches::get ();
    struct cache_entry *longest_unused = NULL, *e;

    if (progress)
      progress->wait ("unlocking cache");
    std::unique_lock<std::timed_mutex> guard (lock, std::defer_lock);
    while (!guard.try_lock_for (std::chrono::milliseconds (333)))
      {
        if (progress && progress->cancel_requested ())
          return NULL;
      }
    
    time++;
  restart:
    size = 0;
    longest_unused = NULL;
    for (e = entries; e; e = e->next)
      {
        if (p == e->params)
          {
            while (e->computing)
              {
                uint64_t id = e->id;
		if (progress && e->computing)
		  progress->wait ("waiting for other thread to finish computation");
                if (cond.wait_for (guard, std::chrono::milliseconds (333))
                    == std::cv_status::timeout)
                  {
                    if (progress && progress->cancel_requested ())
                      return NULL;
                  }
                bool found = false;
                for (struct cache_entry *e2 = entries; e2; e2 = e2->next)
                  if (e2 == e && e2->id == id)
                    {
                      found = true;
                      break;
                    }
                if (!found)
                  goto restart;
              }
            e->last_used = time;
            if (verbose)
              fprintf (stderr, "Cache %s: hit id %i\n", name,
                       (int)e->id);
            std::shared_ptr<T> ret = e->val;
            if (id)
              *id = e->id;
            return ret;
          }
        // Identifies an unused item (use_count == 1) to be evicted
        if (e->val.use_count() <= 1 && !e->computing
            && (!longest_unused || longest_unused->last_used < e->last_used))
          longest_unused = e;
        size++;
      }
    if (size >= cache_size && longest_unused)
      {
        e = longest_unused;
        if (verbose)
          fprintf (stderr, "Cache %s: deleting id %i\n", name, (int)e->id);
        e->val = nullptr; // Note: resetting the shared_ptr drops the previous value
      }
    else
      {
        if (debug && size >= cache_size)
          fprintf (stderr, "Cache %s is over capacity: %i %i\n", name,
                   size + 1, cache_size);
        e = new cache_entry;
        if (!e)
          {
            return nullptr;
          }
        e->next = entries;
        entries = e;
      }
    e->params = p;
    e->computing = true;
    e->id = time;
    e->last_used = time;
    
    guard.unlock ();
    std::unique_ptr<T> val = get_new (e->params, progress);
    guard.lock ();

    e->val = std::move(val);
    e->computing = false;
    cond.notify_all ();

    std::shared_ptr<T> ret = e->val;
    if (id)
      *id = e->id;
    if (!ret)
      {
        for (cache_entry **e2 = &entries;; e2 = &(*e2)->next)
          if (*e2 == e)
            {
              *e2 = e->next;
              delete e;
              break;
            }
      }
    if (verbose)
      fprintf (stderr, "Cache %s: added id %i size %i\n", name, (int)e->id,
               (int)size);
    return ret;
  }



private:
  int cache_size;
  std::timed_mutex lock; std::condition_variable_any cond;
  const char *name;
};

/* LRU tile cache used keep various data between invocations of renderers.
   P represents parameters which are used to produce T.
   get_new is a function computing T based on P.  It is expected to
   allocate memory via new and lru_cache will eventually delete it.  

   The synchronization model and race condition prevention is identical to lru_cache class. */
template <typename P, typename T,
          std::unique_ptr<T> get_new (P &, int xshift, int yshift, int width, int height,
                      progress_info *progress),
          int base_cache_size>
class lru_tile_cache
{
  static const bool debug = false;
  static const bool verbose = false;

public:
  struct cache_entry
  {
    P params;
    std::shared_ptr<T> val;
    cache_entry *next;
    int xshift, yshift, width, height;
    uint64_t id;
    uint64_t last_used;
    bool computing = false;
  } *entries;

  lru_tile_cache (const char *n)
      : entries (NULL), cache_size (base_cache_size), name (n)
  {
  }

  void
  prune ()
  {
    std::lock_guard<std::timed_mutex> guard (lock);
    struct cache_entry **e;
    for (e = &entries; *e;)
      {
        if ((*e)->val.use_count() <= 1 && !(*e)->computing)
          {
            if (verbose)
              fprintf (stderr, "Cache %s: deleting id %i\n", name,
                       (int)(*e)->id);
            struct cache_entry *next = (*e)->next;
            delete (*e);
            *e = next;
          }
        else
          e = &(*e)->next;
      }
  }

  ~lru_tile_cache ()
  {
    prune ();
    if (entries)
      fprintf (stderr, "Claimed entries in cache %s. Leaking memory\n", name);
  }
  void
  increase_capacity (int n)
  {
    std::lock_guard<std::timed_mutex> guard (lock);
    cache_size = n * base_cache_size;
  }

  /* Get T for parameters P; do caching.
     If ID is non-NULL initialize it to the unique identifier of the cached
     data.  */
  std::shared_ptr<T>
  get (P &p, int xshift, int yshift, int width, int height,
       progress_info *progress, uint64_t *id = NULL)
  {
    int size = 0;
    uint64_t time = lru_caches::get ();
    struct cache_entry *longest_unused = NULL, *e;
    if (progress)
      progress->wait ("unlocking cache");
    std::unique_lock<std::timed_mutex> guard (lock, std::defer_lock);
    while (!guard.try_lock_for (std::chrono::milliseconds (333)))
      {
        if (progress && progress->cancel_requested ())
          return NULL;
      }
    time++;
  restart:
    size = 0;
    longest_unused = NULL;
    for (e = entries; e; e = e->next)
      {
        if (xshift <= e->xshift && yshift <= e->yshift
            && width - xshift <= e->width - e->xshift
            && height - yshift <= e->height - e->yshift && p == e->params)
          {
            while (e->computing)
              {
                uint64_t id = e->id;
		if (progress && e->computing)
		  progress->wait ("waiting for other thread to finish computation");
                if (cond.wait_for (guard, std::chrono::milliseconds (333))
                    == std::cv_status::timeout)
                  {
                    if (progress && progress->cancel_requested ())
                      return NULL;
                  }
                bool found = false;
                for (struct cache_entry *e2 = entries; e2; e2 = e2->next)
                  if (e2 == e && e2->id == id)
                    {
                      found = true;
                      break;
                    }
                if (!found)
                  goto restart;
              }
            e->last_used = time;
            if (verbose)
              fprintf (stderr, "Cache %s: hit id %i\n", name,
                       (int)e->id);
            std::shared_ptr<T> ret = e->val;
            if (id)
              *id = e->id;
            return ret;
          }
        if (e->val.use_count() <= 1 && !e->computing
            && (!longest_unused || longest_unused->last_used < e->last_used))
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
        if (debug && size >= cache_size)
          fprintf (stderr, "Cache %s is over capacity: %i %i\n", name,
                   size + 1, cache_size);
        e = new cache_entry;
        if (!e)
          {
            return nullptr;
          }
        e->next = entries;
        entries = e;
      }
    e->params = p;
    e->xshift = xshift;
    e->yshift = yshift;
    e->width = width;
    e->height = height;
    e->computing = true;
    e->id = time;
    e->last_used = time;

    guard.unlock ();
    std::unique_ptr<T> val = get_new (e->params, xshift, yshift, width, height, progress);
    guard.lock ();

    e->val = std::move(val);
    e->computing = false;
    cond.notify_all ();

    std::shared_ptr<T> ret = e->val;
    if (id)
      *id = e->id;
    if (!ret)
      {
        for (cache_entry **e2 = &entries;; e2 = &(*e2)->next)
          if (*e2 == e)
            {
              *e2 = e->next;
              delete e;
              break;
            }
      }
    if (verbose)
      fprintf (stderr, "Cache %s: added id %i size %i\n", name, (int)e->id,
               (int)size);
    return ret;
  }



private:
  int cache_size;
  std::timed_mutex lock; std::condition_variable_any cond;
  const char *name;
};

extern void render_increase_lru_cache_sizes_for_stitch_projects (int n);
extern void
render_interpolated_increase_lru_cache_sizes_for_stitch_projects (int n);
inline void
increase_lru_cache_sizes_for_stitch_projects (int n)
{
  render_increase_lru_cache_sizes_for_stitch_projects (n);
  render_interpolated_increase_lru_cache_sizes_for_stitch_projects (n);
}
}
#endif
