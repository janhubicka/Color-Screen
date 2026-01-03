#ifndef FINETUNE_INT_H
#define FINETUNE_INT_H
#include "include/finetune.h"
#include "simulate.h"
namespace colorscreen
{
bool determine_color_loss (rgbdata *ret_red, rgbdata *ret_green,
                           rgbdata *ret_blue, screen &scr, screen &collection_scr,
			   simulated_screen *simulated_screen,
                           luminosity_t threshold,
			   const sharpen_parameters &sharpen,
			   scr_to_img &map, int xmin, int ymin, int xmax, int ymax);
}
#endif
