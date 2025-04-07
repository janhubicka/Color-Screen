#ifndef SIMULATE_H
#define SIMULATE_H
#include "include/scr-to-img.h"

struct simulate_emulsion_parameters
{
  /* Intensities (color) to be recorded in the emulsion.  */
  rgbdata intensities;
  scr_to_img_parameters scr_to_img_params;
  /* Simulation is done in an enlargement, scale specifies the enlargement factor.  */
  coord_t scale;
  /* Simulate sharpness loss due to emulsion and space between emulsion and color screen.  */
  coord_t blur_radius;
  coord_t blur_amount;
  simulate_emulsion_parameters ()
    intensities ({0,0,0}), scr_to_img_params (), scale (1), blur_radius (0), blur_amount (0)
  {
  }
};
