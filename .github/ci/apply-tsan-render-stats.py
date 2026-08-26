#!/usr/bin/env python3
"""Apply the thread-safe render statistics fix in a validation checkout."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def replace_once(relative, old, new):
    path = ROOT / relative
    text = path.read_text()
    if new in text:
        print(f"{relative}: already patched")
        return
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{relative}: expected one match, found {count}")
    path.write_text(text.replace(old, new))
    print(f"{relative}: patched")


replace_once(
    "src/libcolorscreen/render-tile.C",
    '''  /* Do not render out of scan area; it is slow.  */
  if (stats == -1)
    stats = getenv ("CSSTATS") != NULL;
  struct timeval start_time;
  if (stats)
    gettimeofday (&start_time, NULL);
''',
    '''  /* Do not render out of scan area; it is slow.  Function-local static
     initialization is synchronized by C++, so concurrent GUI render tasks do
     not race while discovering whether statistics are enabled.  */
  static const bool stats_enabled = getenv ("CSSTATS") != NULL;
  struct timeval start_time;
  if (stats_enabled)
    gettimeofday (&start_time, NULL);
''',
)

replace_once(
    "src/libcolorscreen/render-tile.C",
    '''  if (stats)
    {
      struct timeval end_time;
      static struct timeval prev_time;
      static bool prev_time_set = true;
''',
    '''  if (stats_enabled)
    {
      /* CSSTATS is diagnostic output only.  Serialize its shared timing state,
         not rendering itself.  */
      static std::mutex stats_lock;
      std::lock_guard<std::mutex> stats_guard (stats_lock);
      struct timeval end_time;
      static struct timeval prev_time;
      static bool prev_time_set = true;
''',
)

replace_once(
    "src/libcolorscreen/render-scr-detect.C",
    '''  if (stats == -1)
    stats = getenv ("CSSTATS") != nullptr;
  struct timeval start_time;
  if (stats)
    gettimeofday (&start_time, nullptr);
''',
    '''  /* The old CSSTATS lazy initialization here was process-shared mutable
     state, but this timer was never consumed.  Avoid both the race and the
     dead timing work.  */
''',
)
