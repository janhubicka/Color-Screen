#include "include/color.h"
size_t create_profile (const char *desc, xyz r, xyz g, xyz b, xyz wp, luminosity_t gamma, void **buffer);
size_t create_wide_gammut_rgb_profile (void **buffer);
size_t create_pro_photo_rgb_profile (void **buffer, xyz whitepoint = {0.96420, 1.00000, 0.82489});
size_t create_linear_pro_photo_rgb_profile (void **buffer, xyz whitepoint = {0.96420, 1.00000, 0.82489});
size_t create_linear_srgb_profile (void **buffer, xyz whitepoint = d65_white);
