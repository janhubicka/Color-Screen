#ifndef FINETUNE_INT_H
#define FINETUNE_INT_H
#include "include/finetune.h"
#include "simulate.h"
namespace colorscreen {

/* Simulate collection of SCR through COLLECTION_SCR and return the average
   colors assigned to its red, green, and blue collecting patches in RET_RED,
   RET_GREEN, and RET_BLUE.  SIMULATED_SCREEN supplies a previously rendered
   finite capture when nonnull.  Otherwise SHARPEN and MAP describe the
   capture path to evaluate over AREA.  THRESHOLD excludes weak collecting
   patches.  Return false when one of the three collecting colors has no
   usable samples or when filtering fails.  */
bool determine_color_loss (rgbdata *ret_red, rgbdata *ret_green,
                           rgbdata *ret_blue, screen &scr,
                           screen &collection_scr,
                           simulated_screen *simulated_screen,
                           luminosity_t threshold,
                           const sharpen_parameters &sharpen, scr_to_img &map,
                           int_image_area area);
}
#endif
