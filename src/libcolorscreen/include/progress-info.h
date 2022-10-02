#ifndef PROGRESS_INFO_H
#define PROGRESS_INFO_H
#include <stdio.h>
#include <pthread.h>
#include <cstdlib>
#include <atomic>
#include "dllpublic.h"

/* Class to communicate progress info from rendering kernels.  It can also be used to cancel computation in the middle.  */
class DLL_PUBLIC progress_info
{
public:
  constexpr progress_info ()
  : m_task (NULL), m_max (0), m_current (0), m_cancel (0), m_cancelled (0)
  {
  }
  ~progress_info ()
  {
    if (debug && m_task)
      {
	const char *t = m_task;
	unsigned long current = m_current;
	printf ("\nlast task %s: finished with %lu steps\n", t, current);
      }
  }

  /* API used to monitor and control computatoin.  */

  void
  get_status (const char **t, float *s)
  {
    *t = m_task;
    if (m_max)
      *s = 100.0 * m_current / m_max;
    else
      *s = 0;
  }

  bool
  cancel ()
  {
    if (m_cancel)
      {
	m_cancelled = true;
	return true;
      }
    return false;
  }

  /* API used by the workers to inform about status and check if task
     should be cancelled.  */

  void
  set_task (const char *name, unsigned long max)
  {
    if (debug && m_task)
      {
	const char *t = m_task;
	unsigned long current = m_current;
	printf ("\ntask %s: finished with %lu steps\n", t, current);
      }
    m_current = 0;
    m_max = max;
    m_task = name;
    if (debug)
      printf ("\ntask %s: %lu steps\n", name, max);
  }

  void
  inc_progress ()
  {
    m_current++;
    if (debug && m_current > m_max)
      abort ();
  }

  void
  set_progress (unsigned long p)
  {
    m_current = p;
  }

  bool
  cancelled ()
  {
    return m_cancelled;
  }
private:
  const static int debug = false;
  std::atomic<const char *>m_task;
  std::atomic_ulong m_max, m_current;
  std::atomic_bool m_cancel;
  std::atomic_bool m_cancelled;
};

class DLL_PUBLIC file_progress_info : public progress_info
{
public:
  file_progress_info (FILE *f, bool display = true);
  ~file_progress_info ();
  void display_progress ();
private:
  FILE *m_file;
  pthread_t m_thread;
  std::atomic_ulong m_displayed;
  std::atomic<const char *>m_last_task;
  float m_last_status;
  bool m_initialized;
};
#endif
