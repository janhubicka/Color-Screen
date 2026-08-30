#pragma once

#include <functional>

class ColorScreenApplication;

/** Start the completion-driven multi-document/view workspace churn smoke test.

    COMPLETED is called after all presentation, ownership, and close-lifetime
    invariants have passed. Any failed invariant exits APP with the dedicated
    smoke-test failure status. */
void startWorkspaceChurnSmoke(ColorScreenApplication &app,
                              std::function<void()> completed);
