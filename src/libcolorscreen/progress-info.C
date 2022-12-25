#include <unistd.h>
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
  usleep (300000);
  while (true)
    {
      p->display_progress ();
      usleep (100000);
    }
  return NULL;
}
file_progress_info::file_progress_info (FILE *f, bool display)
{
  // TODO: For some reason Windows pthread API will not cancel the thread.
#ifdef _WIN32
  display = false;
#endif
  m_initialized = false;
  m_file = f;
  m_displayed = 0;
  m_last_task = NULL;
  m_last_status = -1;
  if (!display)
    return;
  resume_stdout ();
}
file_progress_info::~file_progress_info ()
{
  pause_stdout (true);
}
void
file_progress_info::pause_stdout (bool final)
{
  if (!m_initialized)
    return;
  pthread_cancel (m_thread);
  pthread_join (m_thread, NULL);
  if (m_displayed)
    {
      const char *task = m_last_task;
      //fprintf (m_file, "\r%s: %2.2f%%\n", task, 100.0);
      if (task)
        {
	  if (!final)
	    {
	      int n = strlen (task) + 9;
	      fprintf (m_file, "\r%*s\r", n, "");
	    }
	  else
	    {
	      fprintf (m_file, "\r%s: %2.2f%%\n", task, 100.0);
	      m_last_task = NULL;
	      free ((void *)task);
	    }
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
void
file_progress_info::resume_stdout ()
{
  if (m_initialized)
    return;
  if (m_displayed)
    {
      const char *task;
      float status;
      get_status (&task, &status);
      fprintf (m_file, "\r%s: %2.2f%%", task, status);
      fflush (m_file);
    }
  pthread_attr_t attr;
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
  const char *task;
  float status;
  get_status (&task, &status);
  if (task)
    {
      m_displayed++;
      bool n = !m_last_task;
      if (m_last_task && strcmp (m_last_task, task))
	{
	  const char *t = m_last_task;
          fprintf (m_file, "\r%s: %2.2f%%\n", t, 100.0);
	  n = true;
	}
      else if (status - m_last_status < 0.01)
	return;
      if (n)
        fprintf (m_file, "%s: %2.2f%%", task, status);
      else
        fprintf (m_file, "\r%s: %2.2f%%", task, status);
      if (!m_last_task)
	m_last_task = strdup (task);
      else if (strcmp (m_last_task, task))
	{
	  const char *t = m_last_task;
	  m_last_task = NULL;
	  free ((void *)t);
	  m_last_task = strdup (task);
	}
      m_last_status = status;
      fflush (m_file);
    }
}
