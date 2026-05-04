/* Progress information reporting and cancellation.
   Copyright (C) 2014-2026 Jan Hubicka
   This file is part of Color-Screen.  */

#ifndef PROGRESS_INFO_H
#define PROGRESS_INFO_H
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cinttypes>
#include <atomic>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <thread>
#include "dllpublic.h"
#include "base.h"

namespace colorscreen
{
/* Class to communicate progress info from rendering kernels.  It can also be
   used to cancel computation in the middle.  */
extern DLL_PUBLIC bool time_report;

/* Class for tracking and reporting progress of long-running tasks.
   This class provides a mechanism for workers to report their progress
   and for the UI or other monitors to check the current status and
   request cancellation.  */
class DLL_PUBLIC progress_info
{
public:
  /* Construct a new progress_info object.  */
  DLL_PUBLIC progress_info ();

  /* Destroy the progress_info object.  */
  DLL_PUBLIC virtual ~progress_info ();

  /* API used to monitor and control computation.  */

  /* Get the current task name and progress.
     T is the pointer where the task name is stored.
     S is the pointer where the progress percentage (0-100) is stored,
     or -1 if no progress info is available.  */
  void
  get_status (const char **t, float *s)
  {
    *t = m_task;
    if (m_max > 1)
      *s = 100.0 * m_current / m_max;
    else
      *s = -1;
  }

  /* Structure representing the status of a single task.  */
  struct status
  {
    const char *task;
    /* If >= 0 then it is the progress in percent.
       If negative, then current task has no progress info.  */
    float progress;

    /* Compare two status objects for equality.  */
    bool
    operator== (const struct status &o) const
    {
      return o.task == task && o.progress == progress;
    }
  };

  /* Structure representing a point in time for performance measurement.  */
  struct TimePoint {
    struct timeval wall;
    double cpu;
  };

  /* Return a stack of nested tasks and their progress.  */
  DLL_PUBLIC std::vector<status> get_status ();

  /* Request cancellation of the current operation.  */
  void
  cancel ()
  {
    m_cancel = true;
  }

  /* Check if the task has been cancelled.  */
  nodiscard_attr bool
  cancelled () const noexcept
  {
    return m_cancelled;
  }

  /* API used by the workers to inform about status and check if task
     should be cancelled.  */

  /* Check if cancellation has been requested.  */
  nodiscard_attr bool
  cancel_requested ()
  {
    if (m_cancel)
      {
        m_cancelled = true;
        return true;
      }
    return false;
  }

  /* Poll the cancellation flag without noting its effect.
     Used by UI to check if cancellation was requested before it is
     acknowledged by the worker.  */
  nodiscard_attr bool
  pool_cancel () const noexcept
  {
    return m_cancel;
  }

  /* Set the current task description and total number of steps.
     NAME is the name of the task (should be lowercase).
     MAX is the total number of steps in the task.  */
  DLL_PUBLIC virtual void set_task (const char *name, uint64_t max);

  /* Announce that the process is waiting (e.g., for a lock).
     NAME is the description of what the process is waiting for.  */
  DLL_PUBLIC virtual void wait (const char *name);

  /* Increment the progress of the current task by one step.  */
  void
  inc_progress ()
  {
    m_current++;
    if (debug && m_current > m_max)
      abort ();
  }

  /* Set the progress of the current task to a specific value.
     P is the current progress value.  */
  void
  set_progress (uint64_t p)
  {
    m_current = p;
  }

  /* Push the current task onto the stack and start a new nested task.
     SAFE if true means the push is considered safe from synchronization issues.  */
  DLL_PUBLIC int push (bool safe = false);

  /* Pop the current task from the stack and restore the previous task.
     EXPECTED is the expected stack depth for sanity checking.
     SAFE if true means the pop is considered safe from synchronization issues.  */
  DLL_PUBLIC virtual void pop (int expected = -1, bool safe = false);

  /* Temporarily stop progress reporting to allow standard output.  */
  DLL_PUBLIC virtual void pause_stdout ();

  /* Resume progress reporting after standard output is finished.  */
  DLL_PUBLIC virtual void resume_stdout ();

protected:
  /* Set to true to record also m_time.  */
  bool m_record_time = false;
  std::atomic<const char *> m_task = {nullptr};
  std::atomic_int64_t m_max = {0};

  TimePoint m_start_time = {};

private:
  /* Internal helper to set the current task.
     NAME is the task name.
     MAX is the maximum steps.  */
  void set_task_1 (const char *name, int64_t max);

  static const bool debug = false;
  std::atomic_int64_t m_current = {0};
  std::atomic_bool m_cancel = {false};
  std::atomic_bool m_cancelled = {false};

  struct task
  {
    int64_t max, current;
    const char *task;
  };
  std::vector<task> stack;
  std::vector<TimePoint> time_stack;
  std::mutex m_lock;
};

/* RAII wrapper for managing nested tasks.  */
class sub_task
{
public:
  /* Construct a new sub_task object, pushing it to the progress info stack.
     P is the pointer to the progress_info object.  */
  sub_task (progress_info *p)
  {
    m_progress = p;
    if (p)
      m_stack = m_progress->push (true);
    else
      m_stack = 0;
  }

  /* Destroy the sub_task object, popping it from the progress info stack.  */
  ~sub_task ()
  {
    if (m_progress)
      m_progress->pop (m_stack, true);
  }

private:
  int m_stack;
  progress_info *m_progress;
};

/* Progress info implementation that prints to a file or terminal.  */
class DLL_PUBLIC file_progress_info : public progress_info
{
public:
  /* Construct a new file_progress_info object.
     F is the file pointer to write progress to.
     DISPLAY if true enables visual progress display.
     PRINT_ALL_TASKS if true prints every task transition.  */
  DLL_PUBLIC file_progress_info (FILE *f, bool display = true, bool print_all_tasks = false);

  /* Destroy the file_progress_info object.  */
  DLL_PUBLIC ~file_progress_info ();

  /* Periodically display current progress.  */
  void display_progress ();

  DLL_PUBLIC virtual void pause_stdout () final;
  DLL_PUBLIC virtual void resume_stdout () final;

  DLL_PUBLIC virtual void set_task (const char *name, uint64_t max) final;
  DLL_PUBLIC virtual void wait (const char *name) final;
  DLL_PUBLIC virtual void pop (int expected = -1, bool safe = false) final;

  /* Used internally.  */
  std::mutex m_exit_lock;
  std::condition_variable m_exit_cond;
  std::atomic<bool> m_exit = {false};

private:
  bool m_print_all_tasks = false;
  bool m_display_progress = false;

  /* Internal helper to pause stdout.
     FINAL if true means this is the final pause during destruction.  */
  void pause_stdout (bool final);

  FILE *m_file = nullptr;
  std::thread m_thread;
  std::atomic_int64_t m_displayed = {0};
  std::atomic<int> m_last_printed_len = {0};
  std::vector<status> m_last_status;
  bool m_initialized = false;
};
}
#endif
