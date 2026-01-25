#ifndef LRU_CACHE_H
#define LRU_CACHE_H
#include <mutex>
#include <chrono>
#include <atomic>
#include <memory>
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
   allocate memory via new and lru_cache will eventually delete it.  */
template <typename P, typename T, typename TP,
          TP get_new (P &, progress_info *progress), int base_cache_size>
class lru_cache
{
  static const bool debug = false;
  static const bool verbose = false;

public:
  struct cache_entry
  {
    P params;
    std::unique_ptr<T> val;
    cache_entry *next;
    uint64_t id;
    uint64_t last_used;
    int nuses;
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
        if (!(*e)->nuses)
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
  TP
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
    for (e = entries; e; e = e->next)
      {
        if (p == e->params)
          {
            e->last_used = time;
            e->nuses++;
            if (verbose)
              fprintf (stderr, "Cache %s: hit id %i nuses %i\n", name,
                       (int)e->id, e->nuses);
            TP ret = e->val.get ();
            if (id)
              *id = e->id;
            return ret;
          }
        if (!e->nuses
            && (!longest_unused || longest_unused->last_used < e->last_used))
          longest_unused = e;
        size++;
      }
    if (size >= cache_size && longest_unused)
      {
        e = longest_unused;
        if (verbose)
          fprintf (stderr, "Cache %s: deleting id %i\n", name, (int)e->id);
        e->val = NULL;
      }
    else
      {
        if (debug && size >= cache_size)
          fprintf (stderr, "Cache %s is over capacity: %i %i\n", name,
                   size + 1, cache_size);
        e = new cache_entry;
        if (!e)
          {
            return NULL;
          }
        e->next = entries;
        entries = e;
      }
    e->params = p;
    e->val = std::unique_ptr<T> (get_new (e->params, progress));
    e->nuses = 1;
    e->id = time;
    e->last_used = time;
    TP ret = e->val.get ();
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

  /* Release T but keep it possibly in the cache.  */
  void
  release (TP val)
  {
    std::lock_guard<std::timed_mutex> guard (lock);
    for (cache_entry *e = entries;; e = e->next)
      if (e->val.get () == val)
        {
          e->nuses--;
          assert (e->nuses >= 0);
          if (verbose)
            fprintf (stderr, "Cache %s: reclaimed id %i nuses %i\n", name,
                     (int)e->id, e->nuses);
          return;
        }
    fprintf (stderr, "Released data not found in cache %s\n", name);
  }

private:
  int cache_size;
  std::timed_mutex lock;
  const char *name;
};

/* LRU cache used keep various data between invocations of renderers.
   P represents parameters which are used to produce T.
   get_new is a function computing T based on P.  It is expected to
   allocate memory via new and lru_cache will eventually delete it.  */
template <typename P, typename T, typename TP,
          TP get_new (P &, int xshift, int yshift, int width, int height,
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
    std::unique_ptr<T> val;
    cache_entry *next;
    int xshift, yshift, width, height;
    uint64_t id;
    uint64_t last_used;
    int nuses;
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
        if (!(*e)->nuses)
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
  TP
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
    for (e = entries; e; e = e->next)
      {
        if (xshift <= e->xshift && yshift <= e->yshift
            && width - xshift <= e->width - e->xshift
            && height - yshift <= e->height - e->yshift && p == e->params)
          {
            e->last_used = time;
            e->nuses++;
            if (verbose)
              fprintf (stderr, "Cache %s: hit id %i nuses %i\n", name,
                       (int)e->id, (int)e->nuses);
            TP ret = e->val.get ();
            if (id)
              *id = e->id;
            return ret;
          }
        if (!e->nuses
            && (!longest_unused || longest_unused->last_used < e->last_used))
          longest_unused = e;
        size++;
      }
    if (size >= cache_size && longest_unused)
      {
        e = longest_unused;
        if (verbose)
          fprintf (stderr, "Cache %s: deleting id %i\n", name, (int)e->id);
        e->val = NULL;
      }
    else
      {
        if (debug && size >= cache_size)
          fprintf (stderr, "Cache %s is over capacity: %i %i\n", name,
                   size + 1, cache_size);
        e = new cache_entry;
        if (!e)
          {
            return NULL;
          }
        e->next = entries;
        entries = e;
      }
    e->params = p;
    e->xshift = xshift;
    e->yshift = yshift;
    e->width = width;
    e->height = height;
    e->val = std::unique_ptr<T> (get_new (e->params, xshift, yshift, width, height, progress));
    e->nuses = 1;
    e->id = time;
    e->last_used = time;
    TP ret = e->val.get ();
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

  /* Release T but keep it possibly in the cache.  */
  void
  release (TP val)
  {
    std::lock_guard<std::timed_mutex> guard (lock);
    for (cache_entry *e = entries;; e = e->next)
      if (e->val.get () == val)
        {
          e->nuses--;
          assert (e->nuses >= 0);
          if (verbose)
            fprintf (stderr, "Cache %s: reclaimed id %i nuses %i\n", name,
                     (int)e->id, e->nuses);
          return;
        }
    fprintf (stderr, "Released data not found in cache %s\n", name);
  }

private:
  int cache_size;
  std::timed_mutex lock;
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
