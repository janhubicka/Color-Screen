#ifndef LRU_CACHE
#define LRU_CACHE
#include <pthread.h>

template<typename P, typename T, T *get_new (P &), int cache_size>
class lru_cache
{
public:
  struct cache_entry
  {
    P params;
    T *val;
    cache_entry *next;
    int last_used;
    int nuses;
  } *entries;
  int time;

  lru_cache()
  : entries (NULL), time (0)
  {
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
  T *get(P &p)
  {
    int size = 0;
    struct cache_entry *longest_unused = NULL, *e;
    pthread_mutex_lock (&lock);
    time++;
    for (e = entries; e; e = e->next)
    {
      if (p == e->params)
        {
	  e->last_used = time;
	  e->nuses++;
	  pthread_mutex_unlock (&lock);
	  return e->val;
	}
      if (!e->nuses
	  && (!longest_unused || longest_unused->last_used < e->last_used))
	longest_unused = e;
      if (!e->nuses)
        size++;
    }
    if (size >= cache_size)
      e = longest_unused;
    e = new cache_entry;
    e->next = entries;
    entries = e;
    e->params = p;
    e->val = get_new (e->params);
    e->nuses = 1;
    pthread_mutex_unlock (&lock);
    return e->val;
  }
  void release(T *val)
  {
    pthread_mutex_lock (&lock);
    for (cache_entry *e = entries; ; e = e->next)
      if (e->val == val)
	{
	  e->nuses--;
	  pthread_mutex_unlock (&lock);
	  return;
	}
  }
private:
  pthread_mutex_t lock;
};

#endif
