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

namespace colorscreen
{
/* Class to communicate progress info from rendering kernels.  It can also be
   used to cancel computation in the middle.  */
extern DLL_PUBLIC bool time_report;
class DLL_PUBLIC progress_info
{
public:
  DLL_PUBLIC progress_info ();
  DLL_PUBLIC ~progress_info ();

  /* API used to monitor and control computation.  */

  void
  get_status (const char **t, float *s)
  {
    *t = m_task;
    if (m_max > 1)
      *s = 100.0 * m_current / m_max;
    else
      *s = -1;
  }
  struct status
  {
    const char *task;
    /* If >= 0 then it is the progress in percent.
       If negative, then current task has no progress info.  */
    float progress;
    bool
    operator== (const struct status &o) const
    {
      return o.task == task && o.progress == progress;
    }
  };

  struct TimePoint {
    struct timeval wall;
    double cpu;
  };

  /* Return stack of nested tasks and their progress.  */
  DLL_PUBLIC std::vector<status> get_status ();

  /* Request cancel.  */
  void
  cancel ()
  {
    m_cancel = true;
  }

  /* Return true if cancel was exeuted.  */
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

  /* Set current task to NAME with MAX steps.  */
  DLL_PUBLIC virtual void set_task (const char *name, uint64_t max);
  /* Announce that the process is waiting (i.e. for cache lock).  */
  DLL_PUBLIC virtual void wait (const char *name);

  /* Increment progress of current task.  */
  void
  inc_progress ()
  {
    m_current++;
    if (debug && m_current > m_max)
      abort ();
  }

  /* Set progress of current taks to given state.  */
  void
  set_progress (uint64_t p)
  {
    m_current = p;
  }

  /* Enter nested tasks; return stack depth.  */
  DLL_PUBLIC int push ();
  /* EXPECTED is return value of push used for sanity checking.  */
  DLL_PUBLIC virtual void pop (int expected = -1);

  /* This needs to be used around standard output so it does not get
     mixed randomly with progress reports.  */
  DLL_PUBLIC virtual void pause_stdout ();
  DLL_PUBLIC virtual void resume_stdout ();

protected:
  /* Set to true to record also m_time.  */
  bool m_record_time;
  std::atomic<const char *> m_task;
  std::atomic_int64_t m_max;

  TimePoint m_start_time;
private:
  void set_task_1 (const char *name, int64_t max);
  static const bool debug = false;
  std::atomic_int64_t m_current;
  std::atomic_bool m_cancel;
  std::atomic_bool m_cancelled;
  struct task
  {
    int64_t max, current;
    const char *task;
  };
  std::vector<task> stack;
  std::vector<TimePoint> time_stack;
  pthread_mutex_t m_lock;
};

class DLL_PUBLIC file_progress_info : public progress_info
{
public:
  DLL_PUBLIC file_progress_info (FILE *f, bool display = true, bool print_all_tasks = false);
  DLL_PUBLIC ~file_progress_info ();
  void display_progress ();
  DLL_PUBLIC virtual void pause_stdout () final;
  DLL_PUBLIC virtual void resume_stdout () final;

  DLL_PUBLIC virtual void set_task (const char *name, uint64_t max) final;
  DLL_PUBLIC virtual void wait (const char *name) final;
  DLL_PUBLIC virtual void pop (int expected = -1) final;

  /* Used internally.  */
  pthread_mutex_t m_exit_lock;
  pthread_cond_t m_exit_cond;
  std::atomic<bool> m_exit;

private:
  bool m_print_all_tasks;
  bool m_display_progress;
  void pause_stdout (bool final);
  FILE *m_file;
  pthread_t m_thread;
  std::atomic_int64_t m_displayed;
  // std::atomic<const char *>m_last_task;
  std::atomic<int> m_last_printed_len;
  std::vector<status> m_last_status;
  bool m_initialized;
};
}
#endif
