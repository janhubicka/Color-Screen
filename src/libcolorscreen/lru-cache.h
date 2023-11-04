#ifndef LRU_CACHE
#define LRU_CACHE
#include <pthread.h>
#include <atomic>
#include <include/progress-info.h>
class lru_caches
{
public:
   constexpr lru_caches ()
   {
   }
   static unsigned long get ()
   {
      return std::atomic_fetch_add(&time, 1);
   }
private:
   static std::atomic_ulong time;
};
extern class lru_caches lru_caches;

/* LRU cache used keep various data between invocations of renderers.
   P represents parameters which are used to produce T.
   get_new is a function computing T based on P.  It is expected to
   allocate memory via new and lru_cache will eventually delete it.  */
template<typename P, typename T, T *get_new (P &, progress_info *progress), int base_cache_size>
class lru_cache
{
public:
  struct cache_entry
  {
    P params;
    T *val;
    cache_entry *next;
    unsigned long id;
    unsigned long last_used;
    int nuses;
  } *entries;

  lru_cache()
  : entries (NULL), cache_size (base_cache_size)
  {
    if (pthread_mutex_init(&lock, NULL) != 0)
      abort ();
  }

  ~lru_cache()
  {
    struct cache_entry *e, *next;
    for (e = entries; e; e = next)
      {
	next = e->next;
	delete e->val;
	delete e;
      }
  }
  void
  increase_capacity (int n)
  {
    pthread_mutex_lock (&lock);
    cache_size = n * base_cache_size;
    pthread_mutex_unlock (&lock);
  }

  /* Get T for parameters P; do caching.
     If ID is non-NULL initialize it to the unique identifier of the cached data.  */
  T *get(P &p, progress_info *progress, unsigned long *id = NULL)
  {
    int size = 0;
    unsigned long time = lru_caches::get ();
    struct cache_entry *longest_unused = NULL, *e;
    pthread_mutex_lock (&lock);
    time++;
    for (e = entries; e; e = e->next)
      {
	if (p == e->params)
	  {
	    e->last_used = time;
	    e->nuses++;
	    T *ret = e->val;
	    pthread_mutex_unlock (&lock);
	    if (id)
	      *id = e->id;
	    return ret;
	  }
	if (!e->nuses
	    && (!longest_unused || longest_unused->last_used < e->last_used))
	  longest_unused = e;
	if (!e->nuses)
	  size++;
      }
    if (size >= cache_size)
      {
        e = longest_unused;
	delete e->val;
      }
    else
      {
	e = new cache_entry;
	if (!e)
	  {
	    pthread_mutex_unlock (&lock);
	    return NULL;
	  }
	e->next = entries;
	entries = e;
      }
    e->params = p;
    e->val = get_new (e->params, progress);
    e->nuses = 1;
    e->id = time;
    e->last_used = time;
    T *ret = e->val;
    if (id)
      *id = e->id;
    if (!ret)
      {
	for (cache_entry **e2 = &entries; ; e2 = &(*e2)->next)
	  if (*e2 == e)
	    {
	      *e2 = e->next;
	      delete e;
	      break;
	    }
      }
    pthread_mutex_unlock (&lock);
    return ret;
  }

  /* Release T but keep it possibly in the cache.  */
  void release(T *val)
  {
    pthread_mutex_lock (&lock);
    for (cache_entry *e = entries; ; e = e->next)
      if (e->val == val)
	{
	  e->nuses--;
	  assert (e->nuses >= 0);
	  pthread_mutex_unlock (&lock);
	  return;
	}
  }
private:
  int cache_size;
  pthread_mutex_t lock;
};

/* LRU cache used keep various data between invocations of renderers.
   P represents parameters which are used to produce T.
   get_new is a function computing T based on P.  It is expected to
   allocate memory via new and lru_cache will eventually delete it.  */
template<typename P, typename T, T *get_new (P &, int xshift, int yshift, int width, int height, progress_info *progress), int base_cache_size>
class lru_tile_cache
{
public:
  struct cache_entry
  {
    P params;
    T *val;
    cache_entry *next;
    int xshift, yshift, width, height;
    unsigned long id;
    unsigned long last_used;
    int nuses;
  } *entries;

  lru_tile_cache()
  : entries (NULL), cache_size (base_cache_size)
  {
    if (pthread_mutex_init(&lock, NULL) != 0)
      abort ();
  }

  ~lru_tile_cache()
  {
    struct cache_entry *e, *next;
    for (e = entries; e; e = next)
      {
	next = e->next;
	delete e->val;
	delete e;
      }
  }
  void
  increase_capacity (int n)
  {
    pthread_mutex_lock (&lock);
    cache_size = n * base_cache_size;
    pthread_mutex_unlock (&lock);
  }

  /* Get T for parameters P; do caching.
     If ID is non-NULL initialize it to the unique identifier of the cached data.  */
  T *get(P &p, int xshift, int yshift, int width, int height, progress_info *progress, unsigned long *id = NULL)
  {
    int size = 0;
    unsigned long time = lru_caches::get ();
    struct cache_entry *longest_unused = NULL, *e;
    pthread_mutex_lock (&lock);
    time++;
    for (e = entries; e; e = e->next)
      {
	if (xshift <= e->xshift
	    && yshift <= e->yshift
	    && width - xshift <= e->width - e->xshift
	    && height - yshift <= e->height - e->yshift
	    && p == e->params)
	  {
	    e->last_used = time;
	    e->nuses++;
	    T *ret = e->val;
	    pthread_mutex_unlock (&lock);
	    if (id)
	      *id = e->id;
	    return ret;
	  }
	if (!e->nuses
	    && (!longest_unused || longest_unused->last_used < e->last_used))
	  longest_unused = e;
	if (!e->nuses)
	  size++;
      }
    if (size >= cache_size)
      {
        e = longest_unused;
	delete e->val;
      }
    else
      {
	e = new cache_entry;
	if (!e)
	  {
	    pthread_mutex_unlock (&lock);
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
    e->val = get_new (e->params, xshift, yshift, width, height, progress);
    e->nuses = 1;
    e->id = time;
    e->last_used = time;
    T *ret = e->val;
    if (id)
      *id = e->id;
    if (!ret)
      {
	for (cache_entry **e2 = &entries; ; e2 = &(*e2)->next)
	  if (*e2 == e)
	    {
	      *e2 = e->next;
	      delete e;
	      break;
	    }
      }
    pthread_mutex_unlock (&lock);
    return ret;
  }

  /* Release T but keep it possibly in the cache.  */
  void release(T *val)
  {
    pthread_mutex_lock (&lock);
    for (cache_entry *e = entries; ; e = e->next)
      if (e->val == val)
	{
	  e->nuses--;
	  assert (e->nuses >= 0);
	  pthread_mutex_unlock (&lock);
	  return;
	}
  }
private:
  int cache_size;
  pthread_mutex_t lock;
};

extern void render_increase_lru_cache_sizes_for_stitch_projects (int n);
inline void
increase_lru_cache_sizes_for_stitch_projects (int n)
{
   render_increase_lru_cache_sizes_for_stitch_projects (n);
}

#endif
