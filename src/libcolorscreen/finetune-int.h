#ifndef FINETUNE_INT_H
#define FINETUNE_INT_H
#include <vector>
#include "include/finetune.h"
#include "simulate.h"
namespace colorscreen {

/* Return an explanatory string when FLAGS select mutually incompatible
   FINETUNE models, or null when the combination is supported.  */
const char *finetune_flag_error (uint64_t flags);

/* Convert the scalar dark term fitted after RGB mixing to the neutral RGB
   dark value stored by render_parameters.  FALLBACK is returned when the
   conversion is undefined or any fitted value is non-finite.  */
rgbdata finetune_render_mix_dark (rgbdata weights, luminosity_t scalar_dark,
                                  rgbdata fallback);

/* Find the fit-quality cutoff that retains RETAIN_RATIO of the most reliable
   successful RESULTS.  Failed and non-finite results are ignored.  */
bool finetune_retained_fit_score_cutoff (
    const std::vector<finetune_result> &results, coord_t retain_ratio,
    coord_t *cutoff);

/* Simulate collection of SCR through COLLECTION_SCR and return the average
   colors assigned to its red, green, and blue collecting patches in RET_RED,
   RET_GREEN, and RET_BLUE.  SIMULATED_SCREEN supplies a previously rendered
   finite capture when nonnull.  Otherwise SAMPLING, SHARPEN, and MAP describe
   the capture path to evaluate over AREA.  THRESHOLD excludes weak collecting
   patches.  Return false when one of the three collecting colors has no usable
   samples or when filtering fails.  */
bool determine_color_loss (rgbdata *ret_red, rgbdata *ret_green,
                           rgbdata *ret_blue, screen &scr,
                           screen &collection_scr,
                           simulated_screen *simulated_screen,
                           screen_sampling sampling, luminosity_t threshold,
                           const sharpen_parameters &sharpen, scr_to_img &map,
                           int_image_area area);
}
#endif
