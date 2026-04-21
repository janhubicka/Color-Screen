#ifndef LRU_CACHE_H
#define LRU_CACHE_H
#include <mutex>
#include <chrono>
#include <atomic>
#include <memory>
#include <condition_variable>
#include <shared_mutex>
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

/* Base structure for cache entries. */
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

/* RAII guard to manage the computing flag and notifications. */
template <typename Mutex>
struct computing_guard
{
  bool &flag;
  Mutex &mutex;
  std::condition_variable_any &cond;
  bool active;
  computing_guard (bool &f, Mutex &m, std::condition_variable_any &c)
      : flag (f), mutex (m), cond (c), active (true)
  {
  }
  ~computing_guard ()
  {
    if (active)
      {
        std::unique_lock<Mutex> guard (mutex);
        flag = false;
        cond.notify_all ();
      }
  }
  void
  finished ()
  {
    active = false;
  }
};

/* Consolidated LRU cache implementation. */
template <typename P, typename T, typename Entry, typename Derived>
class abstract_lru_cache
{
  static const bool verbose = false;

protected:
  Entry *entries;
  int cache_size;
  std::shared_timed_mutex lock;
  std::condition_variable_any cond;
  const char *name;

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

  /* Internal lookup and management logic shared by all cache types. */
  template <typename Matcher, typename Init, typename Fetcher>
  std::shared_ptr<T>
  get_internal (P &p, progress_info *progress, uint64_t *id_out, Matcher &&match_func, Init &&init_func, Fetcher &&fetch_func)
  {
    uint64_t time = lru_caches::get ();
    Entry *longest_unused = NULL, *e;
    int size = 0;

    if (progress)
      progress->wait ("unlocking cache");

    std::unique_lock<std::shared_timed_mutex> guard (lock, std::defer_lock);
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
        if (match_func (e))
          {
            while (e->computing)
              {
                uint64_t id_val = e->id;
                if (progress)
                  progress->wait ("waiting for other thread to finish computation");
                if (cond.wait_for (guard, std::chrono::milliseconds (333))
                    == std::cv_status::timeout)
                  {
                    if (progress && progress->cancel_requested ())
                      return NULL;
                  }
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
            return ret;
          }
        if (e->val.use_count () <= 1 && !e->computing
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
      computing_guard<std::shared_timed_mutex> cguard (e->computing, lock, cond);
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
  void
  prune ()
  {
    std::unique_lock<std::shared_timed_mutex> guard (lock);
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

  void
  increase_capacity (int n)
  {
    std::unique_lock<std::shared_timed_mutex> guard (lock);
    cache_size = n * Derived::base_size_const;
  }
};

template <typename P, typename T>
struct lru_cache_entry : lru_entry_base<P, T, lru_cache_entry<P, T>> {};

template <typename P, typename T, std::unique_ptr<T> get_new (P &, progress_info *progress), int base_cache_size>
class lru_cache : public abstract_lru_cache<P, T, lru_cache_entry<P, T>, lru_cache<P, T, get_new, base_cache_size>>
{
  using Entry = lru_cache_entry<P, T>;
  using Base = abstract_lru_cache<P, T, Entry, lru_cache<P, T, get_new, base_cache_size>>;

public:
  static constexpr int base_size_const = base_cache_size;
  lru_cache (const char *n) : Base (n, base_cache_size) {}

  std::shared_ptr<T>
  get (P &p, progress_info *progress, uint64_t *id = NULL)
  {
    return this->get_internal (
        p, progress, id,
        [&](Entry *e) { return p == e->params; },
        [](Entry *) {},
        [&](Entry *e) { return get_new (e->params, progress); });
  }
};

template <typename P, typename T>
struct tile_cache_entry : lru_entry_base<P, T, tile_cache_entry<P, T>>
{
  int xshift, yshift, width, height;
};

template <typename P, typename T,
          std::unique_ptr<T> get_new (P &, int xshift, int yshift, int width, int height, progress_info *progress),
          int base_cache_size>
class lru_tile_cache : public abstract_lru_cache<P, T, tile_cache_entry<P, T>, lru_tile_cache<P, T, get_new, base_cache_size>>
{
  using Entry = tile_cache_entry<P, T>;
  using Base = abstract_lru_cache<P, T, Entry, lru_tile_cache<P, T, get_new, base_cache_size>>;

public:
  static constexpr int base_size_const = base_cache_size;
  lru_tile_cache (const char *n) : Base (n, base_cache_size) {}

  std::shared_ptr<T>
  get (P &p, int xshift, int yshift, int width, int height, progress_info *progress, uint64_t *id = NULL)
  {
    return this->get_internal (
        p, progress, id,
        [&](Entry *e) {
          return xshift <= e->xshift && yshift <= e->yshift && width - xshift <= e->width - e->xshift
                 && height - yshift <= e->height - e->yshift && p == e->params;
        },
        [&](Entry *e) {
          e->xshift = xshift;
          e->yshift = yshift;
          e->width = width;
          e->height = height;
        },
        [&](Entry *e) { return get_new (e->params, e->xshift, e->yshift, e->width, e->height, progress); });
  }
};

extern void render_increase_lru_cache_sizes_for_stitch_projects (int n);
extern void render_interpolated_increase_lru_cache_sizes_for_stitch_projects (int n);
inline void
increase_lru_cache_sizes_for_stitch_projects (int n)
{
  render_increase_lru_cache_sizes_for_stitch_projects (n);
  render_interpolated_increase_lru_cache_sizes_for_stitch_projects (n);
}
}
#endif
