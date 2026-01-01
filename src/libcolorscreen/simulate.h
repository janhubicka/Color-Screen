#ifndef SIMULATE_H
#define SIMULATE_H
#include "include/color.h"
#include "include/scr-to-img.h"
#include "include/scr-detect-parameters.h"
#include "include/render-parameters.h"
#include "screen.h"

namespace colorscreen
{
typedef mem_rgbdata simulated_screen_pixel;
typedef std::vector <mem_rgbdata> simulated_screen;
simulated_screen *
get_simulated_screen (scr_to_img_parameters &param,
		      screen *scr, int screen_id,
		      sharpen_parameters sharpen,
		      int width, int height, progress_info *progress,
		      uint64_t *id);
void release_simulated_screen (simulated_screen *s);
}

#endif
