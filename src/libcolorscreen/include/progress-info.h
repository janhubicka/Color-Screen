#ifndef PROGRESS_INFO_H
#define PROGRESS_INFO_H
#include <cassert>
#include <stdio.h>
#include <pthread.h>
#include <cstdlib>
#include <cinttypes>
#include <atomic>
#include <vector>
#include "dllpublic.h"

/* Class to communicate progress info from rendering kernels.  It can also be
   used to cancel computation in the middle.  */
class progress_info
{
public:
  DLL_PUBLIC progress_info ();
  DLL_PUBLIC ~progress_info ();

  /* API used to monitor and control computation.  */

  void
  get_status (const char **t, float *s)
  {
    *t = m_task;
    if (m_max)
      *s = 100.0 * m_current / m_max;
    else
      *s = 0;
  }
  struct status
  {
    const char *task;
    float progress;
    bool
    operator== (const struct status &o) const
    {
      return o.task == task && o.progress == progress;
    }
  };

  std::vector<status>
  get_status ()
  {
    if (pthread_mutex_lock (&m_lock) != 0)
      perror ("lock");
    std::vector<status> ret;
    const char *task = m_task;
    int max = m_max;
    int current = m_current;
    ret.reserve (stack.size () + (task != NULL ? 1 : 0));
    for (auto s : stack)
      ret.push_back ({ s.task, s.max ? (float)100.0 * s.current / s.max : 0 });
    pthread_mutex_unlock (&m_lock);
    if (task != NULL)
      ret.push_back ({ task, max ? (float)100.0 * current / max : 0 });
    return ret;
  }

  void
  cancel ()
  {
    m_cancel = true;
  }

  bool
  cancelled ()
  {
    return m_cancelled;
  }

  /* API used by the workers to inform about status and check if task
     should be cancelled.  */

  bool
  cancel_requested ()
  {
    if (m_cancel)
      {
        m_cancelled = true;
        return true;
      }
    return false;
  }

  void
  set_task (const char *name, uint64_t max)
  {
    if (debug && m_task)
      {
        const char *t = m_task;
        uint64_t current = m_current;
        printf ("\ntask %s: finished with %" PRIu64 " steps\n", t, current);
      }
    m_current = 0;
    m_max = max;
    m_task = name;
    if (debug)
      printf ("\ntask %s: %" PRIu64 " steps\n", name, max);
  }

  void
  inc_progress ()
  {
    m_current++;
    if (debug && m_current > m_max)
      abort ();
  }

  void
  set_progress (uint64_t p)
  {
    m_current = p;
  }

  int
  push ()
  {
    if (pthread_mutex_lock (&m_lock) != 0)
      perror ("lock");
    int ret = stack.size ();
    assert (ret >= 0);
    stack.push_back ({ m_max, m_current, m_task });
    m_task = NULL;
    m_current = 0;
    m_max = 1;
    pthread_mutex_unlock (&m_lock);
    return ret;
  }

  /* EXPECTED is return value of push used for sanity checking.  */
  void
  pop (int expected = -1)
  {
    if (pthread_mutex_lock (&m_lock) != 0)
      perror ("lock");
    assert (stack.size () > 0);
    task t = stack.back ();
    m_current = t.current;
    m_max = t.max;
    m_task = t.task;
    stack.pop_back ();
    assert (expected == -1 || (int)stack.size () == expected);
    pthread_mutex_unlock (&m_lock);
  }

  DLL_PUBLIC virtual void pause_stdout ();
  DLL_PUBLIC virtual void resume_stdout ();

private:
  static const bool debug = false;
  std::atomic<const char *> m_task;
  std::atomic_uint64_t m_max, m_current;
  std::atomic_bool m_cancel;
  std::atomic_bool m_cancelled;
  struct task
  {
    uint64_t max, current;
    const char *task;
  };
  std::vector<task> stack;
  pthread_mutex_t m_lock;
};

class file_progress_info : public progress_info
{
public:
  DLL_PUBLIC file_progress_info (FILE *f, bool display = true);
  DLL_PUBLIC ~file_progress_info ();
  void display_progress ();
  DLL_PUBLIC virtual void pause_stdout () final;
  DLL_PUBLIC virtual void resume_stdout () final;

  pthread_mutex_t m_exit_lock;
  pthread_cond_t m_exit_cond;
  std::atomic<bool> m_exit;

private:
  void pause_stdout (bool final);
  FILE *m_file;
  pthread_t m_thread;
  std::atomic_uint64_t m_displayed;
  // std::atomic<const char *>m_last_task;
  std::atomic<int> m_last_printed_len;
  std::vector<status> m_last_status;
  bool m_initialized;
};
#endif
