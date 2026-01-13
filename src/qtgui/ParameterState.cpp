#include "ParameterState.h"
#include <vector>

// INTERNAL: Helper for solver_parameters equality
static bool areSolverParamsEqual(const colorscreen::solver_parameters &a, const colorscreen::solver_parameters &b)
{
    if (a.optimize_lens != b.optimize_lens) return false;
    if (a.optimize_tilt != b.optimize_tilt) return false;
    if (a.weighted != b.weighted) return false;
    if (!(a.center == b.center)) return false;
    
    if (a.points.size() != b.points.size()) return false;
    for (size_t i = 0; i < a.points.size(); ++i) {
        if (!(a.points[i].img == b.points[i].img)) return false;
        if (!(a.points[i].scr == b.points[i].scr)) return false;
        if (a.points[i].color != b.points[i].color) return false;
    }
    return true;
}

// ParameterState equality
bool ParameterState::operator==(const ParameterState &other) const
{
    if (!(const_cast<colorscreen::render_parameters&>(rparams) == other.rparams)) return false;
    
    if (!(const_cast<colorscreen::scr_to_img_parameters&>(scrToImg) == other.scrToImg)) return false;
    
    if (!(const_cast<colorscreen::scr_detect_parameters&>(detect) == other.detect)) return false;
    
    if (!areSolverParamsEqual(solver, other.solver)) return false;
    
    return true;
}
