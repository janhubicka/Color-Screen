#pragma once

#include <functional>

class ColorScreenApplication;

/** Exercise unsaved-document close decisions and application-exit rollback. */
void startDocumentLifecycleSmoke(ColorScreenApplication &app,
                                 std::function<void()> completed);
