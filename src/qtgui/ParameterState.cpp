#include "ParameterState.h"
#include <vector>

// ParameterState equality
bool ParameterState::operator==(const ParameterState &other) const
{
    return rparams == other.rparams &&
           scrToImg == other.scrToImg &&
           detect == other.detect &&
           solver == other.solver;
}
