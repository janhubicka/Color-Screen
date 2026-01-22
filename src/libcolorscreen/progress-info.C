#include <unistd.h>
#include <cerrno>
#include <math.h>
#include <sys/time.h>
#include <cstring>
#include <cstdlib>
#include <map>
#include <string>
#include <mutex>
#include <iomanip>
#include "include/progress-info.h"
namespace colorscreen
{
bool time_report = false;

static std::map<std::string, std::pair<double, double>> monitor_map;
static std::mutex monitor_mutex;

static double
get_thread_cpu_time ()
{
#if defined(__linux__) || defined(__APPLE__)
  struct timespec ts;
#ifdef CLOCK_THREAD_CPUTIME_ID
  clock_gettime (CLOCK_THREAD_CPUTIME_ID, &ts);
#else
  clock_gettime (CLOCK_PROCESS_CPUTIME_ID, &ts);
#endif
  return ts.tv_sec + ts.tv_nsec / 1e9;
#else
  return (double)clock () / CLOCKS_PER_SEC;
#endif
}

class TimeReporter
{
public:
  ~TimeReporter ()
  {
    if (time_report)
      {
        printf("\nTime report:\n");
        printf("------------------------------------------------------------------------------------------------\n");
        printf("%-40s | %10s | %6s | %10s | %6s\n", "Task", "Wall Time", "%", "CPU Time", "%");
        printf("------------------------------------------------------------------------------------------------\n");
        
        double total_wall = 0;
        double total_cpu = 0;

        /* Calculate totals first for percentage */
         
        for (auto &i : monitor_map) {
             total_wall += i.second.first;
             total_cpu += i.second.second;
        }

        /* Filter and print */
           
        for (auto &i : monitor_map)
          {
             double wall = i.second.first;
             double cpu = i.second.second;
             double wall_perc = (total_wall > 0) ? (wall / total_wall * 100.0) : 0;
             
             if (wall_perc >= 1.0) 
             {
                 printf("%-40s | %10.3fs | %5.1f%% | %10.3fs | %5.1f%%\n", 
                        i.first.c_str(), wall, wall_perc, cpu, (total_cpu > 0) ? (cpu/total_cpu*100.0) : 0);
             }
          }
        printf("------------------------------------------------------------------------------------------------\n");
        printf("%-40s | %10.3fs | %6s | %10.3fs | %6s\n", "TOTAL CHECKPOINTS", total_wall, "100%", total_cpu, "100%");
        printf("------------------------------------------------------------------------------------------------\n");
      }
  }
};

static TimeReporter time_reporter;

static void
record_time (const char *task, const progress_info::TimePoint &start)
{
  struct timeval end;
  gettimeofday (&end, NULL);
  double diff = end.tv_sec - start.wall.tv_sec + (end.tv_usec - start.wall.tv_usec) / 1000000.0;
  double cpu_diff = get_thread_cpu_time () - start.cpu;
  
  std::lock_guard<std::mutex> lock (monitor_mutex);
  monitor_map[task].first += diff;
  monitor_map[task].second += cpu_diff;
}

progress_info::progress_info ()
: m_record_time (false), m_task (NULL), m_max (0), m_current (0), m_cancel (0), m_cancelled (0), m_lock (PTHREAD_MUTEX_INITIALIZER)
{
}
progress_info::~progress_info ()
{
  if (debug && m_task)
    {
      const char *t = m_task;
      uint64_t current = m_current;
      printf ("\nlast task %s: finished with %" PRIu64 " steps\n", t, current);
    }
}
void
progress_info::pause_stdout ()
{
}
void
progress_info::resume_stdout ()
{
}
/* Negative max is used to announce waiting tasks.  */
void
progress_info::set_task_1 (const char *name, int64_t max)
{
  if (debug && m_task)
    {
      const char *t = m_task;
      uint64_t current = m_current;
      printf ("\ntask %s: finished with %" PRIu64 " steps\n", t, current);
    }

  if (m_task && time_report)
    record_time (m_task, m_start_time);
  m_current = 0;
  m_max = max;
  m_task = name;
  if (m_record_time || time_report)
   {
    gettimeofday (&m_start_time.wall, NULL);
    m_start_time.cpu = get_thread_cpu_time ();
   }
  if (debug)
    printf ("\ntask %s: %" PRIu64 " steps\n", name, max);
}

void
progress_info::set_task (const char *name, uint64_t max)
{
  set_task_1 (name, max);
}
/* Indicate that process is waiting for something, like lock.  */
void
progress_info::wait (const char *name)
{
  set_task_1 (name, -1);
}
/* Parameter SAFE indicates if push happens from set_task.
   Accessing push/pop directly may lead to stack desynchronization.  */
int
progress_info::push (bool safe)
{
  if (pthread_mutex_lock (&m_lock) != 0)
    perror ("lock");
  //if (!safe)
    //printf ("Unsafe push\n");
  int ret = stack.size ();
  assert (ret >= 0);
  stack.push_back ({ m_max, m_current, m_task });

  if (m_record_time || time_report)
    time_stack.push_back (m_start_time);
  m_task = NULL;
  m_current = 0;
  m_max = 1;
  pthread_mutex_unlock (&m_lock);
  return ret;
}

/* EXPECTED is return value of push used for sanity checking.
   Parameter SAFE indicates if push happens from set_task.  */
void
progress_info::pop (int expected, bool safe)
{
  if (pthread_mutex_lock (&m_lock) != 0)
    perror ("lock");
  //if (!safe)
    //printf ("Unsafe pop\n");
  assert (stack.size () > 0);
  task t = stack.back ();
  m_current = t.current;
  m_max = t.max;
  if (m_task && time_report)
    record_time (m_task, m_start_time);
  m_task = t.task;
  assert (expected == -1 || (int)stack.size () - 1 == expected);
  stack.pop_back ();
  if (m_record_time || time_report)
    {
      m_start_time = time_stack.back ();
      time_stack.pop_back ();
    }
  pthread_mutex_unlock (&m_lock);
}
static void *
thread_start (void *arg)
{
  file_progress_info *p = (file_progress_info *)arg;
  bool first = true;
  while (true)
    {
      pthread_mutex_lock (&p->m_exit_lock);
      struct timespec ts, now;
      int timev = first ? 300000 : 100000;

#if defined (__APPLE__) || defined (_WIN32)
      clock_gettime(CLOCK_REALTIME, &now);
#else
      clock_gettime(CLOCK_MONOTONIC, &now);
#endif
      ts.tv_sec = now.tv_sec;
      ts.tv_nsec = now.tv_nsec+timev * 1000;
      if (ts.tv_nsec>=1000000000)
	{
	  ts.tv_sec += ts.tv_nsec / 1000000000;
	  ts.tv_nsec %= 1000000000;
	}

      int ret;
      bool ex;
      if ((ex = (bool)p->m_exit)
	  || (ret = pthread_cond_timedwait (&p->m_exit_cond, &p->m_exit_lock, &ts) == 0))
	{
	  pthread_mutex_unlock (&p->m_exit_lock);
	  pthread_exit (0);
	}
      pthread_mutex_unlock (&p->m_exit_lock);
      p->display_progress ();
    }
  return NULL;
}
std::vector<progress_info::status>
progress_info::get_status ()
{
  if (pthread_mutex_lock (&m_lock) != 0)
    perror ("lock");
  std::vector<status> ret;
  const char *task = m_task;
  int max = m_max;
  int current = m_current;
  ret.reserve (stack.size () + (task != NULL ? 1 : 0));
  for (auto s : stack)
    {
      float progress = -1;
      if (s.max > 1)
	progress =  (float)100.0 * s.current / s.max;
      ret.push_back ({ s.task, progress });
    }
  pthread_mutex_unlock (&m_lock);
  if (task != NULL)
    ret.push_back ({ task, max > 1 ? (float)100.0 * current / max : -1 });
  return ret;
}
file_progress_info::file_progress_info (FILE *f, bool display, bool print_all_tasks)
{
  // TODO: For some reason Windows pthread API will not cancel the thread.
//#ifdef _WIN32
  //display = false;
//#endif
  m_initialized = false;
  m_file = f;
  m_displayed = 0;
  m_last_printed_len = 0;
  m_exit = false;
  m_print_all_tasks = print_all_tasks && f != NULL;
  if (m_print_all_tasks)
    m_record_time = true;
  m_display_progress = false;
  if (display && f)
    {
      m_display_progress = true;
      pthread_mutex_init (&m_exit_lock, NULL);
      pthread_condattr_t condattr;
      pthread_condattr_init(&condattr);
#if !defined (__APPLE__) && !defined (_WIN32)
      pthread_condattr_setclock(&condattr, CLOCK_MONOTONIC);
#endif
      pthread_cond_init (&m_exit_cond, &condattr);
      if (!f || !display)
	return;
      resume_stdout ();
    }
}
file_progress_info::~file_progress_info ()
{
  pause_stdout (true);
}
static void
clear_task (FILE *f, int len)
{
  if (len)
    fprintf (f, "\r%*s\r", len, "");
}
void
file_progress_info::pause_stdout (bool final)
{
  if (!m_initialized || !m_display_progress)
    return;
  pthread_mutex_lock (&m_exit_lock);
  m_exit = true;
  pthread_cond_signal (&m_exit_cond);
  pthread_mutex_unlock (&m_exit_lock);
  pthread_join (m_thread, NULL);
  if (m_displayed)
    {
      int len = m_last_printed_len;
      if (len)
        {
	  if (!final || true)
	    clear_task (m_file, len);
#if 0
	  else
	    {
	      print_finished (m_file, task);
	      m_last_printed_len = 0;
	      free ((void *)task);
	    }
#endif
        }
      fflush (m_file);
    }
  m_initialized = false;
}
void
file_progress_info::pause_stdout ()
{
  pause_stdout (false);
}

static int 
print_task (FILE *f, std::vector<progress_info::status> &status, const struct timeval *start_time)
{
  int len = 0;
  for (size_t i = 0; i < status.size () - 1; i++)
    len += fprintf (f, "  ");
  auto s = status [status.size () - 1];
  if (!start_time)
    len += fprintf (f, "%s\n", s.task);
  else
    {
      struct timeval end_time;
      gettimeofday (&end_time, NULL);
      double time = end_time.tv_sec + end_time.tv_usec/1000000.0 - start_time->tv_sec - start_time->tv_usec/1000000.0;
      fprintf (f, "... done in %.3fs\n", time);
    }
  fflush (f);
  return len;
}

static int 
print_status (FILE *f, std::vector<progress_info::status> &status, int last)
{
  int len;
  bool repeat;
  do
    {
    repeat = false;
    bool first = true;
    len = 0;
    for (auto s: status)
      {
	if (s.progress >= 0)
	  len += fprintf (f, "%s%s: %2.2f%%", first ? "\r" : " | ", s.task, s.progress);
	else
	  len += fprintf (f, "%s%s", first ? "\r" : " | ", s.task);
	first = false;
      }
    if (last > len)
      {
       fprintf (f, "%*s", last-len, "");
       repeat = true;
       last = len;
      }
    }
  while (repeat);
  fflush (f);
  return len;
}
void
file_progress_info::resume_stdout ()
{
  if (m_initialized || !m_file || !m_display_progress)
    return;
  if (m_displayed)
    {
      auto s = get_status ();
      m_last_printed_len = print_status (m_file, s, 0);
      m_last_status = s;
    }
  pthread_attr_t attr;
  m_exit = false;
  if (pthread_attr_init (&attr))
    {
      perror ("Can not initialize thread attributes");
      return;
    }
  if (pthread_create (&m_thread, &attr, thread_start, this))
    {
      perror ("Can not initialize thread attributes");
      return;
    }
  m_initialized = true;
}
void
file_progress_info::display_progress ()
{
  auto s = get_status ();
  if (s.size ())
    {
      m_displayed++;
      if (s != m_last_status)
	{
	  m_last_printed_len = print_status (m_file, s, m_last_printed_len);
	  m_last_status = s;
	}
      fflush (m_file);
    }
}

void
file_progress_info::set_task (const char *name, uint64_t max)
{
  bool paused = false;
  if (m_task && m_print_all_tasks && m_max >= 0)
    {
      auto s = get_status ();
      if (s.size ())
	{
	  pause_stdout ();
	  print_task (m_file, s, &m_start_time.wall);
	  paused = true;
	}
    }
  progress_info::set_task (name, max);
  if (m_print_all_tasks)
    {
      auto s = get_status ();
      if (s.size ())
	{
	  if (!paused)
	    pause_stdout ();
	  print_task (m_file, s, NULL);
	  paused = true;
	}
    }
  if (paused)
    resume_stdout ();
}
void
file_progress_info::wait (const char *name)
{
  bool paused = false;
  if (m_task && m_print_all_tasks && m_max >= 0)
    {
      auto s = get_status ();
      if (s.size ())
	{
	  pause_stdout ();
	  print_task (m_file, s, &m_start_time.wall);
	  paused = true;
	}
    }
  progress_info::wait (name);
  if (paused)
    resume_stdout ();
}
void
file_progress_info::pop (int expected)
{
  bool paused = false;
  if (m_task && m_print_all_tasks && m_max >= 0)
    {
      auto s = get_status ();
      if (s.size ())
	{
	  pause_stdout ();
	  print_task (m_file, s, &m_start_time.wall);
	  paused = true;
	}
    }
  progress_info::pop ();
  if (paused)
    resume_stdout ();
}
}
