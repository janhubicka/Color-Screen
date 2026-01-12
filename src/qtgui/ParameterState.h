#ifndef PARAMETER_STATE_H
#define PARAMETER_STATE_H

#include "../libcolorscreen/include/render-parameters.h"
#include "../libcolorscreen/include/scr-to-img-parameters.h"
#include "../libcolorscreen/include/scr-detect-parameters.h"
#include "../libcolorscreen/include/solver-parameters.h"

struct ParameterState {
    colorscreen::render_parameters rparams;
    colorscreen::scr_to_img_parameters scrToImg;
    colorscreen::scr_detect_parameters detect;
    colorscreen::solver_parameters solver;

    bool operator==(const ParameterState &other) const;
    bool operator!=(const ParameterState &other) const { return !(*this == other); }
};

#endif // PARAMETER_STATE_H
