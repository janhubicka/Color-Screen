#include <unistd.h>
#include <cerrno>
#include <math.h>
#include <sys/time.h>
#include <cstring>
#include <cstdlib>
#include "include/progress-info.h"
void
progress_info::pause_stdout ()
{
}
void
progress_info::resume_stdout ()
{
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

#ifdef __APPLE__
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
file_progress_info::file_progress_info (FILE *f, bool display)
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
  pthread_mutex_init (&m_exit_lock, NULL);
  pthread_condattr_t condattr;
  pthread_condattr_init(&condattr);
#ifndef __APPLE__
  pthread_condattr_setclock(&condattr, CLOCK_MONOTONIC);
#endif
  pthread_cond_init (&m_exit_cond, &condattr);
  if (!f || !display)
    return;
  resume_stdout ();
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
  if (!m_initialized)
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
	len += fprintf (f, "%s%s: %2.2f%%", first ? "\r" : " | ", s.task, s.progress);
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
  if (m_initialized || !m_file)
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
