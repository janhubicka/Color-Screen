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
    "src/libcolorscreen/render-tile.h",
    '''#include <sys/time.h>\n#include <mutex>\n''',
    '''#include <sys/time.h>\n#include <atomic>\n#include <mutex>\n''',
)

replace_once(
    "src/libcolorscreen/render-tile.h",
    '''static int stats __attribute__((unused)) = -1;\nstd::mutex global_rendering_lock;\n''',
    '''static std::atomic<int> stats {-1};\nstd::mutex global_rendering_lock;\nstd::mutex rendering_stats_lock;\n''',
)

replace_once(
    "src/libcolorscreen/render-tile.C",
    '''  if (stats)\n    {\n      struct timeval end_time;\n''',
    '''  if (stats)\n    {\n      std::lock_guard<std::mutex> stats_guard (rendering_stats_lock);\n      struct timeval end_time;\n''',
)
